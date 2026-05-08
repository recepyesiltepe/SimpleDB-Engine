cmake_minimum_required(VERSION 3.16)
project(SimpleDBEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(simpledb
    src/main.cpp
    src/sql.cpp
    src/database.cpp
    src/bplustree.cpp
)

target_include_directories(simpledb PRIVATE include)
