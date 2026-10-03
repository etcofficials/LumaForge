#pragma once

#include <string>

namespace lf::app {

// Writes a minidump (crash-YYYYMMDD-HHMMSS.dmp) into `dumpDir` when the
// process dies from an unhandled SEH exception, std::terminate, abort(),
// a pure virtual call or an invalid CRT parameter, and logs where it went.
// Open the dump with cdb/WinDbg next to the matching PDBs.
void installCrashHandler(const std::wstring& dumpDir);

}  // namespace lf::app
