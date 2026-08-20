// Tests for Common/CustomMutex.{h,cpp} -- the std::shared_mutex-backed Linux
// implementation swapped in behind #ifdef _WIN32 for the original SRWLOCK one.
// Passing once proves little for concurrency code; these are re-run with
// `ctest --repeat until-fail:50` (see Doc/linux-build.md) and, separately, under
// ThreadSanitizer by hand -- see the M1 plan's verification section.

#include "CustomMutex.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

TEST(CustomMutex, ConcurrentReadersOverlap) {
    // Proves lockRead is shared, not exclusive: N readers must be able to be
    // inside their critical section at the same time.
    constexpr int kReaders = 8;
    CustomMutex m;
    std::atomic<int> inside{0};
    std::atomic<int> maxObserved{0};
    std::atomic<bool> start{false};

    std::vector<std::thread> readers;
    for (int i = 0; i < kReaders; ++i) {
        readers.emplace_back([&] {
            while (!start.load()) {
                std::this_thread::yield();
            }
            m.lockRead();
            const int now = ++inside;
            int prevMax = maxObserved.load();
            while (now > prevMax && !maxObserved.compare_exchange_weak(prevMax, now)) {
            }
            std::this_thread::sleep_for(20ms);
            --inside;
            m.unlockRead();
        });
    }
    start = true;
    for (auto& t : readers) t.join();

    EXPECT_GT(maxObserved.load(), 1) << "readers never overlapped -- lockRead is "
                                         "behaving as exclusive, not shared";
}

TEST(CustomMutex, WriterExcludesReadersAndOtherWriters) {
    // Readers overlapping readers is fine (and expected, per the test above) --
    // what must never happen is a writer overlapping *anyone*. Track reader and
    // writer presence separately so the assertion only fires on a real
    // exclusivity violation, not on the readers doing their normal job.
    CustomMutex m;
    std::atomic<int> readerCount{0};
    std::atomic<bool> writerActive{false};
    std::atomic<bool> violated{false};

    auto reader = [&] {
        for (int i = 0; i < 20; ++i) {
            m.lockRead();
            if (writerActive.load()) violated = true;
            ++readerCount;
            std::this_thread::sleep_for(1ms);
            --readerCount;
            m.unlockRead();
        }
    };
    auto writer = [&] {
        for (int i = 0; i < 20; ++i) {
            m.lockWrite();
            if (writerActive.exchange(true) || readerCount.load() != 0) {
                violated = true;
            }
            std::this_thread::sleep_for(1ms);
            writerActive = false;
            m.unlockWrite();
        }
    };

    std::thread w1(writer), w2(writer), r1(reader), r2(reader);
    w1.join();
    w2.join();
    r1.join();
    r2.join();

    EXPECT_FALSE(violated) << "a writer overlapped with another holder";
}

}  // namespace
