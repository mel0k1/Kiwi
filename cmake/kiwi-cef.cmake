if(NOT WIN32)
    message(FATAL_ERROR "KIWI_WITH_CEF is only for Windows builds; the Linux path uses GTK + Mustela")
endif()

set(CEF_ROOT "" CACHE PATH "CEF binary distribution root (cef_binary_*_windows64_minimal)")
if(NOT CEF_ROOT AND DEFINED ENV{CEF_ROOT})
    file(TO_CMAKE_PATH "$ENV{CEF_ROOT}" CEF_ROOT)
endif()
if(NOT EXISTS "${CEF_ROOT}/cmake/FindCEF.cmake")
    message(FATAL_ERROR "CEF_ROOT must point to a CEF binary distribution (cef_binary_*_minimal)")
endif()

set(MUSTELA_BUILD_SHELL OFF CACHE BOOL "" FORCE)
set(MUSTELA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MUSTELA_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(MUSTELA_BUILD_VIEW OFF CACHE BOOL "" FORCE)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING
    "CEF links the static CRT; every target in this tree must match" FORCE)

include(FetchContent)
FetchContent_Declare(mustela
    GIT_REPOSITORY https://github.com/mel0k1/Mustela.git
    GIT_TAG v0.25.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(mustela)

list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)
add_subdirectory(${CEF_LIBCEF_DLL_WRAPPER_PATH} libcef_dll_wrapper)
ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")

add_library(kiwi SHARED
    src/cef/cef_app.cpp
    src/cef/cef_client.cpp
    src/cef/cef_runtime.cpp
    src/cef/win/cef_main_win.cpp
    src/cef/win/cef_shell_win.cpp
    src/cef/win/site_rules_win.cpp
    src/start_page.cpp
    src/diagnostics.cpp)

# Sandbox model since CEF 138: the client is a DLL exporting RunWinMain,
# loaded by the distribution's bootstrap.exe which prepares the sandbox
# context. Rename it to kiwi.exe so the shell keeps its name and the browser
# subprocess path keeps working (subprocesses re-enter through kiwi.exe).
if(NOT EXISTS "${CEF_ROOT}/Release/bootstrap.exe")
    message(FATAL_ERROR "CEF dist has no Release/bootstrap.exe — the sandboxed Windows build requires the bootstrap loader (CEF 138+ model)")
endif()
add_custom_command(TARGET kiwi POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CEF_ROOT}/Release/bootstrap.exe" "$<TARGET_FILE_DIR:kiwi>/kiwi.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cef/win/kiwi.exe.manifest"
        "$<TARGET_FILE_DIR:kiwi>/kiwi.exe.manifest"
    COMMENT "Renaming CEF bootstrap.exe to kiwi.exe and shipping the manifest")

target_include_directories(kiwi PRIVATE
    src
    ${CEF_ROOT})
target_compile_definitions(kiwi PRIVATE
    KIWI_VERSION="${PROJECT_VERSION}"
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN)
target_link_libraries(kiwi PRIVATE
    mustela_core
    libcef_lib
    libcef_dll_wrapper
    comctl32)

SET_LIBRARY_TARGET_PROPERTIES("kiwi")
add_dependencies(kiwi libcef_dll_wrapper)

SET_CEF_TARGET_OUT_DIR()
COPY_FILES(kiwi "${CEF_BINARY_FILES}" "${CEF_ROOT}/Release" "${CEF_TARGET_OUT_DIR}")
COPY_FILES(kiwi "${CEF_RESOURCE_FILES}" "${CEF_ROOT}/Resources" "${CEF_TARGET_OUT_DIR}")

option(KIWI_BUILD_TESTS "Build the unit tests" ON)
if(KIWI_BUILD_TESTS)
    enable_testing()
    add_executable(kiwi_start_page_tests src/tests/test_start_page.cpp src/start_page.cpp)
    target_include_directories(kiwi_start_page_tests PRIVATE src)
    target_link_libraries(kiwi_start_page_tests PRIVATE mustela_core)
    add_test(NAME kiwi_start_page_tests COMMAND kiwi_start_page_tests)
endif()
