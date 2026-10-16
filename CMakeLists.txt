cmake_minimum_required(VERSION 3.16)

project(MyWebServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(server src/main.cpp)
add_executable(tcp_server src/tcp_server.cpp)
add_executable(
    http_server
    src/http_server.cpp
    src/http/http_request.cpp
)

target_include_directories(http_server PRIVATE include)
add_executable(
    epoll_server
    src/epoll_server.cpp
    src/http/http_request.cpp
)

target_include_directories(epoll_server PRIVATE include)

add_executable(
    reactor
    src/epoll_reactor_server.cpp
    src/thread_pool.cpp
    src/http/http_request.cpp
)

target_include_directories(reactor PRIVATE include)