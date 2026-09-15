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
find_package(jsoncons REQUIRED)

# The status file's schema is the Go port's, read from where it lives rather
# than copied, and compiled in as a string so a render opens no file for it.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../internal/state/status.schema.json" status_schema)
configure_file(src/status_schema.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/status_schema.hpp" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../internal/state/status.schema.json")

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
    src/environment.cpp
    src/files.cpp
    src/forget.cpp
    src/gojson.cpp
    src/gopath.cpp
    src/goquote.cpp
    src/gotext.cpp
    src/host.cpp
    src/num.cpp
    src/palette.cpp
    src/payload.cpp
    src/pricing.cpp
    src/print_table.cpp
    src/render.cpp
    src/state.cpp
    src/status_file.cpp
    src/usage.cpp
    src/width.cpp
    src/width_table.cpp
)
target_include_directories(infobot PUBLIC src)
target_include_directories(infobot PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
# Static, so the measured binary carries what it runs and pays no dynamic
# loading for it on every render.
target_link_libraries(infobot PUBLIC simdjson::simdjson_static jsoncons)

# Every *_test.cpp in tests/. INFOBOT_TEST_SOURCES names a subset instead, as
# paths relative to tests/, which is how one suite is built and run while the
# others are still being written.
set(INFOBOT_TEST_SOURCES "" CACHE STRING "test files to build, relative to tests/; empty for all")
if(INFOBOT_TEST_SOURCES)
    list(TRANSFORM INFOBOT_TEST_SOURCES PREPEND "tests/" OUTPUT_VARIABLE test_sources)
else()
    file(GLOB test_sources CONFIGURE_DEPENDS tests/*_test.cpp)
endif()
add_executable(infobot-tests tests/main.cpp tests/support.cpp ${test_sources})
target_include_directories(infobot-tests PRIVATE tests)
target_link_libraries(infobot-tests PRIVATE infobot doctest::doctest)

add_executable(statusline cmd/statusline.cpp)
target_link_libraries(statusline PRIVATE infobot)

add_executable(forget cmd/forget.cpp)
target_link_libraries(forget PRIVATE infobot)

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
    target_compile_options(statusline PRIVATE --coverage -O0)
    target_compile_options(forget PRIVATE --coverage -O0)
    target_link_options(infobot-tests PRIVATE --coverage)
    target_link_options(statusline PRIVATE --coverage)
    target_link_options(forget PRIVATE --coverage)
endif()
