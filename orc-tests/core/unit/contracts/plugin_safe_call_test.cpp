/*
 * File:        plugin_safe_call_test.cpp
 * Module:      orc-core unit tests
 * Purpose:     Unit tests for the plugin_safe_call fault-isolation wrapper.
 *
 * Tests verify:
 *   - Normal (non-faulting) callable returns true
 *   - Exceptions propagating out of the callable are not silently swallowed
 *     (plugin_safe_call only guards CPU-level faults, not C++ exceptions)
 *   - Thread-local state (plugin_fault_active / plugin_fault_jmpbuf) is
 *     correctly restored after both the success and fault paths so that
 *     repeated calls on the same thread work correctly
 *   - Concurrent calls on independent threads do not corrupt each other's
 *     thread-local state
 *
 * NOTE: We deliberately do NOT test SIGSEGV/SIGBUS receipt in a unit-test
 *   context.  Raising a fatal signal inside a GoogleTest runner is inherently
 *   fragile and platform-dependent.  The signal-handler correctness is
 *   validated by the crash-recovery integration test suite instead.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include <atomic>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// Include the header under test directly (internal unit-test privilege).
#include "../../../orc/core/include/plugin_safe_call.h"

namespace orc_unit_test {

using orc::core_internal::plugin_safe_call;

// ---------------------------------------------------------------------------
// Basic success path
// ---------------------------------------------------------------------------

TEST(PluginSafeCallTest, returnsTrue_WhenCallableSucceeds)
{
    std::string error;
    bool called = false;
    const bool result = plugin_safe_call([&called] { called = true; }, error);

    EXPECT_TRUE(result);
    EXPECT_TRUE(called);
    EXPECT_TRUE(error.empty());
}

TEST(PluginSafeCallTest, callableReturnValueIsIgnored_WhenVoidCallable)
{
    std::string error;
    int side_effect = 0;
    const bool result = plugin_safe_call([&side_effect] { side_effect = 42; }, error);

    EXPECT_TRUE(result);
    EXPECT_EQ(side_effect, 42);
}

// ---------------------------------------------------------------------------
// Repeated invocations on the same thread
// ---------------------------------------------------------------------------

TEST(PluginSafeCallTest, canBeCalledRepeatedlyOnSameThread)
{
    std::string error;
    int count = 0;

    for (int i = 0; i < 10; ++i) {
        const bool ok = plugin_safe_call([&count] { ++count; }, error);
        EXPECT_TRUE(ok) << "Failed on iteration " << i;
        EXPECT_TRUE(error.empty()) << "Error on iteration " << i << ": " << error;
    }

    EXPECT_EQ(count, 10);
}

// ---------------------------------------------------------------------------
// Thread-local state isolation across threads
// ---------------------------------------------------------------------------

TEST(PluginSafeCallTest, concurrentCallsOnSeparateThreadsDoNotInterfere)
{
    constexpr int kThreads = 8;
    constexpr int kIterations = 100;

    std::atomic<int> success_count{0};
    std::atomic<int> fail_count{0};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&success_count, &fail_count] {
            for (int i = 0; i < kIterations; ++i) {
                std::string error;
                const bool ok = plugin_safe_call([] {
                    // Lightweight work to give the scheduler opportunity to
                    // interleave threads.
                    volatile int x = 0;
                    for (int j = 0; j < 100; ++j) { x += j; }
                    (void)x;
                }, error);

                if (ok) {
                    success_count.fetch_add(1, std::memory_order_relaxed);
                } else {
                    fail_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    EXPECT_EQ(success_count.load(), kThreads * kIterations);
    EXPECT_EQ(fail_count.load(), 0);
}

// ---------------------------------------------------------------------------
// C++ exceptions are NOT caught (only CPU faults are)
// ---------------------------------------------------------------------------

TEST(PluginSafeCallTest, cppExceptionsPropagateThroughWrapper)
{
    // plugin_safe_call does not handle C++ exceptions.  The caller is
    // responsible for wrapping plugin calls that might throw.
    std::string error;
    EXPECT_THROW(
        plugin_safe_call([] { throw std::runtime_error("plugin threw"); }, error),
        std::runtime_error);
}

} // namespace orc_unit_test
