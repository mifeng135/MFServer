#ifndef MFSpinLock_hpp
#define MFSpinLock_hpp

#include <atomic>
#include <thread>

#include "MFMacro.h"

inline void asm_volatile_pause() {
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
  ::_mm_pause();
#elif defined(__i386__) || FOLLY_X64 || \
    (defined(__mips_isa_rev) && __mips_isa_rev > 1)
  asm volatile("pause");
#elif defined(__aarch64__)
  asm volatile("isb");
#elif (defined(__arm__) && !(__ARM_ARCH < 7))
  asm volatile("yield");
#elif defined(__powerpc64__)
  asm volatile("or 27,27,27");
#endif
}
//Short critical sections + multithreading will cause contention
class Sleeper {
public:
    const std::chrono::nanoseconds delta;
    uint32_t spinCount;
    static constexpr uint32_t kMaxActiveSpin = 4000;

public:
    static constexpr std::chrono::nanoseconds kMinYieldingSleep = std::chrono::microseconds(500);

    constexpr Sleeper() noexcept
    : delta(kMinYieldingSleep)
    , spinCount(0) {
    }

    explicit Sleeper(std::chrono::nanoseconds d) noexcept
    : delta(d)
    , spinCount(0) {
    }

    void wait() noexcept {
        if (spinCount < kMaxActiveSpin) {
            ++spinCount;
            asm_volatile_pause();
        } else {
            std::this_thread::sleep_for(delta);
        }
    }
};

class MFSpinLock {
public:
    MFSpinLock(const MFSpinLock &) = delete;

    MFSpinLock &operator=(MFSpinLock) = delete;

    MFSpinLock() 
    : m_flag(false) {
    
    }

    void lock() {
        Sleeper sleeper;
        while (m_flag.exchange(true, std::memory_order_acquire)) {
            sleeper.wait();
        }
    }

    void unlock() {
        m_flag.store(false, std::memory_order_release);
    }

private:
    std::atomic<bool> m_flag;
};

#endif /* MFSpinLock_hpp */
