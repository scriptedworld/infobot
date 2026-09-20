# The C++ status line. docs/SPEC.md says what each module owns, and
# docs/DECISIONS/what-cxx-is-built-from.md what it is built from and why.
#
# Dependencies come through FetchContent pinned to a commit and link static, so
# a clone builds with nothing installed beyond a compiler, CMake and Ninja.
# `just cxx-profile` keeps their sources in .cache/fetchcontent between builds
# and hands the gate a compilation database holding infobot's files only.

cmake_minimum_required(VERSION 3.28)
project(infobot LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# gcov instrumentation is a build of its own, so it never reaches a binary that
# gets timed.
option(INFOBOT_COVERAGE "instrument infobot's own code for gcov" OFF)

include(FetchContent)
set(BUILD_SHARED_LIBS OFF)
set(UTF8PROC_INSTALL OFF)
set(DOCTEST_NO_INSTALL ON)
FetchContent_Declare(utf8proc
    GIT_REPOSITORY https://github.com/JuliaStrings/utf8proc.git
    GIT_TAG d7bf128df773c2a1a7242eb80e51e91a769fc985  # v2.11.0
    SYSTEM)
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG 1da23a3e8119ec5cce4f9388e91b065e20bf06f5  # v2.4.12
    SYSTEM)
FetchContent_MakeAvailable(utf8proc doctest)

# Warnings as errors for infobot's targets and none of the fetched ones.
add_library(infobot-warnings INTERFACE)
target_compile_options(infobot-warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)

add_library(infobot STATIC
    src/forget.cpp
    src/platform.cpp
    src/render.cpp)
target_include_directories(infobot PUBLIC src)
target_link_libraries(infobot PRIVATE infobot-warnings utf8proc)

add_executable(statusline cmd/statusline.cpp)
target_link_libraries(statusline PRIVATE infobot infobot-warnings)

add_executable(forget cmd/forget.cpp)
target_link_libraries(forget PRIVATE infobot infobot-warnings)

file(GLOB test_sources CONFIGURE_DEPENDS tests/*_test.cpp)
add_executable(infobot-tests tests/main.cpp tests/allocation.cpp ${test_sources})
target_include_directories(infobot-tests PRIVATE tests)
target_link_libraries(infobot-tests PRIVATE infobot infobot-warnings doctest::doctest)

# The program is instrumented and the tests are not: an assertion macro expands
# to branches that are taken only when it fails, which would measure doctest
# rather than anything a test is missing. Entry points are instrumented too and
# run once by `just cxx-profile`, since no test process reaches main().
if(INFOBOT_COVERAGE)
    foreach(target infobot statusline forget)
        target_compile_options(${target} PRIVATE --coverage -O0)
        target_link_options(${target} PRIVATE --coverage)
    endforeach()
    target_link_options(infobot-tests PRIVATE --coverage)
endif()
