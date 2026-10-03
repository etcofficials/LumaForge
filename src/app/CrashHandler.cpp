#include "CrashHandler.h"

#include "lumaforge/core/Log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>

namespace lf::app {

namespace {

wchar_t g_dir[MAX_PATH] = {};
std::atomic<bool> g_written{false};

void writeDump(EXCEPTION_POINTERS* info, const char* reason) {
    if (g_written.exchange(true)) return;   // first fault wins; nested faults must not recurse
    SYSTEMTIME t{};
    GetLocalTime(&t);
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\crash-%04u%02u%02u-%02u%02u%02u.dmp", g_dir, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    bool ok = false;
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = info;
        mei.ClientPointers = FALSE;
        const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
        ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, info ? &mei : nullptr, nullptr, nullptr) != FALSE;
        CloseHandle(file);
    }
    char narrow[MAX_PATH * 3] = {};
    WideCharToMultiByte(CP_UTF8, 0, path, -1, narrow, sizeof narrow, nullptr, nullptr);
    const unsigned code = info && info->ExceptionRecord ? static_cast<unsigned>(info->ExceptionRecord->ExceptionCode) : 0u;
    LF_LOG_ERROR("crash", "fatal: {} (code 0x{:08X}); minidump {}: {}", reason, code, ok ? "written" : "FAILED", narrow);
    log::Logger::instance().flush();
}

LONG WINAPI onUnhandled(EXCEPTION_POINTERS* info) {
    writeDump(info, "unhandled exception");
    return EXCEPTION_CONTINUE_SEARCH;   // let Windows Error Reporting finish the process
}

// For non-SEH deaths, raise a real exception so the dump has a stack.
void dumpHere(const char* reason) {
    __try {
        RaiseException(0xE0000001, 0, 0, nullptr);
    } __except (writeDump(GetExceptionInformation(), reason), EXCEPTION_EXECUTE_HANDLER) {
    }
}

void onTerminate() {
    dumpHere("std::terminate");
    std::abort();
}

void onAbort(int) { dumpHere("abort()"); }
void onPureCall() { dumpHere("pure virtual call"); std::abort(); }
void onInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) { dumpHere("invalid CRT parameter"); std::abort(); }

}  // namespace

void installCrashHandler(const std::wstring& dumpDir) {
    wcsncpy_s(g_dir, dumpDir.c_str(), _TRUNCATE);
    CreateDirectoryW(g_dir, nullptr);
    SetUnhandledExceptionFilter(onUnhandled);
    std::set_terminate(onTerminate);
    std::signal(SIGABRT, onAbort);
    _set_purecall_handler(onPureCall);
    _set_invalid_parameter_handler(onInvalidParameter);
    // Keep the CRT from showing its own abort dialog over ours.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
}

}  // namespace lf::app
