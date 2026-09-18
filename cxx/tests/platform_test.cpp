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
#include <string_view>
#include <thread>

namespace platform = infobot::platform;

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
    [[nodiscard]] int write_end() const { return ends_[1]; }
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

// COVERS: FR-1.2 | positive
//
// Whatever writes the payload finishes writing: the input is read to its end.
TEST_CASE("drain reads a descriptor to its end") {
    Pipe pipe;
    constexpr std::string_view body = R"({"session_id":"s"})";
    REQUIRE(::write(pipe.write_end(), body.data(), body.size()) ==
            static_cast<ssize_t>(body.size()));
    pipe.close_write();
    platform::drain(pipe.read_end());
    std::array<char, 1> after{};
    CHECK(::read(pipe.read_end(), after.data(), after.size()) == 0);
}

// COVERS: FR-1.2 | negative
TEST_CASE("drain returns on a descriptor that cannot be read") {
    platform::drain(-1);
    CHECK(true);
}

// COVERS: FR-1.2 | edge
//
// A signal interrupting the read is not the end of the input.
TEST_CASE("drain carries on through an interrupted read") {
    constexpr std::chrono::milliseconds pause{20};
    struct sigaction action{};
    action.sa_handler = nothing;
    struct sigaction previous{};
    REQUIRE(::sigaction(SIGUSR1, &action, &previous) == 0);

    Pipe pipe;
    std::atomic<bool> closed{false};
    const pthread_t reader = ::pthread_self();
    std::thread interrupter([&] {
        std::this_thread::sleep_for(pause);
        ::pthread_kill(reader, SIGUSR1);
        std::this_thread::sleep_for(pause);
        closed = true;
        pipe.close_write();
    });
    platform::drain(pipe.read_end());
    interrupter.join();
    CHECK(closed);
    ::sigaction(SIGUSR1, &previous, nullptr);
}
