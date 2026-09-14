# The C++ implementation of the status line. Not deployed: bin/infobot execs the
# Go binary, and this tree is held to byte parity with it and measured against
# it. See cpp/README.md.
#
# THE COMPILATION DATABASE IS THE GATE'S CONTRACT. toolbox's cpp-std-quality
# jig reads compile_commands.json for format, lint, analyse and headers, so it is
# exported on every configure and linked to the repository root by `just`.

cmake_minimum_required(VERSION 3.28)
project(infobot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Nothing here is a module, and scanning for them adds a dyndep step per target.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Coverage is a build of its own, because gcov's instrumentation is what the
# jig's `tests` task reads and it must never reach a binary that gets timed.
option(INFOBOT_COVERAGE "instrument for gcov" OFF)

find_package(simdjson REQUIRED)
find_package(doctest REQUIRED)

add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Werror
)

add_library(infobot STATIC
    src/num.cpp
    src/payload.cpp
)
target_include_directories(infobot PUBLIC src)
# Static, so the measured binary carries what it runs and pays no dynamic
# loading for it on every render.
target_link_libraries(infobot PUBLIC simdjson::simdjson_static)

add_executable(infobot-tests
    tests/main.cpp
    tests/num_test.cpp
    tests/payload_test.cpp
)
target_link_libraries(infobot-tests PRIVATE infobot doctest::doctest)

# THE CODE UNDER TEST IS INSTRUMENTED AND THE TESTS ARE NOT, which is the
# boundary `go test -cover` draws for the Go tree. A doctest assertion expands
# to branches that are taken only when it fails, so an instrumented test file
# reads about 40% of branches taken in a suite that passes. That figure measures
# the assertion macros rather than anything a test could be missing.
#
# Every product source is instrumented, entry points included once they exist,
# so no file of the program is outside the per-file minimum.
if(INFOBOT_COVERAGE)
    target_compile_options(infobot PRIVATE --coverage -O0)
    target_link_options(infobot-tests PRIVATE --coverage)
endif()
