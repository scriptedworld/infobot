#include "allocation.hpp"

#include <cstddef>
#include <cstdlib>
#include <functional>
#include <new>

namespace {

// Allocations left before the armed one fails. Negative is disarmed. Per thread,
// so a library thread is never failed by a test on another.
thread_local std::ptrdiff_t countdown = -1;
thread_local bool fired_here = false;

// Whether this allocation is the armed one, consuming it if so.
bool should_fail() {
    if (countdown < 0) {
        return false;
    }
    if (countdown == 0) {
        countdown = -1;
        fired_here = true;
        return true;
    }
    --countdown;
    return false;
}

// A zero-byte request still returns a unique pointer, as the default does.
std::size_t at_least_one(std::size_t size) { return size == 0 ? 1 : size; }

void* plain(std::size_t size) {
    if (should_fail()) {
        return nullptr;
    }
    return std::malloc(at_least_one(size));
}

void* aligned(std::size_t size, std::align_val_t alignment) {
    if (should_fail()) {
        return nullptr;
    }
    const auto align = static_cast<std::size_t>(alignment);
    // aligned_alloc wants a size that is a multiple of the alignment.
    const std::size_t rounded = (at_least_one(size) + align - 1) / align * align;
    return std::aligned_alloc(align, rounded);
}

void* or_throw(void* allocated) {
    if (allocated == nullptr) {
        throw std::bad_alloc();
    }
    return allocated;
}

}  // namespace

void* operator new(std::size_t size) { return or_throw(plain(size)); }

void* operator new[](std::size_t size) { return or_throw(plain(size)); }

void* operator new(std::size_t size, std::align_val_t alignment) {
    return or_throw(aligned(size, alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    return or_throw(aligned(size, alignment));
}

void* operator new(std::size_t size, const std::nothrow_t& /*tag*/) noexcept {
    return plain(size);
}

void* operator new[](std::size_t size, const std::nothrow_t& /*tag*/) noexcept {
    return plain(size);
}

void* operator new(std::size_t size,
                   std::align_val_t alignment,
                   const std::nothrow_t& /*tag*/) noexcept {
    return aligned(size, alignment);
}

void* operator new[](std::size_t size,
                     std::align_val_t alignment,
                     const std::nothrow_t& /*tag*/) noexcept {
    return aligned(size, alignment);
}

void operator delete(void* pointer) noexcept { std::free(pointer); }

void operator delete[](void* pointer) noexcept { std::free(pointer); }

void operator delete(void* pointer, std::size_t /*size*/) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer, std::size_t /*size*/) noexcept {
    std::free(pointer);
}

void operator delete(void* pointer, std::align_val_t /*alignment*/) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer, std::align_val_t /*alignment*/) noexcept {
    std::free(pointer);
}

void operator delete(void* pointer,
                     std::size_t /*size*/,
                     std::align_val_t /*alignment*/) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer,
                       std::size_t /*size*/,
                       std::align_val_t /*alignment*/) noexcept {
    std::free(pointer);
}

void operator delete(void* pointer, const std::nothrow_t& /*tag*/) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer, const std::nothrow_t& /*tag*/) noexcept {
    std::free(pointer);
}

void operator delete(void* pointer,
                     std::align_val_t /*alignment*/,
                     const std::nothrow_t& /*tag*/) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer,
                       std::align_val_t /*alignment*/,
                       const std::nothrow_t& /*tag*/) noexcept {
    std::free(pointer);
}

namespace infobot::test {

Failure::Failure(std::size_t count) {
    countdown = static_cast<std::ptrdiff_t>(count);
    fired_here = false;
}

Failure::~Failure() { countdown = -1; }

void one_allocation() {
    constexpr std::size_t size = 32;
    void* block = ::operator new(size);
    ::operator delete(block, size);
}

void* one_nothrow_allocation() {
    constexpr std::size_t size = 32;
    return ::operator new(size, std::nothrow);
}

bool Failure::fired() { return fired_here; }

Sweep fail_each_allocation(const std::function<void()>& body,
                           const std::function<void(std::size_t run)>& after) {
    // Far above any body a test hands this, and low enough that a body which
    // allocates without end fails the sweep instead of hanging the suite.
    constexpr std::size_t limit = 200000;
    Sweep sweep;
    for (std::size_t armed = 0; armed < limit; ++armed) {
        bool fired = false;
        bool thrown = false;
        {
            const Failure failure(armed);
            try {
                body();
            } catch (const std::bad_alloc&) {
                thrown = true;
            }
            fired = Failure::fired();
        }
        after(armed);
        if (!fired) {
            sweep.completed = true;
            break;
        }
        if (thrown) {
            ++sweep.thrown;
        } else {
            ++sweep.absorbed;
        }
    }
    return sweep;
}

}  // namespace infobot::test
