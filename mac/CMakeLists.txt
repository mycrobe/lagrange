# lagrange Classic flavor (M-tier, OS 8/9 PPC) -- Retro68 cross build.
# This is the *network-first* first slice of the Classic port (mirrors the
# darwin8/T-tier d8_smoke): it cross-builds the vendored ClassicNet OT slice
# (cn_ot Open Transport + cn_tls mbedTLS) + cy384's PPC mbedTLS and links a
# raw TLS Gemini fetch -- proving the M-tier network seam is portable to
# Mac OS 9 before any UI.  The full Classic canvas host (src/mac/) and the
# the_Foundation seam are the next slices.
#
#   cmake -S mac -B build-mac \
#     -DCMAKE_TOOLCHAIN_FILE=<Retro68>/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake
#   cmake --build build-mac      -> build-mac/cn_ot_smoke.bin (+ .APPL)
#
# See PLAN.md Phase 2 (build systems + the network slice) and the user-level
# Retro68 toolchain doc.  The OT/TLS stack is proven in starscape on OS 9;
# this reuses the exact same vendored slice + mbedTLS fork.
cmake_minimum_required(VERSION 3.13)
project(LagrangeClassic C CXX)   # CXX so add_application can link RetroConsole

set(CMAKE_C_STANDARD 90)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CLASSICNET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/ClassicNet"
    CACHE PATH "ClassicNet project (git submodule)")
set(CN_SRC "${CLASSICNET_DIR}/src")
set(CN_TGT "${CLASSICNET_DIR}/target")
set(MBEDTLS_PPC_ROOT "${CLASSICNET_DIR}/deps/mbedtls-ppc"
    CACHE PATH "cy384 classic-Mac mbedTLS fork, PPC build (vendor/ClassicNet/scripts/setup-mbedtls.sh)")

if(NOT EXISTS "${MBEDTLS_PPC_ROOT}/build-ppc/library/libmbedtls.a")
    message(FATAL_ERROR
        "mbedTLS (ppc) missing at ${MBEDTLS_PPC_ROOT} -- cross-build it via "
        "vendor/ClassicNet/scripts/setup-mbedtls.sh (the PPC step, Retro68).")
endif()

# --- ClassicNet OT slice: Open Transport transport + TLS (verify-none; the
#     app's trust is the TOFU pin, layered above) + the Mac platform glue
#     (cn_mac_time.c: the mbedTLS time source + RNG jitter). ---
add_application(cn_ot_smoke CONSOLE TYPE "APPL" CREATOR "CnOs"
    FILES
        cn_ot_smoke.c
        cn_ot_smoke.r
        ${CN_TGT}/cn_mac_time.c
        ${CN_SRC}/cn_ot.c
        ${CN_SRC}/cn_tls.c
)
target_compile_definitions(cn_ot_smoke PRIVATE
    CN_WITH_OT=1
    CN_WITH_MBEDTLS=1
    CN_TLS_FORCE_TLS12=1
    MBEDTLS_USER_CONFIG_FILE="mbedtls_userconfig.h")
target_include_directories(cn_ot_smoke PRIVATE
    ${CLASSICNET_DIR}/include
    ${CN_TGT}                        # mbedtls_userconfig.h
    ${MBEDTLS_PPC_ROOT}/include)
target_link_libraries(cn_ot_smoke
    ${MBEDTLS_PPC_ROOT}/build-ppc/library/libmbedtls.a
    ${MBEDTLS_PPC_ROOT}/build-ppc/library/libmbedx509.a
    ${MBEDTLS_PPC_ROOT}/build-ppc/library/libmbedcrypto.a
    OpenTransportAppPPC OpenTransportLib OpenTptInternetLib
    ThreadsLib)   # YieldToAnyThread during the poll loop (no hardware RNG,
                  # and OT callbacks need yielding -- see cn_ot_smoke.c)

