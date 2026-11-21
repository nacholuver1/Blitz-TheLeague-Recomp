// blitz - ReXGlue Recompiled Project
//
// Last-chance minidump writer.
//
// ReXGlue's crash reporter logs through the game logger and falls back to
// stderr if it can't take the logger within 200 ms, so a crash on a thread
// that is busy logging can leave no trace at all. This filter is installed in
// front of it (chained, so the SDK report still runs) and always writes a
// minidump to a "crashes" folder next to the exe, which can be opened with
// lldb/WinDbg together with the matching blitz.pdb.

#pragma once

#ifdef _WIN32
#include <Windows.h>
#include <DbgHelp.h>

#include <cstdio>

#pragma comment(lib, "dbghelp.lib")

namespace blitz {

inline LPTOP_LEVEL_EXCEPTION_FILTER g_prev_crash_filter = nullptr;
inline volatile LONG g_crash_dumping = 0;

inline LONG WINAPI CrashDumpFilter(EXCEPTION_POINTERS* info) {
  if (InterlockedExchange(&g_crash_dumping, 1) == 0) {
    // <exe folder>\crashes\ (fixed-size buffers only: we may be out of stack/heap).
    wchar_t dir[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, dir, MAX_PATH);
    while (len > 0 && dir[len - 1] != L'\\') --len;
    dir[len] = L'\0';
    wcscat_s(dir, L"crashes");
    CreateDirectoryW(dir, nullptr);
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\blitz_%04u%02u%02u_%02u%02u%02u_%08X.dmp", dir, t.wYear, t.wMonth,
               t.wDay, t.wHour, t.wMinute, t.wSecond,
               static_cast<unsigned>(info->ExceptionRecord->ExceptionCode));
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file != INVALID_HANDLE_VALUE) {
      MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), info, FALSE};
      // Thread stacks + data segments + handle info; not full guest memory (that is GBs).
      const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithHandleData |
                                                   MiniDumpWithThreadInfo |
                                                   MiniDumpWithIndirectlyReferencedMemory);
      MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, &mei, nullptr,
                        nullptr);
      CloseHandle(file);
    }
  }
  return g_prev_crash_filter ? g_prev_crash_filter(info) : EXCEPTION_CONTINUE_SEARCH;
}

// Call after the SDK has installed its crash reporter (i.e. from OnInit).
inline void InstallCrashDump() { g_prev_crash_filter = SetUnhandledExceptionFilter(CrashDumpFilter); }

inline void RemoveCrashDump() {
  SetUnhandledExceptionFilter(g_prev_crash_filter);
  g_prev_crash_filter = nullptr;
}

}  // namespace blitz

#else

namespace blitz {
inline void InstallCrashDump() {}
inline void RemoveCrashDump() {}
}  // namespace blitz

#endif
