#include "lumaforge/diagnostics/CrashHandler.h"

#include "lumaforge/core/Log.h"
#include "lumaforge/core/StringUtil.h"

#include <windows.h>
#include <dbghelp.h>

#include <atomic>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>

namespace lf::diag {

namespace {
std::wstring g_dir;
std::mutex g_symMutex;
bool g_symInit = false;

void ensureSymbols() {
    if (g_symInit) return;
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
    g_symInit = SymInitializeW(GetCurrentProcess(), nullptr, TRUE) == TRUE;
}

std::string describeAddress(DWORD64 addr) {
    char buffer[sizeof(SYMBOL_INFO) + 512]{};
    auto* sym = reinterpret_cast<SYMBOL_INFO*>(buffer);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 511;
    std::string out;
    HMODULE mod = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(addr), &mod) && mod) {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(mod, path, MAX_PATH);
        std::wstring p(path);
        const auto slash = p.find_last_of(L'\\');
        out += wideToUtf8(slash == std::wstring::npos ? p : p.substr(slash + 1));
        out += std::format("+0x{:X}", static_cast<unsigned long long>(addr - reinterpret_cast<DWORD64>(mod)));
    } else {
        out += std::format("0x{:016X}", static_cast<unsigned long long>(addr));
    }
    DWORD64 disp = 0;
    if (SymFromAddr(GetCurrentProcess(), addr, &disp, sym)) {
        out += std::format("  {}+0x{:X}", sym->Name, static_cast<unsigned long long>(disp));
        IMAGEHLP_LINEW64 line{};
        line.SizeOfStruct = sizeof line;
        DWORD lineDisp = 0;
        if (SymGetLineFromAddrW64(GetCurrentProcess(), addr, &lineDisp, &line)) {
            std::wstring f(line.FileName);
            const auto slash = f.find_last_of(L'\\');
            out += std::format("  ({}:{})", wideToUtf8(slash == std::wstring::npos ? f : f.substr(slash + 1)), line.LineNumber);
        }
    }
    return out;
}

std::string walkStack(CONTEXT* ctx, int skip) {
    std::lock_guard lock(g_symMutex);
    ensureSymbols();
    std::string out;
    if (ctx) {
        STACKFRAME64 frame{};
        frame.AddrPC.Offset = ctx->Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = ctx->Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = ctx->Rsp;
        frame.AddrStack.Mode = AddrModeFlat;
        CONTEXT copy = *ctx;
        for (int i = 0; i < 64; ++i) {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), GetCurrentThread(), &frame, &copy, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) break;
            if (frame.AddrPC.Offset == 0) break;
            if (i >= skip) out += std::format("  #{:<2} {}\n", i - skip, describeAddress(frame.AddrPC.Offset));
        }
    } else {
        void* frames[64];
        const USHORT n = CaptureStackBackTrace(static_cast<DWORD>(skip + 1), 64, frames, nullptr);
        for (USHORT i = 0; i < n; ++i) out += std::format("  #{:<2} {}\n", i, describeAddress(reinterpret_cast<DWORD64>(frames[i])));
    }
    return out;
}

const char* exceptionName(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
        case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
        case EXCEPTION_BREAKPOINT: return "BREAKPOINT";
        case 0xE06D7363: return "C++ exception";
        case 0xC0000409: return "STACK_BUFFER_OVERRUN / fail-fast";
        default: return "exception";
    }
}

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* info) {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    wchar_t stamp[32];
    wcsftime(stamp, 32, L"%Y%m%d-%H%M%S", &tm);
    const std::wstring base = g_dir + L"\\crash-" + stamp;

    // Minidump first: it is the most robust artefact if symbolisation itself fails.
    HANDLE file = CreateFileW((base + L".dmp").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), info, FALSE};
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo), &mei, nullptr, nullptr);
        CloseHandle(file);
    }

    const DWORD code = info->ExceptionRecord->ExceptionCode;
    std::string report = std::format("LumaForge crash report\nexception: {} (0x{:08X}) at {}\nthread: {}\n",
                                     exceptionName(code), static_cast<unsigned long>(code),
                                     describeAddress(reinterpret_cast<DWORD64>(info->ExceptionRecord->ExceptionAddress)), GetCurrentThreadId());
    if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2) {
        report += std::format("access: {} address 0x{:016X}\n", info->ExceptionRecord->ExceptionInformation[0] == 0 ? "read" : (info->ExceptionRecord->ExceptionInformation[0] == 1 ? "write" : "execute"),
                              static_cast<unsigned long long>(info->ExceptionRecord->ExceptionInformation[1]));
    }
    report += "stack:\n" + walkStack(info->ContextRecord, 0);

    FILE* f = nullptr;
    if (_wfopen_s(&f, (base + L".txt").c_str(), L"w") == 0 && f) {
        std::fputs(report.c_str(), f);
        std::fclose(f);
    }
    LF_LOG_ERROR("crash", "{}", report);
    LF_LOG_ERROR("crash", "report written to {}", wideToUtf8(base));
    lf::log::Logger::instance().flush();
    return EXCEPTION_EXECUTE_HANDLER;
}
}  // namespace

namespace {
// First-chance view of fatal exceptions: fires even when the crash happens
// during process teardown, where the top-level filter may not run.
LONG WINAPI onVectoredException(EXCEPTION_POINTERS* info) {
    static std::atomic<bool> reported{false};
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    const bool fatal = code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_STACK_OVERFLOW ||
                       code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == 0xC0000409;
    if (fatal && !reported.exchange(true)) onUnhandledException(info);
    return EXCEPTION_CONTINUE_SEARCH;
}
}  // namespace

void CrashHandler::install(const std::wstring& directory) {
    g_dir = directory;
    CreateDirectoryW(directory.c_str(), nullptr);
    SetUnhandledExceptionFilter(&onUnhandledException);
    AddVectoredExceptionHandler(1, &onVectoredException);
    // Keep exceptions coming to our filter even if a DLL tries to swallow them.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
}

std::string CrashHandler::currentStackTrace(int skipFrames) { return walkStack(nullptr, skipFrames + 1); }

}  // namespace lf::diag
