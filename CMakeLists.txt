cmake_minimum_required(VERSION 3.16)
project(SimpleDBEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/bin")
endforeach()

add_library(simpledb_core
    src/sql.cpp
    src/database.cpp
    src/bplustree.cpp
)

target_include_directories(simpledb_core PUBLIC include)

if(MSVC)
    target_compile_options(simpledb_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(simpledb_core PRIVATE -Wall -Wextra -pedantic)
endif()

add_executable(simpledb
    src/main.cpp
)

target_link_libraries(simpledb PRIVATE simpledb_core)

enable_testing()

add_executable(simpledb_tests
    tests/regression.cpp
)

target_link_libraries(simpledb_tests PRIVATE simpledb_core)

add_test(NAME simpledb_regression COMMAND $<TARGET_FILE:simpledb_tests>)
