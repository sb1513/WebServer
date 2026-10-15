
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
const size_t MAX_REQUEST_SIZE = 16384;

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
    std::string request;
    char buffer[4096];

    while (request.find("\r\n\r\n") == std::string::npos) {
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

        request.append(buffer, static_cast<size_t>(n));

        if (request.size() > MAX_REQUEST_SIZE) {
            std::string body = "<h1>431 Request Header Fields Too Large</h1>";
            sendAll(client_fd, makeResponse(
                431, "Request Header Fields Too Large",
                "text/html; charset=utf-8", body
            ));
            return;
        }
    }

    size_t line_end = request.find("\r\n");

    if (line_end == std::string::npos) {
        std::string body = "<h1>400 Bad Request</h1>";
        sendAll(client_fd, makeResponse(
            400, "Bad Request",
            "text/html; charset=utf-8", body
        ));
        return;
    }

    std::string request_line = request.substr(0, line_end);
    std::istringstream line_stream(request_line);

    std::string method;
    std::string path;
    std::string version;
    std::string extra;

    if (!(line_stream >> method >> path >> version) ||
        (line_stream >> extra) ||
        version != "HTTP/1.1" ||
        path.empty() || path[0] != '/') {
        std::string body = "<h1>400 Bad Request</h1>";
        sendAll(client_fd, makeResponse(
            400, "Bad Request",
            "text/html; charset=utf-8", body
        ));
        return;
    }

    size_t query_pos = path.find('?');
    if (query_pos != std::string::npos) {
        path = path.substr(0, query_pos);
    }

    std::cout << method << " " << path
              << " " << version << std::endl;

    if (method != "GET") {
        std::string body = "<h1>405 Method Not Allowed</h1>";
        std::string response = makeResponse(
            405, "Method Not Allowed",
            "text/html; charset=utf-8", body
        );

        response.insert(
            response.find("\r\n") + 2,
            "Allow: GET\r\n"
        );

        sendAll(client_fd, response);
        return;
    }

    if (path == "/") {
        std::string body =
            "<!DOCTYPE html>"
            "<html lang=\"en\">"
            "<head>"
            "<meta charset=\"UTF-8\">"
            "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
            "<title>MyWebServer</title>"
            "</head>"
            "<body>"
            "<h1>Hello, WebServer!</h1>"
            "<p>HTTP/1.1 is working.</p>"
            "<p>This page is served by a C++ server.</p>"
            "<p><a href=\"/hello\">Visit the hello page</a></p>"
            "</body>"
            "</html>";

        sendAll(client_fd, makeResponse(
            200, "OK", "text/html; charset=utf-8", body
        ));
    } else if (path == "/hello") {
        std::string body =
            "<!DOCTYPE html>"
            "<html><head><meta charset=\"UTF-8\">"
            "<title>Hello</title></head>"
            "<body><h1>Hello from C++!</h1>"
            "<a href=\"/\">Back to home</a></body></html>";

        sendAll(client_fd, makeResponse(
            200, "OK", "text/html; charset=utf-8", body
        ));
    } else {
        std::string body =
            "<!DOCTYPE html>"
            "<html><head><meta charset=\"UTF-8\">"
            "<title>404 Not Found</title></head>"
            "<body><h1>404 Not Found</h1>"
            "<p>The requested page does not exist.</p>"
            "</body></html>";

        sendAll(client_fd, makeResponse(
            404, "Not Found", "text/html; charset=utf-8", body
        ));
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
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) == -1) {
        std::cerr << "setsockopt failed: "
                  << strerror(errno) << std::endl;
        close(listen_fd);
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    if (bind(listen_fd,
             reinterpret_cast<sockaddr*>(&server_addr),
             sizeof(server_addr)) == -1) {
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
