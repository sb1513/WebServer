#include <iostream>
#include <cstring>
#include <string>
#include <cerrno>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

const int PORT = 8080;
const int BUFFER_SIZE = 1024;

bool sendAll(int fd, const char* data, size_t length) {
    size_t sent = 0;

    while (sent < length) {
        ssize_t n = send(fd, data + sent, length - sent, 0);

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

int main()
{
    int listen_fd = socket(AF_INET,SOCK_STREAM,0);
    
    if (listen_fd == -1)
    {
        std::cerr<<"socket fail: "<<strerror(errno)<<std::endl;
        return 1;
    }

    int opt = 1;
    if(setsockopt(listen_fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt))==-1)
    {
        std::cerr<<"setsockopt failed: "<< strerror(errno) << std::endl;
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

    std::cout << "TCP server listening on port "
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

        char ip[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &client_addr.sin_addr,
                  ip, sizeof(ip));

        std::cout << "Client connected: "
                  << ip << ":"
                  << ntohs(client_addr.sin_port)
                  << std::endl;

        char buffer[BUFFER_SIZE];

        while (true) {
            ssize_t n = recv(
                client_fd, buffer, sizeof(buffer), 0
            );

            if (n == 0) {
                std::cout << "Client disconnected"
                          << std::endl;
                break;
            }

            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }

                std::cerr << "recv failed: "
                          << strerror(errno) << std::endl;
                break;
            }

            std::string message(buffer,
                                static_cast<size_t>(n));

            std::cout << "Received: " << message
                      << std::endl;

            std::string response = "Server received: " + message;

            if (!sendAll(client_fd, response.data(),
                         response.size())) {
                std::cerr << "send failed: "
                          << strerror(errno) << std::endl;
                break;
            }
        }

        close(client_fd);
    }

    close(listen_fd);
    return 0;
}