#include "http/http_request.h"

#include <iostream>
#include <string>
#include <sstream>
#include <cstring>
#include <cerrno>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>

const int PORT = 8081;
const int MAX_EVENTS = 1024;

struct Connection {
    int fd;
    HttpRequestParser parser;
    std::string out;
    size_t sent;
    bool closing;

    explicit Connection(int clientFd)
        : fd(clientFd), sent(0), closing(false) {
    }
};

bool setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags == -1) {
        return false;
    }

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

std::string makeResponse(
    int status,
    const std::string& reason,
    const std::string& contentType,
    const std::string& body
) {
    std::ostringstream response;

    response << "HTTP/1.1 " << status << " " << reason << "\r\n";
    response << "Content-Type: " << contentType << "\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "Connection: close\r\n";
    response << "\r\n";
    response << body;

    return response.str();
}

std::string makeErrorResponse(ParseResult result) {
    if (result == ParseResult::PAYLOAD_TOO_LARGE) {
        return makeResponse(
            413, "Content Too Large", "text/html; charset=utf-8",
            "<h1>413 Content Too Large</h1>"
        );
    }

    if (result == ParseResult::UNSUPPORTED) {
        return makeResponse(
            501, "Not Implemented", "text/html; charset=utf-8",
            "<h1>501 Not Implemented</h1>"
        );
    }

    return makeResponse(
        400, "Bad Request", "text/html; charset=utf-8",
        "<h1>400 Bad Request</h1>"
    );
}

std::string routeRequest(const HttpRequest& request) {
    const std::string& method = request.method;
    const std::string& path = request.path;

    if (method != "GET" && method != "POST" && method != "HEAD") {
        std::string response = makeResponse(
            405, "Method Not Allowed", "text/html; charset=utf-8",
            "<h1>405 Method Not Allowed</h1>"
        );

        response.insert(
            response.find("\r\n") + 2,
            "Allow: GET, POST, HEAD\r\n"
        );

        return response;
    }

    if (method == "POST" && path == "/echo") {
        return makeResponse(
            200, "OK", "text/plain; charset=utf-8",
            request.body
        );
    }

    if (method == "GET" && path == "/") {
        std::string body =
            "<!DOCTYPE html><html><head>"
            "<meta charset=\"UTF-8\"><title>MyWebServer</title>"
            "</head><body><h1>Hello, WebServer!</h1>"
            "<p>epoll ET mode is working.</p>"
            "<a href=\"/hello\">Hello page</a>"
            "</body></html>";

        return makeResponse(200, "OK", "text/html; charset=utf-8", body);
    }

    if (method == "GET" && path == "/hello") {
        std::string body =
            "<!DOCTYPE html><html><head>"
            "<meta charset=\"UTF-8\"><title>Hello</title>"
            "</head><body><h1>Hello from C++!</h1>"
            "<a href=\"/\">Home</a></body></html>";

        return makeResponse(200, "OK", "text/html; charset=utf-8", body);
    }

    return makeResponse(
        404, "Not Found", "text/html; charset=utf-8",
        "<h1>404 Not Found</h1>"
    );
}

void modifyEvents(int epollFd, int fd, uint32_t events) {
    epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    epoll_ctl(epollFd, EPOLL_CTL_MOD, fd, &ev);
}

void closeConnection(
    int epollFd,
    std::unordered_map<int, Connection>& connections,
    int fd
) {
    epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
    connections.erase(fd);

    std::cout << "Connection closed: fd " << fd << std::endl;
}

void handleAccept(
    int listenFd,
    int epollFd,
    std::unordered_map<int, Connection>& connections
) {
    while (true) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);

        int clientFd = accept(
            listenFd,
            reinterpret_cast<sockaddr*>(&clientAddr),
            &clientLen
        );

        if (clientFd == -1) {
            if (errno == EINTR) {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }

            std::cerr << "accept failed: "
                      << strerror(errno) << std::endl;
            break;
        }

        setNonBlocking(clientFd);
        connections.emplace(clientFd, Connection(clientFd));

        epoll_event ev;
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = clientFd;

        if (epoll_ctl(epollFd, EPOLL_CTL_ADD, clientFd, &ev) == -1) {
            std::cerr << "epoll add failed: "
                      << strerror(errno) << std::endl;
            close(clientFd);
            connections.erase(clientFd);
            continue;
        }

        char ip[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));

        std::cout << "Client connected: "
                  << ip << ":" << ntohs(clientAddr.sin_port)
                  << " (fd " << clientFd << ")" << std::endl;
    }
}

