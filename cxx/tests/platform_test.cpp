#include "platform.hpp"

#include <doctest/doctest.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <new>
#include <string>
#include <string_view>
#include <thread>

#include "allocation.hpp"

namespace platform = infobot::platform;
namespace test = infobot::test;

namespace {

void nothing(int /*signal*/) {}

// A pipe whose ends close with it.
class Pipe {
   public:
    Pipe() { REQUIRE(::pipe(ends_.data()) == 0); }
    ~Pipe() {
        close_read();
        close_write();
    }
    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;
    Pipe(Pipe&&) = delete;
    Pipe& operator=(Pipe&&) = delete;

    [[nodiscard]] int read_end() const { return ends_[0]; }

    void put(std::string_view body) const {
        REQUIRE(::write(ends_[1], body.data(), body.size()) ==
                static_cast<ssize_t>(body.size()));
    }
    void close_read() { close_end(0); }
    void close_write() { close_end(1); }

   private:
    void close_end(std::size_t which) {
        if (ends_.at(which) >= 0) {
            ::close(ends_.at(which));
            ends_.at(which) = -1;
        }
    }

    std::array<int, 2> ends_{-1, -1};
};

}  // namespace

// COVERS: FR-1.1 | positive
//
// The payload arrives on a descriptor and is read whole, however it was split
// on the way in.
TEST_CASE("read_all returns everything the descriptor held") {
    Pipe pipe;
    constexpr std::string_view body = R"({"session_id":"s"})";
    pipe.put(body);
    pipe.put(body);
    pipe.close_write();
    CHECK(platform::read_all(pipe.read_end()) == std::string(body) + std::string(body));
}

// COVERS: FR-1.1 | edge
//
// A transcript-sized payload arrives in more reads than one, so what holds it
// grows as it goes.
TEST_CASE("read_all returns a payload larger than one read") {
    constexpr std::size_t big = 100000;
    const std::string body(big, 'x');
    Pipe pipe;
    std::thread writer([&] {
        pipe.put(body);
        pipe.close_write();
    });
    const std::string held = platform::read_all(pipe.read_end());
    writer.join();
    CHECK(held.size() == big);
    CHECK(held == body);
}

// COVERS: FR-1.3 | edge
TEST_CASE("read_all returns nothing for an empty descriptor") {
    Pipe pipe;
    pipe.close_write();
    CHECK(platform::read_all(pipe.read_end()).empty());
}

// COVERS: FR-1.2 | negative
TEST_CASE("read_all returns nothing for a descriptor that cannot be read") {
    CHECK(platform::read_all(-1).empty());
}

// COVERS: FR-1.2 | edge
//
// A payload too large to hold is a throw out of here, which the entry points
// turn into an exit of 0. The suite's allocator fails the allocation that would
// hold it.
// The arming and the call have nothing between them on purpose: a doctest
// assertion macro allocates while it evaluates, so CHECK_THROWS_AS around this
// fails the allocation inside doctest and reports a pass having never reached
// the line under test.
TEST_CASE("read_all throws when what arrives cannot be held") {
    // Past the small-string buffer, so holding it is an allocation to fail.
    constexpr std::size_t past_the_short_string = 64;
    Pipe pipe;
    pipe.put(std::string(past_the_short_string, 'x'));
    pipe.close_write();
    bool threw = false;
    {
        const test::Failure failure(0);
        try {
            static_cast<void>(platform::read_all(pipe.read_end()));
        } catch (const std::bad_alloc&) {
            threw = true;
        }
    }
    CHECK(threw);
}

// COVERS: FR-1.1 | edge
//
// A signal interrupting the read is not the end of the input.
TEST_CASE("read_all carries on through an interrupted read") {
    constexpr std::chrono::milliseconds pause{20};
    struct sigaction action{};
    action.sa_handler = nothing;
    struct sigaction previous{};
    REQUIRE(::sigaction(SIGUSR1, &action, &previous) == 0);

    Pipe pipe;
    std::atomic<bool> written{false};
    const pthread_t reader = ::pthread_self();
    std::thread interrupter([&] {
        std::this_thread::sleep_for(pause);
        ::pthread_kill(reader, SIGUSR1);
        std::this_thread::sleep_for(pause);
        pipe.put("after the signal");
        written = true;
        pipe.close_write();
    });
    const std::string held = platform::read_all(pipe.read_end());
    interrupter.join();
    CHECK(written);
    CHECK(held == "after the signal");
    ::sigaction(SIGUSR1, &previous, nullptr);
}
