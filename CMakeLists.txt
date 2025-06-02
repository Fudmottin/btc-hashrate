# CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(btc-hashrate VERSION 0.1 LANGUAGES CXX)

# Policy for Boost (no-op on older CMake versions)
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

# C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build type
option(BUILD_DEBUG "Build with debug info" OFF)
if(BUILD_DEBUG)
    set(CMAKE_BUILD_TYPE Debug)
else()
    set(CMAKE_BUILD_TYPE Release)
endif()

# Boost.JSON
find_package(Boost REQUIRED COMPONENTS json)

# Executable
add_executable(btc-hashrate src/main.cpp)
target_link_libraries(btc-hashrate PRIVATE Boost::json)

# Optimization for Release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(btc-hashrate PRIVATE -O3)
endif()

