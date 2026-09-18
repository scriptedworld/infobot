// Allocation that fails on demand, for the suites and nothing else.
//
// allocation.cpp replaces the global operator new and delete in the test binary
// only; the statusline and forget binaries never link it. Every replacement
// allocates exactly as the default does until a Failure is armed, and then the
// allocation it names fails once: the throwing forms throw std::bad_alloc and
// the nothrow forms return nullptr.
//
// It exists so a test can take the exception edges gcov counts at every call
// that can throw. No other route reaches them: nothing a test hands the code
// makes an allocation fail. Registered as S-3 in SUPPRESSIONS, with the question
// that was asked and the answer given.
#pragma once

#include <cstddef>
#include <functional>

namespace infobot::test {

// Arms the allocation count allocations from now (0 is the next one) to fail,
// on this thread, until it fires or this goes out of scope.
class Failure {
   public:
    explicit Failure(std::size_t count);
    ~Failure();

    Failure(const Failure&) = delete;
    Failure& operator=(const Failure&) = delete;
    Failure(Failure&&) = delete;
    Failure& operator=(Failure&&) = delete;

    // Whether the allocation armed on this thread was reached and failed.
    [[nodiscard]] static bool fired();
};

// What running body once per allocation it makes found.
struct Sweep {
    // Runs in which the failed allocation escaped body as std::bad_alloc.
    std::size_t thrown = 0;
    // Runs in which an allocation failed and body returned anyway, which a
    // nothrow allocation or a caught failure produces.
    std::size_t absorbed = 0;
    // Whether a run finally made every allocation and completed. False means
    // the sweep stopped at its limit first.
    bool completed = false;
};

// Runs body with its first allocation failing, then its second, and so on,
// until a run completes without reaching the armed one. after is called with the
// run's index once each run ends, with nothing armed, so it can assert on what
// the run left behind and reset it.
Sweep fail_each_allocation(const std::function<void()>& body,
                           const std::function<void(std::size_t run)>& after);

}  // namespace infobot::test