# --- the_Foundation + ClassicNet OT seam (the L9 portable core) -------------
# The darwin8/tier T seam (osx/CMakeLists.txt) cross-compiles the_Foundation
# with TFDN_CLASSICNET over cn_darwin8; this is the Retro68/OT analog: build
# the_Foundation as a static lib whose platform/classicnet/tlsrequest.c OT
# backend drives a CNTransport (cn_ot) + mbedTLS-ppc.  Link it into a
# tls_smoke that does a Gemini fetch through iTlsRequest -- proving lagrange's
# gmrequest path is portable to Classic before any UI.
#
# OPT-IN: the portable core's Retro68 port is in flight.  The Retro68 POSIX
# void is now answered by the mac/posix/ pthread layer (Thread Manager) plus
# _POSIX_TIMERS for clock_gettime/nanosleep; the seam still needs to
# cross-build green before it becomes a default gate.  Default build-mac.sh
# stays the green cn_ot_smoke gate; build the seam with
#   cmake -S mac -B build-mac -DCMAKE_TOOLCHAIN_FILE=... -DLAGRANGE_TFDN_SEAM=ON
option (LAGRANGE_TFDN_SEAM "Build the the_Foundation + ClassicNet OT seam (L9)" OFF)
if (LAGRANGE_TFDN_SEAM)
set (UNISTRING_RETRO68     "${CLASSICNET_DIR}/deps/libunistring-retro68")
set (PCRE2_RETRO68         "${CLASSICNET_DIR}/deps/pcre2-retro68")
set (ZLIB_RETRO68          "${CLASSICNET_DIR}/deps/zlib-retro68")
if (NOT EXISTS "${UNISTRING_RETRO68}")
    message (FATAL_ERROR "UNISTRING_RETRO68 missing at ${UNISTRING_RETRO68}")
endif ()
if (NOT EXISTS "${PCRE2_RETRO68}")
    message (FATAL_ERROR "PCRE2_RETRO68 missing at ${PCRE2_RETRO68}")
endif ()
if (NOT EXISTS "${ZLIB_RETRO68}")
    message (FATAL_ERROR "ZLIB_RETRO68 missing at ${ZLIB_RETRO68}")
endif ()

set (TFDN_STATIC_LIBRARY      ON  CACHE BOOL "" FORCE)
set (TFDN_ENABLE_INSTALL      OFF CACHE BOOL "" FORCE)
set (TFDN_ENABLE_TESTS        OFF CACHE BOOL "" FORCE)
set (TFDN_ENABLE_WEBREQUEST   OFF CACHE BOOL "" FORCE)
set (TFDN_ENABLE_WARN_ERROR   OFF CACHE BOOL "" FORCE)
set (TFDN_ENABLE_TLSREQUEST   ON  CACHE BOOL "" FORCE)
set (TFDN_CLASSICNET          ON  CACHE BOOL "" FORCE)  # socket/tls seam over ClassicNet
set (TFDN_ENABLE_SSE41        OFF CACHE BOOL "" FORCE)   # no SSE on PPC
set (TFDN_ENABLE_DEBUG_OUTPUT OFF CACHE BOOL "" FORCE)
set (TFDN_ENABLE_MUTEX_DEBUG  OFF CACHE BOOL "" FORCE)
# Retro68 static deps (the fork's RestroPPC Depends branch reads these):
set (UNISTRING_DIR   "${UNISTRING_RETRO68}"    CACHE PATH "" FORCE)
set (UNISTRING_ICONV NO                        CACHE BOOL "" FORCE)
set (PCRE2_ROOT      "${PCRE2_RETRO68}"        CACHE PATH "" FORCE)
set (ZLIB_ROOT       "${ZLIB_RETRO68}"         CACHE PATH "" FORCE)

