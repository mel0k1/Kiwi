cmake_minimum_required(VERSION 3.16)

project(kiwi
    VERSION 0.1.0
    DESCRIPTION "Privacy-first web browser powered by the Mustela engine"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(kiwi src/main.cpp)

if(MSVC)
    target_compile_options(kiwi PRIVATE /W4)
else()
    target_compile_options(kiwi PRIVATE -Wall -Wextra)
endif()
