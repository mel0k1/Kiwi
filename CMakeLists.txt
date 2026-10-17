cmake_minimum_required(VERSION 3.16)

project(kiwi
    VERSION 0.6.0
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
pkg_check_modules(SOUP REQUIRED libsoup-3.0)

if(EXISTS "${MUSTELA_BUILD_DIR}/mustela-targets.cmake")
    include("${MUSTELA_BUILD_DIR}/mustela-targets.cmake")
    set(MUSTELA_LINK_LIBS mustela::mustela_view_gtk mustela::mustela_core)
else()
    find_file(MUSTELA_TARGETS_FILE mustela-targets.cmake
        HINTS
            ${MUSTELA_CMAKE_DIR}
            ${CMAKE_PREFIX_PATH}/lib/cmake/mustela
            /app/lib/cmake/mustela
            /usr/lib/cmake/mustela
            /usr/local/lib/cmake/mustela)
    if(MUSTELA_TARGETS_FILE)
        include("${MUSTELA_TARGETS_FILE}")
        set(MUSTELA_LINK_LIBS mustela::mustela_view_gtk mustela::mustela_core)
    else()
        set(MUSTELA_LINK_LIBS mustela_view_gtk mustela_core)
    endif()
endif()

add_executable(kiwi
    src/main.cpp
    src/browser.cpp
    src/screens.cpp
    src/prefs.cpp
    src/context_menu.cpp
    src/updater.cpp)

target_include_directories(kiwi PRIVATE
    src
    ${MUSTELA_ROOT}/include
    ${GTK_INCLUDE_DIRS}
    ${SOUP_INCLUDE_DIRS})
target_compile_definitions(kiwi PRIVATE
    KIWI_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    KIWI_VERSION="${PROJECT_VERSION}")
target_link_directories(kiwi PRIVATE ${GTK_LIBRARY_DIRS} ${SOUP_LIBRARY_DIRS} ${MUSTELA_BUILD_DIR})
target_link_libraries(kiwi PRIVATE
    ${MUSTELA_LINK_LIBS}
    ${GTK_LIBRARIES}
    ${SOUP_LIBRARIES})
target_compile_options(kiwi PRIVATE ${GTK_CFLAGS_OTHER} ${SOUP_CFLAGS_OTHER})

if(MSVC)
    target_compile_options(kiwi PRIVATE /W4)
else()
    target_compile_options(kiwi PRIVATE -Wall -Wextra)
endif()

option(KIWI_BUILD_TESTS "Build the updater unit tests" ON)
if(KIWI_BUILD_TESTS)
    enable_testing()
    add_executable(kiwi_updater_tests src/tests/test_updater.cpp)
    target_include_directories(kiwi_updater_tests PRIVATE
        src
        ${MUSTELA_ROOT}/include
        ${MUSTELA_ROOT}/src
        ${MUSTELA_ROOT}/src/core
        ${GTK_INCLUDE_DIRS}
        ${SOUP_INCLUDE_DIRS})
    target_compile_definitions(kiwi_updater_tests PRIVATE
        KIWI_ASSET_DIR=""
        KIWI_VERSION="${PROJECT_VERSION}")
    target_link_directories(kiwi_updater_tests PRIVATE
        ${GTK_LIBRARY_DIRS} ${SOUP_LIBRARY_DIRS} ${MUSTELA_BUILD_DIR})
    target_link_libraries(kiwi_updater_tests PRIVATE ${GTK_LIBRARIES} ${SOUP_LIBRARIES})
    add_test(NAME kiwi_updater_tests COMMAND kiwi_updater_tests)
endif()

include(GNUInstallDirs)
install(TARGETS kiwi RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
set(KIWI_ICON_SIZES 16 22 24 32 48 64 128 256)
foreach(size IN LISTS KIWI_ICON_SIZES)
    install(FILES "assets/icons/hicolor/${size}x${size}/apps/io.github.mel0k1.Kiwi.png"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps")
endforeach()
install(FILES linux/io.github.mel0k1.Kiwi.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES linux/io.github.mel0k1.Kiwi.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
if(UNIX AND EXISTS "${MUSTELA_BUILD_DIR}/libmustela-webext.so")
    install(FILES "${MUSTELA_BUILD_DIR}/libmustela-webext.so"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/mustela/webext)
endif()

set(CPACK_PACKAGE_NAME "kiwi")
set(CPACK_PACKAGE_VENDOR "mel0k1")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Privacy-first web browser powered by the Mustela engine")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "kiwi")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_DEBIAN_PACKAGE_NAME "kiwi")
set(CPACK_DEBIAN_PACKAGE_SECTION "web")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "mel0k1 <218306052+mel0k1@users.noreply.github.com>")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
include(CPack)
