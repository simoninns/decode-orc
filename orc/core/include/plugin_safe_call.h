/*
 * File:        plugin_safe_call.h
 * Module:      orc-core
 * Purpose:     Fault-isolation wrapper for calls into untrusted plugin code
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 *
 * Provides plugin_safe_call<Fn>(fn, error) which executes fn() and returns
 * true on success.  If fn() triggers SIGSEGV, SIGBUS, or SIGILL (POSIX) or
 * an access-violation exception (Windows), the signal/exception is caught,
 * "error" is populated with a description, and false is returned.  The host
 * process continues normally.
 *
 * IMPORTANT: the body of fn() must not hold C++ destructors that touch
 * heap/STL objects in a way that would be unsafe to abandon mid-flight.
 * In practice: only call raw C function pointers inside fn().
 *
 * Windows has two implementations below:
 *   - MSVC (and any compiler implementing SEH, e.g. clang-cl): __try/__except.
 *     This is a compiler-language extension, not a Win32 API, and gcc/MinGW
 *     does not implement it.
 *   - MinGW/gcc: AddVectoredExceptionHandler + setjmp/longjmp. This is a
 *     plain Win32 API (declared in <windows.h>, exported by kernel32), so it
 *     works identically under any compiler; we use it specifically where SEH
 *     keywords are unavailable. Guarded on _MSC_VER rather than _WIN32 so the
 *     right branch is picked regardless of which Windows compiler is in use.
 */

#pragma once

#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <csetjmp>
#else
#include <setjmp.h>

#include <csignal>
#endif

namespace orc {
namespace core_internal {

// ---------------------------------------------------------------------------
// POSIX implementation
// ---------------------------------------------------------------------------
#ifndef _WIN32

// inline thread_local gives one per-thread instance that is ODR-safe across
// all translation units that include this header (C++17 inline variables).
inline thread_local sigjmp_buf plugin_fault_jmpbuf;
inline thread_local bool plugin_fault_active = false;

inline void plugin_fault_signal_handler(int sig) {
  if (plugin_fault_active) {
    siglongjmp(plugin_fault_jmpbuf, 1);
  }
  // Not our guard — restore the default disposition and re-raise.
  // Use sigaction here because signal() is not async-signal-safe on POSIX
  // and calling it inside a signal handler is undefined behaviour when
  // another thread is inside sigaction() concurrently.
  struct sigaction sa{};
  sa.sa_handler = SIG_DFL;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(sig, &sa, nullptr);
  raise(sig);
}

template <typename Fn>
bool plugin_safe_call(Fn&& fn, std::string& error) {
  struct sigaction old_segv, old_bus, old_ill;
  struct sigaction sa{};
  sa.sa_handler = plugin_fault_signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGSEGV, &sa, &old_segv);
  sigaction(SIGBUS, &sa, &old_bus);
  sigaction(SIGILL, &sa, &old_ill);

  plugin_fault_active = true;
  bool ok = true;
  if (sigsetjmp(plugin_fault_jmpbuf, 1) == 0) {
    fn();
  } else {
    error = "Plugin raised a fatal signal (SIGSEGV/SIGBUS/SIGILL)";
    ok = false;
  }
  plugin_fault_active = false;

  sigaction(SIGSEGV, &old_segv, nullptr);
  sigaction(SIGBUS, &old_bus, nullptr);
  sigaction(SIGILL, &old_ill, nullptr);
  return ok;
}

// ---------------------------------------------------------------------------
// Windows: MSVC / SEH-capable compilers
// ---------------------------------------------------------------------------
#elif defined(_MSC_VER)

template <typename Fn>
bool plugin_safe_call(Fn&& fn, std::string& error) {
  __try {
    fn();
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    error = "Plugin raised an access violation";
    return false;
  }
}

// ---------------------------------------------------------------------------
// Windows: MinGW/gcc (no SEH language extension)
// ---------------------------------------------------------------------------
// AddVectoredExceptionHandler is a plain Win32 API (not a compiler
// extension), so it is available under MinGW. We register a process-wide
// vectored handler once, guard it with a thread-local "are we inside a
// guarded call" flag (mirroring the POSIX signal-handler pattern above), and
// longjmp back out of the faulting context on a matching hardware exception.
#else

inline thread_local jmp_buf plugin_fault_jmpbuf;
inline thread_local bool plugin_fault_active = false;

inline LONG CALLBACK plugin_fault_vectored_handler(EXCEPTION_POINTERS* info) {
  if (!plugin_fault_active) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  switch (info->ExceptionRecord->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_STACK_OVERFLOW:
      longjmp(plugin_fault_jmpbuf, 1);
      // unreachable
    default:
      return EXCEPTION_CONTINUE_SEARCH;
  }
}

template <typename Fn>
bool plugin_safe_call(Fn&& fn, std::string& error) {
  // The vectored handler is process-global once registered; install it
  // lazily on first use and leave it installed. plugin_fault_active (checked
  // first in the handler above) makes it a no-op for any fault that occurs
  // outside a guarded call, exactly like the POSIX sigaction handler does.
  static PVOID registration =
      AddVectoredExceptionHandler(/*first=*/1, plugin_fault_vectored_handler);
  (void)registration;

  plugin_fault_active = true;
  bool ok = true;
  if (setjmp(plugin_fault_jmpbuf) == 0) {
    fn();
  } else {
    error = "Plugin raised an access violation";
    ok = false;
  }
  plugin_fault_active = false;
  return ok;
}

#endif  // _WIN32 / _MSC_VER

}  // namespace core_internal
}  // namespace orc