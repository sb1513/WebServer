cmake_minimum_required(VERSION 3.16)

project(MyWebServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(server src/main.cpp)
add_executable(tcp_server src/tcp_server.cpp)
add_executable(http_server src/http_server.cpp)