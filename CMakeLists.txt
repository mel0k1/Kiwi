cmake_minimum_required(VERSION 3.16)

project(kiwi
    VERSION 0.1.0
    DESCRIPTION "Privacy-first web browser powered by the Mustela engine"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MUSTELA_ROOT "" CACHE PATH "Mustela engine checkout (holds include/ and the built libraries)")
set(MUSTELA_BUILD_DIR "" CACHE PATH "Mustela build directory (defaults to MUSTELA_ROOT/build)")

if(NOT MUSTELA_ROOT)
    get_filename_component(MUSTELA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../Mustela" ABSOLUTE)
endif()
if(NOT MUSTELA_BUILD_DIR)
    set(MUSTELA_BUILD_DIR "${MUSTELA_ROOT}/build")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK REQUIRED gtk+-3.0)

add_executable(kiwi
    src/main.cpp
    src/browser.cpp
    src/screens.cpp)

target_include_directories(kiwi PRIVATE
    src
    ${MUSTELA_ROOT}/include
    ${GTK_INCLUDE_DIRS})
target_link_directories(kiwi PRIVATE ${GTK_LIBRARY_DIRS} ${MUSTELA_BUILD_DIR})
target_link_libraries(kiwi PRIVATE
    mustela_view_gtk
    mustela_core
    ${GTK_LIBRARIES})
target_compile_options(kiwi PRIVATE ${GTK_CFLAGS_OTHER})

if(MSVC)
    target_compile_options(kiwi PRIVATE /W4)
else()
    target_compile_options(kiwi PRIVATE -Wall -Wextra)
endif()