# --- Classic POSIX/pthread layer (Mac OS Thread Manager) --------------------
# Retro68 ships newlib's RTEMS-flavored <pthread.h>, but NO library implements
# the API: every declaration is gated on `_POSIX_THREADS` (undefined for this
# target in <sys/features.h>), and no static lib defines pthread_create/
# pthread_mutex_* (libThreadsLib.a only has NewThread/DisposeThread/
# GetCurrentThread/YieldToAnyThread).  the_Foundation, though, is built on a
# real thread backend: iThread/iMutex/iCondition -> C11 threads -> pthreads,
# and the ClassicNet iTlsRequest seam runs its fetch on a worker thread.
# mac/posix/ provides that pthread API over the Thread Manager, plus
# clock_gettime/nanosleep/sched_yield.  `include_directories(BEFORE)` makes our
# <pthread.h> win the search; clock_gettime/nanosleep are declared by a minimal
# force-included header (classic_posix.h) rather than by switching on
# `_POSIX_TIMERS`, because that gate makes <time.h> pull <signal.h>, which in
# Retro68/Multiverse collides with <OpenTransport.h>/<MacTypes.h> everywhere.
set (CLASSIC_POSIX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/posix")
include_directories (BEFORE ${CLASSIC_POSIX_DIR})
add_compile_options (-include ${CLASSIC_POSIX_DIR}/classic_posix.h)
list (APPEND CMAKE_REQUIRED_INCLUDES ${CLASSIC_POSIX_DIR})
add_library (classic_posix STATIC
    ${CLASSIC_POSIX_DIR}/pthread_classic.c
    ${CLASSIC_POSIX_DIR}/posix_classic.c)
target_include_directories (classic_posix PUBLIC ${CLASSIC_POSIX_DIR})
target_link_libraries (classic_posix PUBLIC ThreadsLib)

add_subdirectory (${CMAKE_CURRENT_SOURCE_DIR}/../lib/the_Foundation
                  ${CMAKE_CURRENT_BINARY_DIR}/the_Foundation)
add_library (the_Foundation::the_Foundation ALIAS the_Foundation)

# ClassicNet OT slice as a static lib, linked PUBLIC into the_Foundation (so
# the seam's tlsrequest.c resolves cn_ot/cn_tls + mbedTLS symbols).
add_library (cn_ot STATIC
    ${CN_TGT}/cn_mac_time.c
    ${CN_SRC}/cn_ot.c
    ${CN_SRC}/cn_tls.c)
target_compile_definitions (cn_ot PUBLIC
    CN_WITH_OT=1
    CN_WITH_MBEDTLS=1
    CN_TLS_FORCE_TLS12=1
    MBEDTLS_USER_CONFIG_FILE="mbedtls_userconfig.h")
target_include_directories (cn_ot PUBLIC
    ${CLASSICNET_DIR}/include
    ${CN_TGT}                        # mbedtls_userconfig.h
    ${MBEDTLS_PPC_ROOT}/include)
target_link_libraries (cn_ot PUBLIC
    ${MBEDTLS_PPC_ROOT}/build-ppc/library/libmbedtls.a
    ${MBEDTLS_PPC_ROOT}/build-ppc/library/libmbedx509.a
    ${MBEDTLS_PPC_ROOT}/build-ppc/library/libmbedcrypto.a
    OpenTransportAppPPC OpenTransportLib OpenTptInternetLib
    ThreadsLib)

if (TARGET the_Foundation)
    # The ClassicNet seam needs the same transports definitions/includes as
    # cn_ot, and must link it (CN_WITH_OT selects tlsrequest.c's OT backend).
    target_compile_definitions (the_Foundation PUBLIC
        CN_WITH_OT=1
        CN_WITH_MBEDTLS=1
        CN_TLS_FORCE_TLS12=1
        "MBEDTLS_USER_CONFIG_FILE=\"mbedtls_userconfig.h\"")
    target_include_directories (the_Foundation PUBLIC
        ${CLASSICNET_DIR}/include
        ${CN_TGT}
        ${MBEDTLS_PPC_ROOT}/include)
    target_link_libraries (the_Foundation PUBLIC cn_ot classic_posix)
    # Retro68: libunistring's unitypes.h -> unistring/stdint.h #defines the
    # exact-width int types to unistring_uint32_t (unsigned int) on non-glibc,
    # while Retro68's system uint32_t is `unsigned long` (PPC32 ILP32: both
    # 32-bit, ABI-safe, distinct C types).  Callers passing uint32_t* into the
    # u32_* API get an incompatible-pointer-types ERROR.  Downgrade it + the
    # uint32_t-vs-%u format note to warnings (same fix as the PCRE2 Retro68
    # build; see docs/arcana.md).
    target_compile_options (the_Foundation PRIVATE
        -Wno-error=incompatible-pointer-types
        -Wno-incompatible-pointer-types
        -Wno-format)
    # Retro68 POSIX void: clock_gettime/nanosleep are declared by <time.h>
    # under _POSIX_TIMERS (defined above) and implemented in mac/posix/;
    # struct tm has no tm_gmtoff, guarded in src/time.c for iPlatformClassic.
    # pthreads are provided over the Thread Manager by classic_posix (linked
    # above); see mac/posix/pthread.h for the full rationale.
endif ()

# --- the_Foundation seam fetch proof: a Gemini fetch through iTlsRequest over
#     OT + mbedTLS-ppc on the macos9 guest -- the L9 mirror of d8_tls_smoke. ---
add_application (tls_smoke CONSOLE TYPE "APPL" CREATOR "CnOs"
    FILES
        tls_smoke.c
        tls_smoke.r
)
target_link_libraries (tls_smoke
    # cn_ot (which owns cn_mac_time.o) and mbedTLS reference each other
    # (mbedTLS's platform time macros call cn_mac_time/mbedtls_ms_time/
    # mbedtls_platform_gmtime_r, defined in cn_mac_time.o), so the static
    # archives must be resolved together -- a plain left-to-right pass leaves
    # one side unresolved.
    -Wl,--start-group
    the_Foundation::the_Foundation
    cn_ot
    ${MBEDTLS_PPC_ROOT}/build-ppc/library/libmbedtls.a
    ${MBEDTLS_PPC_ROOT}/build-ppc/library/libmbedx509.a
    ${MBEDTLS_PPC_ROOT}/build-ppc/library/libmbedcrypto.a
    -Wl,--end-group
    OpenTransportAppPPC OpenTransportLib OpenTptInternetLib
    ThreadsLib)
endif ()  # LAGRANGE_TFDN_SEAM
