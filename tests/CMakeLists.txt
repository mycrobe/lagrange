# Host unit tests for lagrange.
#
# Standalone:  cmake -S tests -B build-host-tests && ctest --test-dir build-host-tests
# Also pulled in by the root build via include(CTest) + add_subdirectory(tests),
# so `ctest` from a normal host configure sees them too.
#
# These tests compile portable/mac-layer sources natively with fake platform
# headers (tests/host/); they never need a Mac or the Retro68 toolchain.

cmake_minimum_required (VERSION 3.15)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project (LagrangeHostTests LANGUAGES C)
    enable_testing()
endif ()

set (LAGRANGE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

# --- Classic POSIX/pthread layer (mac/posix/pthread_classic.c) --------------
# Fake Thread Manager + heap-zone (tests/host/fake_threads.c); pthread.h is
# compiled in host-test mode so it supplies integer stand-ins for the target's
# newlib pthread_* words.
add_executable (test_pthread_classic
    test_pthread_classic.c
    host/fake_threads.c
    ${LAGRANGE_ROOT}/mac/posix/pthread_classic.c
)
target_include_directories (test_pthread_classic PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}          # test.h
    ${CMAKE_CURRENT_SOURCE_DIR}/host     # fake <Threads.h> / <MacMemory.h>
    ${LAGRANGE_ROOT}/mac/posix           # pthread.h
)
target_compile_definitions (test_pthread_classic PRIVATE LAGRANGE_PTHREAD_HOSTTEST=1)
target_compile_options (test_pthread_classic PRIVATE -Wall -Wextra)
add_test (NAME test_pthread_classic COMMAND test_pthread_classic)

# --- Classic process-level POSIX bits (mac/posix/posix_classic.c) -----------
# clock_gettime / nanosleep / sched_yield.  gettimeofday + <time.h> are the
# host's; TickCount + YieldToAnyThread come from the fake (a yield advances the
# fake tick counter, so nanosleep terminates deterministically).
add_executable (test_posix_classic
    test_posix_classic.c
    host/fake_threads.c
    ${LAGRANGE_ROOT}/mac/posix/posix_classic.c
)
target_include_directories (test_posix_classic PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/host     # fake <Threads.h> / <Events.h> / <MacMemory.h>
    ${LAGRANGE_ROOT}/mac/posix
)
target_compile_definitions (test_posix_classic PRIVATE LAGRANGE_PTHREAD_HOSTTEST=1)
target_compile_options (test_posix_classic PRIVATE -Wall -Wextra)
add_test (NAME test_posix_classic COMMAND test_posix_classic)
