// Tests for Common/ThreadHelper.{h,cpp} -- the std::thread + condition_variable
// Linux implementation swapped in behind #ifdef _WIN32. Public API (constructor
// signature, public members, Stop()/Start()) is unchanged from the Windows
// version; two behavioral quirks are preserved deliberately (see
// Doc/linux_roadmap/03-platform-abstraction.md) and asserted here rather than
// left to accident: the do/while first-tick, and the manual-reset-never-reset
// event that makes a Start() after a Stop() exit its wait immediately.
//
// Passing once proves little for concurrency code; re-run with
// `ctest --repeat until-fail:50` (see Doc/linux-build.md) and, separately, under
// Address/ThreadSanitizer by hand -- see the M1 plan's verification section.

#include "ThreadHelper.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace {

using namespace std::chrono_literals;

void IncrementCounter(LPVOID param) {
    auto* counter = static_cast<std::atomic<int>*>(param);
    ++(*counter);
}

TEST(ThreadHelper, CallbackFiresImmediatelyOnConstruction) {
    // do/while in the worker loop: the first call happens before the first
    // `delay`-ms wait, not after. A long delay makes this unambiguous -- if the
    // implementation waited first, the counter would still be 0 here.
    std::atomic<int> counter{0};
    ThreadHelper helper(reinterpret_cast<LPVOID>(&IncrementCounter), &counter,
                         /*delay=*/10000);
    std::this_thread::sleep_for(50ms);
    EXPECT_GE(counter.load(), 1);
    helper.Stop();
}

TEST(ThreadHelper, TickCountRoughlyMatchesInterval) {
    constexpr int kDelayMs = 20;
    std::atomic<int> counter{0};
    ThreadHelper helper(reinterpret_cast<LPVOID>(&IncrementCounter), &counter,
                         kDelayMs);
    std::this_thread::sleep_for(std::chrono::milliseconds(kDelayMs * 10));
    helper.Stop();

    // Expect roughly 10 ticks (1 immediate + ~9 more over 10 intervals). Wide
    // tolerance -- this is a scheduling-sensitive test, not a precise timer
    // test; it only needs to catch "never ticks again" or "busy-loops".
    EXPECT_GE(counter.load(), 5);
    EXPECT_LE(counter.load(), 20);
}

TEST(ThreadHelper, StopJoinsAndCallbackCannotRunAfterItReturns) {
    std::atomic<int> counter{0};
    ThreadHelper helper(reinterpret_cast<LPVOID>(&IncrementCounter), &counter,
                         /*delay=*/5);
    std::this_thread::sleep_for(30ms);
    helper.Stop();

    const int afterStop = counter.load();
    std::this_thread::sleep_for(50ms);  // long enough for several more ticks,
                                          // if the worker were still running
    EXPECT_EQ(counter.load(), afterStop)
        << "callback ran after Stop() returned -- Stop() did not actually join";
}

TEST(ThreadHelper, StopIsIdempotent) {
    std::atomic<int> counter{0};
    ThreadHelper helper(reinterpret_cast<LPVOID>(&IncrementCounter), &counter, 10);
    helper.Stop();
    helper.Stop();  // must not double-join / crash
    SUCCEED();
}

TEST(ThreadHelper, DestructorStopsARunningHelper) {
    std::atomic<int> counter{0};
    {
        ThreadHelper helper(reinterpret_cast<LPVOID>(&IncrementCounter), &counter, 5);
        std::this_thread::sleep_for(20ms);
    }  // destructor runs here
    const int afterDestruction = counter.load();
    std::this_thread::sleep_for(30ms);
    EXPECT_EQ(counter.load(), afterDestruction)
        << "callback ran after the ThreadHelper was destroyed";
}

TEST(ThreadHelper, RestartAfterStopReproducesManualResetEventQuirk) {
    // Deliberate, documented quirk: CreateEvent(NULL, true, false, NULL) is
    // manual-reset and this class never calls ResetEvent. So once Stop() has
    // signaled the event, a later Start() creates a new worker whose first
    // do/while wait sees an already-signaled event and exits immediately --
    // exactly one tick fires, not a steady stream. This is asserted as
    // intentional (matching Windows' actual behavior), not "fixed" here.
    std::atomic<int> counter{0};
    ThreadHelper helper(reinterpret_cast<LPVOID>(&IncrementCounter), &counter, 10000);
    std::this_thread::sleep_for(20ms);
    helper.Stop();
    const int afterFirstRun = counter.load();
    ASSERT_GE(afterFirstRun, 1);

    helper.Start();
    std::this_thread::sleep_for(50ms);
    // Exactly one more tick (the immediate do/while one), then the worker exits
    // on its very first wait -- it must NOT still be ticking 50ms later.
    EXPECT_EQ(counter.load(), afterFirstRun + 1);
    helper.Stop();
}

}  // namespace
