// "LumaForge Launcher.exe": the double-click entry point of the portable
// package. It finds app\LumaForge.exe relative to itself (so the folder can
// live anywhere), starts it with the same arguments, and explains the usual
// failures (missing files, missing DLLs) instead of failing silently.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <string>

namespace {

std::wstring moduleDir() {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    std::wstring path(buf, n);
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring(L".") : path.substr(0, slash);
}

bool fileExists(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// Everything after the launcher's own name on the command line.
std::wstring forwardedArguments() {
    const wchar_t* cmd = GetCommandLineW();
    bool quoted = false;
    while (*cmd && (quoted || (*cmd != L' ' && *cmd != L'\t'))) {
        if (*cmd == L'"') quoted = !quoted;
        ++cmd;
    }
    while (*cmd == L' ' || *cmd == L'\t') ++cmd;
    return cmd;
}

void fail(const std::wstring& text) { MessageBoxW(nullptr, text.c_str(), L"LumaForge", MB_OK | MB_ICONERROR); }

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const std::wstring dir = moduleDir();
    // Portable package: <root>\app\LumaForge.exe. Development: next to the launcher.
    std::wstring appDir = dir + L"\\app";
    std::wstring exe = appDir + L"\\LumaForge.exe";
    if (!fileExists(exe)) {
        appDir = dir;
        exe = dir + L"\\LumaForge.exe";
    }
    if (!fileExists(exe)) {
        fail(L"LumaForge.exe was not found.\n\nExpected it in:\n" + dir + L"\\app\n\n"
             L"Extract the whole LumaForge folder from the ZIP (keep the \"app\" folder next to this launcher) and try again.");
        return 1;
    }

    std::wstring commandLine = L"\"" + exe + L"\"";
    const std::wstring args = forwardedArguments();
    if (!args.empty()) commandLine += L" " + args;

    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, appDir.c_str(), &si, &pi)) {
        fail(L"LumaForge could not be started (error " + std::to_wstring(GetLastError()) + L").\n\n" + exe);
        return 1;
    }
    // Stay only long enough to catch a start-up failure the user would otherwise never see.
    const DWORD waited = WaitForSingleObject(pi.hProcess, 4000);
    DWORD code = STILL_ACTIVE;
    if (waited == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (waited != WAIT_OBJECT_0 || code == 0) return 0;
    if (code == 0xC0000135u || code == 0xC000007Bu) {
        fail(L"LumaForge could not start because a program file (DLL) is missing or damaged.\n\n"
             L"Extract the complete ZIP again into a new folder; do not move files out of the \"app\" folder.");
    } else {
        wchar_t hex[16];
        wsprintfW(hex, L"0x%08X", code);
        fail(std::wstring(L"LumaForge stopped while starting (exit code ") + hex + L").\n\n"
             L"The log is in the LumaForgeData\\Logs folder (on the drive LumaForge runs from).");
    }
    return 1;
}
