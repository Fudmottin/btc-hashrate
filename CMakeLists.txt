# CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(btc-hashrate VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_DEBUG "Build with debug info" OFF)

if(BUILD_DEBUG)
    set(CMAKE_BUILD_TYPE Debug)
else()
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Boost REQUIRED COMPONENTS json)

add_executable(btc-hashrate src/main.cpp)
target_link_libraries(btc-hashrate PRIVATE Boost::json)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(btc-hashrate PRIVATE -O3)
endif()

