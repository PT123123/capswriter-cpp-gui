#pragma once

#include <string>

namespace capswriter {

// Installs process-wide crash handlers that turn a previously-silent crash
// (uncaught C++ exception -> std::terminate -> abort, or a CRT invalid
// parameter / fast-fail) into a log line with the exception code, faulting
// address and a stack trace. Without this, a throw on a worker thread kills
// the process with no diagnostic (WER only writes a minidump, not a readable
// log). Call once, early, from each frontend's main().
//
// Note: STATUS_FAIL_FAST_EXCEPTION raised via RaiseFailFastException bypasses
// SetUnhandledExceptionFilter, so a CRT invalid-parameter handler is also
// installed: it logs and returns, which lets the offending CRT call fail
// gracefully (e.g. fopen("") -> NULL) instead of crashing the process.
void InstallCrashHandlers();

}  // namespace capswriter
