#include "http/http_request.h"

#include <iostream>
#include <string>
#include <sstream>
#include <cstring>
#include <cerrno>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

const int PORT = 8080;

bool sendAll(int fd, const std::string& data) {
    size_t sent = 0;

    while (sent < data.size()) {
        ssize_t n = send(
            fd,
            data.data() + sent,
            data.size() - sent,
            MSG_NOSIGNAL
        );

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }

            return false;
        }

        if (n == 0) {
            return false;
        }

        sent += static_cast<size_t>(n);
    }

    return true;
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

void handleClient(int client_fd) {
    HttpRequestParser parser;
    char buffer[4096];

    while (true) {
        ssize_t n = recv(client_fd, buffer, sizeof(buffer), 0);

        if (n == 0) {
            return;
        }

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }

            std::cerr << "recv failed: "
                      << strerror(errno) << std::endl;
            return;
        }

        ParseResult result = parser.append(
            buffer,
            static_cast<size_t>(n)
        );

        if (result == ParseResult::INCOMPLETE) {
            continue;
        }

        if (result != ParseResult::COMPLETE) {
            int status = 400;
            std::string reason = "Bad Request";

            if (result == ParseResult::PAYLOAD_TOO_LARGE) {
                status = 413;
                reason = "Content Too Large";
            } else if (result == ParseResult::UNSUPPORTED) {
                status = 501;
                reason = "Not Implemented";
            }

            std::string body = "<h1>" + reason + "</h1>";

            sendAll(client_fd, makeResponse(
                status,
                reason,
                "text/html; charset=utf-8",
                body
            ));

            return;
        }

        const HttpRequest& request = parser.request();

        const std::string& method = request.method;
        const std::string& path = request.path;

        std::cout << method << " " << path
                  << " " << request.version << std::endl;

        if (method != "GET" && method != "POST") {
            std::string body = "<h1>405 Method Not Allowed</h1>";

            std::string response = makeResponse(
                405,
                "Method Not Allowed",
                "text/html; charset=utf-8",
                body
            );

            response.insert(
                response.find("\r\n") + 2,
                "Allow: GET, POST\r\n"
            );

            sendAll(client_fd, response);
            return;
        }

        std::string route = path;
        size_t query_pos = route.find('?');

        if (query_pos != std::string::npos) {
            route = route.substr(0, query_pos);
        }

        if (method == "POST" && route == "/echo") {
            sendAll(client_fd, makeResponse(
                200,
                "OK",
                "text/plain; charset=utf-8",
                request.body
            ));

            return;
        }

        if (method == "GET" && route == "/") {
            std::string body =
                "<!DOCTYPE html><html lang=\"en\"><head>"
                "<meta charset=\"UTF-8\"><title>MyWebServer</title>"
                "</head><body><h1>Hello, WebServer!</h1>"
                "<p>HTTP parser state machine is working.</p>"
                "<a href=\"/hello\">Hello page</a>"
                "</body></html>";

            sendAll(client_fd, makeResponse(
                200,
                "OK",
                "text/html; charset=utf-8",
                body
            ));

            return;
        }

        if (method == "GET" && route == "/hello") {
            std::string body =
                "<!DOCTYPE html><html><head>"
                "<meta charset=\"UTF-8\"><title>Hello</title>"
                "</head><body><h1>Hello from C++!</h1>"
                "<a href=\"/\">Home</a></body></html>";

            sendAll(client_fd, makeResponse(
                200,
                "OK",
                "text/html; charset=utf-8",
                body
            ));

            return;
        }

        sendAll(client_fd, makeResponse(
            404,
            "Not Found",
            "text/html; charset=utf-8",
            "<h1>404 Not Found</h1>"
        ));

        return;
    }
}

int main() {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (listen_fd == -1) {
        std::cerr << "socket failed: "
                  << strerror(errno) << std::endl;
        return 1;
    }

    int opt = 1;

    if (setsockopt(
            listen_fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &opt,
            sizeof(opt)
        ) == -1) {
        std::cerr << "setsockopt failed: "
                  << strerror(errno) << std::endl;
        close(listen_fd);
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    if (bind(
            listen_fd,
            reinterpret_cast<sockaddr*>(&server_addr),
            sizeof(server_addr)
        ) == -1) {
        std::cerr << "bind failed: "
                  << strerror(errno) << std::endl;
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 128) == -1) {
        std::cerr << "listen failed: "
                  << strerror(errno) << std::endl;
        close(listen_fd);
        return 1;
    }

    std::cout << "HTTP server listening on port "
              << PORT << std::endl;

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(
            listen_fd,
            reinterpret_cast<sockaddr*>(&client_addr),
            &client_len
        );

        if (client_fd == -1) {
            if (errno == EINTR) {
                continue;
            }

            std::cerr << "accept failed: "
                      << strerror(errno) << std::endl;
            continue;
        }

        handleClient(client_fd);
        close(client_fd);
    }

    close(listen_fd);
    return 0;
}
