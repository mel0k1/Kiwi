#include "diagnostics.hpp"

#include <glib/gstdio.h>

#include <cstdio>
#include <ctime>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#endif

namespace kiwi {

std::string diagnostics_dir() {
    std::string path;
    const gchar* base = g_get_user_config_dir();
    if (base) path = std::string(base) + G_DIR_SEPARATOR_S "kiwi";
    if (!path.empty()) g_mkdir_with_parents(path.c_str(), 0700);
    return path;
}

namespace {

void append_line(const std::string& path, const std::string& line) {
    FILE* file = g_fopen(path.c_str(), "ab");
    if (!file) return;
    std::time_t now = std::time(nullptr);
    char stamp[32];
    std::tm tm_now{};
#ifdef _WIN32
    localtime_s(&tm_now, &now);
#else
    localtime_r(&now, &tm_now);
#endif
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm_now);
    std::fprintf(file, "[%s] %s\n", stamp, line.c_str());
    fclose(file);
}

}  // namespace

#ifdef _WIN32

namespace {

std::wstring to_wide_path(const std::string& path) {
    int size = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()),
        nullptr, 0);
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()),
        wide.data(), size);
    return wide;
}

std::string module_name_for(void* address) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(address), &module)
        || !module) {
        return "?";
    }
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(module, path, MAX_PATH);
    if (len <= 0 || len >= MAX_PATH) return "?";
    std::wstring wide(path, len);
    size_t slash = wide.find_last_of(L"\\/");
    return slash == std::wstring::npos ? "?" : std::string(wide.begin() + slash + 1, wide.end());
}

LONG WINAPI kiwi_unhandled_exception(EXCEPTION_POINTERS* info) {
    void* frames[32];
    unsigned short count = RtlCaptureStackBackTrace(0, 32, frames, nullptr);
    std::string line = "fatal exception 0x"
        + std::to_string(info && info->ExceptionRecord
                ? static_cast<unsigned long long>(info->ExceptionRecord->ExceptionCode)
                : 0ull)
        + " at 0x"
        + std::to_string(reinterpret_cast<unsigned long long>(
            info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress : nullptr));
    for (unsigned short i = 0; i < count && i < 8; ++i) {
        line += " | [" + std::to_string(i) + "] "
            + module_name_for(frames[i]) + "+0x";
        char suffix[32];
        std::snprintf(suffix, sizeof(suffix), "%p", frames[i]);
        line += suffix;
    }
    std::string dir = diagnostics_dir();
    append_line(dir + G_DIR_SEPARATOR_S "crash.log", line);

    // Best-effort minidump for post-mortem analysis; failures are fine.
    if (info) {
        HANDLE process = GetCurrentProcess();
        HANDLE file = CreateFileW(
            to_wide_path(dir + G_DIR_SEPARATOR_S "crash.dmp").c_str(),
            GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION dump_info;
            dump_info.ThreadId = GetCurrentThreadId();
            dump_info.ExceptionPointers = info;
            dump_info.ClientPointers = FALSE;
            MiniDumpWriteDump(process, GetCurrentProcessId(), file, MiniDumpNormal,
                &dump_info, nullptr, nullptr);
            CloseHandle(file);
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void gtk_log_to_file(const gchar* log_domain, GLogLevelFlags level, const gchar* message,
                     gpointer) {
    (void)level;
    std::string line = std::string(log_domain ? log_domain : "gtk") + ": " + message;
    append_line(diagnostics_dir() + G_DIR_SEPARATOR_S "kiwi.log", line);
}

}  // namespace

void install_crash_handler() {
    SetUnhandledExceptionFilter(kiwi_unhandled_exception);
}

void install_gtk_log_file() {
    // Fatal + critical only: warnings are too noisy for a user machine.
    g_log_set_handler(nullptr, static_cast<GLogLevelFlags>(G_LOG_LEVEL_ERROR
            | G_LOG_LEVEL_CRITICAL | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION),
        gtk_log_to_file, nullptr);
    g_log_set_handler("Gtk", static_cast<GLogLevelFlags>(G_LOG_LEVEL_ERROR
            | G_LOG_LEVEL_CRITICAL | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION),
        gtk_log_to_file, nullptr);
    g_log_set_handler("GLib-GObject", static_cast<GLogLevelFlags>(G_LOG_LEVEL_ERROR
            | G_LOG_LEVEL_CRITICAL | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION),
        gtk_log_to_file, nullptr);
}

#else

namespace {

void gtk_log_to_file(const gchar* log_domain, GLogLevelFlags level, const gchar* message,
                     gpointer) {
    (void)level;
    std::string line = std::string(log_domain ? log_domain : "gtk") + ": " + message;
    append_line(diagnostics_dir() + G_DIR_SEPARATOR_S "kiwi.log", line);
}

}  // namespace

void install_crash_handler() {}

void install_gtk_log_file() {
    g_log_set_handler(nullptr, static_cast<GLogLevelFlags>(G_LOG_LEVEL_ERROR
            | G_LOG_LEVEL_CRITICAL | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION),
        gtk_log_to_file, nullptr);
}

#endif

}  // namespace kiwi
