# ---------------------------------------------------------------------------
# Limit Order Book matching engine - top-level build file
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.20)
project(lob VERSION 0.1.0 LANGUAGES CXX)

# --- Language standard -----------------------------------------------------
# C++20, and fail loudly if the compiler can't do it (no silent fallback).
# Extensions OFF = portable ISO C++, not GNU dialect.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Lets editors (VS Code clangd) understand our include paths.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to Release if the user did not pick a build type.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(LOB_SANITIZERS "Enable AddressSanitizer + UBSan in Debug builds" ON)

# --- Shared compile settings -----------------------------------------------
# An INTERFACE target holds no code; it only carries flags. Every target that
# links to it inherits them, so flags live in one place.
add_library(lob_options INTERFACE)

target_compile_options(lob_options INTERFACE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
  $<$<CONFIG:Release>:-O3 -march=native>
  $<$<CONFIG:Debug>:-O0 -g3>
)

if(LOB_SANITIZERS)
  set(_san -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_compile_options(lob_options INTERFACE $<$<CONFIG:Debug>:${_san}>)
  target_link_options(lob_options INTERFACE $<$<CONFIG:Debug>:${_san}>)
endif()

# --- Third-party dependencies ----------------------------------------------
# FetchContent downloads pinned versions at configure time, so anyone who
# clones the repo gets the exact same test/benchmark libraries.
include(FetchContent)

FetchContent_Declare(googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(benchmark
  URL https://github.com/google/benchmark/archive/refs/tags/v1.9.1.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_MakeAvailable(googletest benchmark)

# --- The engine library ----------------------------------------------------
# All engine code goes in one library. Tests and benchmarks link to it, so
# they exercise exactly the same compiled code.
add_library(lob
  src/types.cpp
  src/order_book.cpp
)
target_include_directories(lob PUBLIC include)
target_link_libraries(lob PUBLIC lob_options)

# --- Unit tests --------------------------------------------------------------
enable_testing()
add_executable(lob_tests
  tests/test_sanity.cpp
  tests/test_types.cpp
  tests/test_order_book.cpp
  tests/test_matching.cpp
  tests/test_edge_cases.cpp
  tests/test_differential.cpp
  tests/test_bench_tools.cpp
  tests/test_object_pool.cpp
)
# The benchmark tools in bench/ are tested too.
target_include_directories(lob_tests PRIVATE bench)
target_link_libraries(lob_tests PRIVATE lob GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(lob_tests)

# --- Benchmarks --------------------------------------------------------------
add_executable(lob_bench
  bench/bench_sanity.cpp
  bench/bench_order_book.cpp
)
target_link_libraries(lob_bench PRIVATE lob benchmark::benchmark_main)

# --- Demo program ------------------------------------------------------------
add_executable(lob_demo apps/demo.cpp)
target_link_libraries(lob_demo PRIVATE lob)

# --- Latency benchmark (Layer 6) ----------------------------------------------
add_executable(lob_latency bench/latency_main.cpp)
target_link_libraries(lob_latency PRIVATE lob)
