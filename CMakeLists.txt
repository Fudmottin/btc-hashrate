# CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(btc-hashrate VERSION 0.1 LANGUAGES CXX)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

find_package(Boost REQUIRED COMPONENTS json)

add_executable(btc-hashrate src/pool-names.cpp src/main.cpp)
target_link_libraries(btc-hashrate PRIVATE Boost::json)

target_compile_features(btc-hashrate PRIVATE cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(
        btc-hashrate PRIVATE
        $<$<CONFIG:Debug>:-O0 -g>
        $<$<CONFIG:Release>:-O3 -DNDEBUG>
    )
endif()