void handleWrite(
    int epollFd,
    std::unordered_map<int, Connection>& connections,
    int fd
) {
    auto it = connections.find(fd);

    if (it == connections.end()) {
        return;
    }

    Connection& conn = it->second;

    while (conn.sent < conn.out.size()) {
        ssize_t n = send(
            fd,
            conn.out.data() + conn.sent,
            conn.out.size() - conn.sent,
            MSG_NOSIGNAL
        );

        if (n > 0) {
            conn.sent += static_cast<size_t>(n);
            continue;
        }

        if (n < 0 && errno == EINTR) {
            continue;
        }

        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        }

        closeConnection(epollFd, connections, fd);
        return;
    }

    if (conn.closing) {
        closeConnection(epollFd, connections, fd);
    }
}

void handleRead(
    int epollFd,
    std::unordered_map<int, Connection>& connections,
    int fd
) {
    auto it = connections.find(fd);

    if (it == connections.end()) {
        return;
    }

    Connection& conn = it->second;
    char buffer[4096];
    bool responseReady = false;

    while (true) {
        ssize_t n = recv(fd, buffer, sizeof(buffer), 0);

        if (n > 0) {
            if (!responseReady) {
                ParseResult result = conn.parser.append(
                    buffer,
                    static_cast<size_t>(n)
                );

                if (result != ParseResult::INCOMPLETE) {
                    conn.out = (result == ParseResult::COMPLETE)
                        ? routeRequest(conn.parser.request())
                        : makeErrorResponse(result);

                    conn.sent = 0;
                    conn.closing = true;
                    responseReady = true;
                }
            }

            continue;
        }

        if (n == 0) {
            closeConnection(epollFd, connections, fd);
            return;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (responseReady) {
                handleWrite(epollFd, connections, fd);

                if (connections.find(fd) != connections.end()) {
                    modifyEvents(
                        epollFd,
                        fd,
                        EPOLLOUT | EPOLLET
                    );
                }
            }

            return;
        }

        closeConnection(epollFd, connections, fd);
        return;
    }
}

int main() {
    int listenFd = socket(AF_INET, SOCK_STREAM, 0);

    if (listenFd == -1) {
        std::cerr << "socket failed: "
                  << strerror(errno) << std::endl;
        return 1;
    }

    int opt = 1;

    if (setsockopt(
            listenFd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &opt,
            sizeof(opt)
        ) == -1) {
        std::cerr << "setsockopt failed: "
                  << strerror(errno) << std::endl;
        close(listenFd);
        return 1;
    }

    if (!setNonBlocking(listenFd)) {
        std::cerr << "set non-blocking failed: "
                  << strerror(errno) << std::endl;
        close(listenFd);
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(PORT);

    if (bind(
            listenFd,
            reinterpret_cast<sockaddr*>(&serverAddr),
            sizeof(serverAddr)
        ) == -1) {
        std::cerr << "bind failed: "
                  << strerror(errno) << std::endl;
        close(listenFd);
        return 1;
    }

    if (listen(listenFd, 128) == -1) {
        std::cerr << "listen failed: "
                  << strerror(errno) << std::endl;
        close(listenFd);
        return 1;
    }

    int epollFd = epoll_create1(0);

    if (epollFd == -1) {
        std::cerr << "epoll_create1 failed: "
                  << strerror(errno) << std::endl;
        close(listenFd);
        return 1;
    }

    epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = listenFd;

    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, listenFd, &ev) == -1) {
        std::cerr << "epoll_ctl failed: "
                  << strerror(errno) << std::endl;
        close(epollFd);
        close(listenFd);
        return 1;
    }

    std::unordered_map<int, Connection> connections;
    std::vector<epoll_event> events(MAX_EVENTS);

    std::cout << "epoll(ET) HTTP server listening on port "
              << PORT << std::endl;

    while (true) {
        int ready = epoll_wait(
            epollFd,
            events.data(),
            MAX_EVENTS,
            -1
        );

        if (ready == -1) {
            if (errno == EINTR) {
                continue;
            }

            std::cerr << "epoll_wait failed: "
                      << strerror(errno) << std::endl;
            break;
        }

        for (int i = 0; i < ready; ++i) {
            int fd = events[i].data.fd;
            uint32_t flags = events[i].events;

            if (fd == listenFd) {
                handleAccept(listenFd, epollFd, connections);
                continue;
            }

            if (flags & EPOLLIN) {
                handleRead(epollFd, connections, fd);
            }

            if (connections.find(fd) == connections.end()) {
                continue;
            }

            if (flags & EPOLLOUT) {
                handleWrite(epollFd, connections, fd);
            }

            if (connections.find(fd) != connections.end() &&
                (flags & (EPOLLHUP | EPOLLERR))) {
                closeConnection(epollFd, connections, fd);
            }
        }
    }

    close(epollFd);
    close(listenFd);
    return 0;
}