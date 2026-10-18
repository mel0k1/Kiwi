cmake_minimum_required(VERSION 3.21)

project(kiwi
    VERSION 0.11.0
    DESCRIPTION "Privacy-first web browser"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(KIWI_WITH_CEF "Build the Windows shell on CEF instead of GTK + the Mustela view" OFF)
if(KIWI_WITH_CEF)
    include(cmake/kiwi-cef.cmake)
    return()
endif()

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

if(WIN32)
    set(MUSTELA_WEBKIT_SDK "" CACHE PATH "Mustela WebKit C API SDK (include/ and lib/ with import libraries or runtime DLLs)")
    set(MUSTELA_VIEW_TARGET mustela_view_win)
else()
    set(MUSTELA_VIEW_TARGET mustela_view_gtk)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK REQUIRED gtk+-3.0)
pkg_check_modules(SOUP REQUIRED libsoup-3.0)

if(EXISTS "${MUSTELA_BUILD_DIR}/mustela-targets.cmake")
    include("${MUSTELA_BUILD_DIR}/mustela-targets.cmake")
    set(MUSTELA_LINK_LIBS mustela::${MUSTELA_VIEW_TARGET} mustela::mustela_core)
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
        set(MUSTELA_LINK_LIBS mustela::${MUSTELA_VIEW_TARGET} mustela::mustela_core)
    else()
        set(MUSTELA_LINK_LIBS ${MUSTELA_VIEW_TARGET} mustela_core)
    endif()
endif()

add_executable(kiwi
    src/main.cpp
    src/browser.cpp
    src/screens.cpp
    src/prefs.cpp
    src/context_menu.cpp
    src/updater.cpp
    src/icons.cpp
    src/start_page.cpp
    src/diagnostics.cpp
    src/diagnostics_gtk.cpp)

if(WIN32)
    target_sources(kiwi PRIVATE src/viewhost_win.cpp)
else()
    target_sources(kiwi PRIVATE src/viewhost.cpp)
endif()

target_include_directories(kiwi PRIVATE
    src
    ${MUSTELA_ROOT}/include
    ${GTK_INCLUDE_DIRS}
    ${SOUP_INCLUDE_DIRS})
target_compile_definitions(kiwi PRIVATE
    KIWI_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    KIWI_VERSION="${PROJECT_VERSION}")
target_link_directories(kiwi PRIVATE ${GTK_LIBRARY_DIRS} ${SOUP_LIBRARY_DIRS} ${MUSTELA_BUILD_DIR})
if(WIN32 AND MUSTELA_WEBKIT_SDK)
    target_link_directories(kiwi PRIVATE ${MUSTELA_WEBKIT_SDK}/lib)
endif()
target_link_libraries(kiwi PRIVATE
    ${MUSTELA_LINK_LIBS}
    ${GTK_LIBRARIES}
    ${SOUP_LIBRARIES})
if(WIN32)
    target_link_libraries(kiwi PRIVATE dbghelp)
endif()
target_compile_options(kiwi PRIVATE ${GTK_CFLAGS_OTHER} ${SOUP_CFLAGS_OTHER})

if(WIN32 AND NOT MSVC)
    target_link_options(kiwi PRIVATE -mwindows)
endif()

if(MSVC)
    target_compile_options(kiwi PRIVATE /W4)
else()
    target_compile_options(kiwi PRIVATE -Wall -Wextra)
endif()

option(KIWI_BUILD_TESTS "Build the unit tests" ON)
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

    add_executable(kiwi_start_page_tests src/tests/test_start_page.cpp src/start_page.cpp)
    target_include_directories(kiwi_start_page_tests PRIVATE
        src
        ${MUSTELA_ROOT}/include
        ${MUSTELA_ROOT}/src
        ${MUSTELA_ROOT}/src/core
        ${GTK_INCLUDE_DIRS}
        ${SOUP_INCLUDE_DIRS})
    target_link_directories(kiwi_start_page_tests PRIVATE ${GTK_LIBRARY_DIRS} ${SOUP_LIBRARY_DIRS})
    target_link_libraries(kiwi_start_page_tests PRIVATE ${GTK_LIBRARIES} ${SOUP_LIBRARIES})
    add_test(NAME kiwi_start_page_tests COMMAND kiwi_start_page_tests)
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
