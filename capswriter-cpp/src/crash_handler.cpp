#include "crash_handler.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <sstream>
#include <string>

#include "log.h"
#include "util.h"

namespace capswriter {

namespace {

// Captures a raw stack trace via the kernel32 CaptureStackBackTrace API
// (no DbgHelp / PDB dependency). Addresses are logged as-is; when a crash is
// reproduced the offsets can be mapped back to functions by rebuilding with
// debug info or by the minidump.
std::string CaptureStack() {
  void* frames[64];
  const int n =
      static_cast<int>(CaptureStackBackTrace(0, 64, frames, nullptr));
  std::ostringstream os;
  for (int i = 1; i < n; ++i) {  // skip frame 0 (this function)
    char buf[32];
    std::snprintf(buf, sizeof(buf), "  #%-2d %p\n", i, frames[i]);
    os << buf;
  }
  return os.str();
}

void LogCrash(const char* kind, unsigned long code, void* address) {
  LOG_ERROR("FATAL %s (code=0x%08lX at %p)", kind, code, address);
  const std::string stack = CaptureStack();
  if (!stack.empty()) LOG_ERROR("Stack:\n%s", stack.c_str());

  // Mirror to a standalone crash log next to the exe so the diagnostic survives
  // even when launched by double-click (no console, no --log-file): stderr may
  // go nowhere in that case.
  wchar_t exe_path[MAX_PATH] = {};
  std::string crash_file;
  if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) > 0) {
    const std::wstring wide(exe_path);
    const size_t pos = wide.find_last_of(L"\\/");
    crash_file = WideToUtf8(pos == std::wstring::npos ? wide : wide.substr(0, pos + 1));
    crash_file += "capswriter-crash.log";
  } else {
    crash_file = "capswriter-crash.log";
  }
  FILE* f = nullptr;
  if (_wfopen_s(&f, Utf8ToWide(crash_file).c_str(), L"a, ccs=UTF-8") == 0 && f) {
    const std::string ts = [] {
      char buf[32];
      const time_t t = time(nullptr);
      struct tm tm_buf {};
      localtime_s(&tm_buf, &t);
      std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
      return std::string(buf);
    }();
    fprintf(f, "[%s] FATAL %s (code=0x%08lX at %p)\n", ts.c_str(), kind, code,
            address);
    if (!stack.empty()) fprintf(f, "%s", stack.c_str());
    fflush(f);
    fclose(f);
  }
}

void __cdecl TerminateHandler() {
  LogCrash("std::terminate (uncaught C++ exception)", 0, nullptr);
}

LONG WINAPI ExceptionFilter(EXCEPTION_POINTERS* ep) {
  LogCrash("unhandled SEH exception",
           ep ? ep->ExceptionRecord->ExceptionCode : 0,
           ep ? ep->ExceptionRecord->ExceptionAddress : nullptr);
  return EXCEPTION_EXECUTE_HANDLER;  // let the process terminate / WER take over
}

// CRT invalid-parameter handler: log and return instead of invoking Watson
// (which raises STATUS_FAIL_FAST_EXCEPTION and kills the process).
void __cdecl InvalidParameterHandler(const wchar_t* /*expression*/,
                                      const wchar_t* /*function*/,
                                      const wchar_t* /*file*/,
                                      unsigned int /*line*/,
                                      uintptr_t /*reserved*/) {
  LogCrash("CRT invalid parameter", 0, nullptr);
}

}  // namespace

void InstallCrashHandlers() {
  std::set_terminate(TerminateHandler);
  SetUnhandledExceptionFilter(ExceptionFilter);
  _set_invalid_parameter_handler(InvalidParameterHandler);
  // Suppress the modal abort dialog but keep WER's report-fault behaviour so a
  // dump is still written for offline analysis; our handlers above have logged.
  _set_abort_behavior(_CALL_REPORTFAULT, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
}

}  // namespace capswriter
