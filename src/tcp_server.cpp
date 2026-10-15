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

bool sendAll(int fd, const char* data, size_t length) {//多次发送直到内容全部送达
    size_t sent = 0;

    while (sent < length) {
        ssize_t n = send(fd, data + sent, length - sent, 0);//目标套接字和发送的信息，长度

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
    int listen_fd = socket(AF_INET,SOCK_STREAM,0);//ipv4，流式
    
    if (listen_fd == -1)
    {
        std::cerr<<"socket fail: "<<strerror(errno)<<std::endl;//返回错误信息，把errno错误码用strerror转化
        return 1;
    }

    int opt = 1;
    if(setsockopt(listen_fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt))==-1)//选择协议和重启时地址复用
    {
        std::cerr<<"setsockopt failed: "<< strerror(errno) << std::endl;
        return 1;
    }

    sockaddr_in server_addr{};//用来存ipv4和端口的结构体
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);//host to network转成网络字节序，长整型转换
    server_addr.sin_port = htons(PORT);//短整型转换

    if (bind(listen_fd,//绑定监听的端口和地址
             reinterpret_cast<sockaddr*>(&server_addr),//强制通过二进制转化成地址类型
             sizeof(server_addr)) == -1) {
        std::cerr << "bind failed: "
                  << strerror(errno) << std::endl;
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 128) == -1) {//监听目标文件描述符，请求队列最多128个
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

        int client_fd = accept(//客户端接受监听套接字
            listen_fd,
            reinterpret_cast<sockaddr*>(&client_addr),//客户端地址
            &client_len
        );

        if (client_fd == -1) {
            if (errno == EINTR) {//用户终止连接
                continue;
            }

            std::cerr << "accept failed: "
                      << strerror(errno) << std::endl;
            continue;
        }

        char ip[INET_ADDRSTRLEN]{};//字符串ip转网络整数的长度
        inet_ntop(AF_INET, &client_addr.sin_addr,//网络字节序二进制ip转文本
                  ip, sizeof(ip));

        std::cout << "Client connected: "
                  << ip << ":"
                  << ntohs(client_addr.sin_port)
                  << std::endl;

        char buffer[BUFFER_SIZE];

        while (true) {
            ssize_t n = recv(//有符号size类型，接收来自客户端数据，放在buffer里
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