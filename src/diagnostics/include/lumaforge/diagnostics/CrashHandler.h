#pragma once

#include <string>

namespace lf::diag {

// Installs an unhandled-exception filter that writes, on a crash:
//   <dir>\crash-<timestamp>.dmp   minidump (loadable in WinDbg / Visual Studio)
//   <dir>\crash-<timestamp>.txt   exception code, faulting module+offset and a
//                                 symbolised stack trace (uses the PDBs next
//                                 to the executable when present)
// The report path is also written to the application log.
class CrashHandler {
public:
    static void install(const std::wstring& directory);
    // Writes a symbolised trace of the *current* thread (for diagnostics/tests).
    static std::string currentStackTrace(int skipFrames = 0);
};

}  // namespace lf::diag
