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
 */

#pragma once

#include <string>

#if defined(_WIN32)
#  define NOMINMAX
#  include <windows.h>
#else
#  include <csignal>
#  include <setjmp.h>
#endif

namespace orc {
namespace core_internal {

// ---------------------------------------------------------------------------
// POSIX implementation
// ---------------------------------------------------------------------------
#ifndef _WIN32

namespace {

thread_local sigjmp_buf  plugin_fault_jmpbuf;
thread_local bool        plugin_fault_active = false;

inline void plugin_fault_signal_handler(int sig)
{
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

} // anonymous namespace

template <typename Fn>
bool plugin_safe_call(Fn&& fn, std::string& error)
{
    struct sigaction old_segv, old_bus, old_ill;
    struct sigaction sa{};
    sa.sa_handler = plugin_fault_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGSEGV, &sa, &old_segv);
    sigaction(SIGBUS,  &sa, &old_bus);
    sigaction(SIGILL,  &sa, &old_ill);

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
    sigaction(SIGBUS,  &old_bus,  nullptr);
    sigaction(SIGILL,  &old_ill,  nullptr);
    return ok;
}

// ---------------------------------------------------------------------------
// Windows SEH implementation
// ---------------------------------------------------------------------------
#else // _WIN32

template <typename Fn>
bool plugin_safe_call(Fn&& fn, std::string& error)
{
    __try {
        fn();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        error = "Plugin raised an access violation";
        return false;
    }
}

#endif // _WIN32

} // namespace core_internal
} // namespace orc
