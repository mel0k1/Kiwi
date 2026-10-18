#include "diagnostics.hpp"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace kiwi {

namespace {

#ifdef _WIN32

std::string env_utf8(const wchar_t* name) {
    wchar_t buffer[MAX_PATH * 2];
    DWORD size = GetEnvironmentVariableW(name, buffer, MAX_PATH * 2);
    if (size == 0 || size > MAX_PATH * 2) return std::string();
    int utf8 = WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(size),
        nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(utf8), '\0');
    if (utf8 > 0) {
        WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(size),
            out.data(), utf8, nullptr, nullptr);
        while (!out.empty() && out.back() == '\0') out.pop_back();
    }
    return out;
}

std::string base_config_dir() {
    std::string base = env_utf8(L"LOCALAPPDATA");
    if (base.empty()) {
        base = env_utf8(L"USERPROFILE");
        if (!base.empty()) base += "\\AppData\\Local";
    }
    if (base.empty()) base = ".";
    return base + "\\kiwi";
}

std::wstring to_wide_path(const std::string& path) {
    int size = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()),
        nullptr, 0);
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()),
        wide.data(), size);
    return wide;
}

#else

std::string base_config_dir() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    std::string base = xdg && *xdg ? std::string(xdg) : std::string();
    if (base.empty()) {
        const char* home = std::getenv("HOME");
        base = home && *home ? std::string(home) + "/.config" : std::string(".");
    }
    return base + "/kiwi";
}

#endif

void append_line(const std::string& path, const std::string& line) {
    FILE* file = std::fopen(path.c_str(), "ab");
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

std::string diagnostics_dir() {
    static std::string cached;
    if (!cached.empty()) return cached;
    std::string path = base_config_dir();
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    cached = path;
    return cached;
}

void trace_stage(const std::string& message) {
    append_line(diagnostics_dir() + "/startup.log", message);
}

#ifdef _WIN32

void fatal_dialog(const std::string& title, const std::string& detail) {
    append_line(diagnostics_dir() + "/startup.log", "FATAL " + title + ": " + detail);
    MessageBoxW(nullptr, to_wide_path(detail).c_str(), to_wide_path(title).c_str(),
                MB_OK | MB_ICONERROR);
}

#else

void fatal_dialog(const std::string& title, const std::string& detail) {
    append_line(diagnostics_dir() + "/startup.log", "FATAL " + title + ": " + detail);
    std::fprintf(stderr, "kiwi: %s: %s\n", title.c_str(), detail.c_str());
}

#endif

#ifdef _WIN32

namespace {

std::string module_name_for(void* address, std::uintptr_t* base_out) {
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
    if (base_out) *base_out = reinterpret_cast<std::uintptr_t>(module);
    return slash == std::wstring::npos ? "?" : std::string(wide.begin() + slash + 1, wide.end());
}

LONG WINAPI kiwi_unhandled_exception(EXCEPTION_POINTERS* info) {
    void* frames[32];
    unsigned short count = RtlCaptureStackBackTrace(0, 32, frames, nullptr);
    std::string line = "pid "
        + std::to_string(GetCurrentProcessId())
        + " fatal exception 0x"
        + std::to_string(info && info->ExceptionRecord
                ? static_cast<unsigned long long>(info->ExceptionRecord->ExceptionCode)
                : 0ull)
        + " at 0x"
        + std::to_string(reinterpret_cast<unsigned long long>(
            info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress : nullptr));
    for (unsigned short i = 0; i < count && i < 32; ++i) {
        std::uintptr_t base = 0;
        std::string module = module_name_for(frames[i], &base);
        char frame[96];
        // Print the offset from the module base so the frame can be matched
        // against that exact binary (sym + base+offset).
        std::snprintf(frame, sizeof(frame), " | [%u] %s+%llx",
            static_cast<unsigned>(i), module.c_str(),
            static_cast<unsigned long long>(
                reinterpret_cast<std::uintptr_t>(frames[i]) - base));
        line += frame;
    }
    std::string dir = diagnostics_dir();
    append_line(dir + "/crash.log", line);

    if (info) {
        // dbghelp is loaded dynamically: kiwi.dll is loaded in every process
        // (including sandboxed subprocesses via the bootstrap loader) and the
        // static import would drag dbghelp into the sandbox for nothing — it
        // is only ever used here, in the browser process crash path. The
        // handful of dbghelp types are declared locally so <dbghelp.h> (and
        // with it the static import) is not needed at all.
        using MiniDumpType = int;  // MINIDUMP_TYPE, MiniDumpNormal == 0
        struct MiniDumpExceptionInformation {
            DWORD ThreadId;
            EXCEPTION_POINTERS* ExceptionPointers;
            BOOL ClientPointers;
        };
        using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE,
            MiniDumpType, MiniDumpExceptionInformation*, void*, void*);
        HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
        if (dbghelp) {
            auto minidump = reinterpret_cast<MiniDumpWriteDumpFn>(
                reinterpret_cast<std::uintptr_t>(
                    GetProcAddress(dbghelp, "MiniDumpWriteDump")));
            if (minidump) {
                HANDLE process = GetCurrentProcess();
                HANDLE file = CreateFileW(
                    to_wide_path(dir + "/crash.dmp").c_str(),
                    GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, nullptr);
                if (file != INVALID_HANDLE_VALUE) {
                    MiniDumpExceptionInformation dump_info;
                    dump_info.ThreadId = GetCurrentThreadId();
                    dump_info.ExceptionPointers = info;
                    dump_info.ClientPointers = FALSE;
                    minidump(process, GetCurrentProcessId(), file,
                        0 /* MiniDumpNormal */, &dump_info, nullptr, nullptr);
                    CloseHandle(file);
                }
            }
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

void install_crash_handler() {
    SetUnhandledExceptionFilter(kiwi_unhandled_exception);
}

#else

void install_crash_handler() {}

#endif

}
