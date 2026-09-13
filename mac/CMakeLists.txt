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
# OPT-IN: the portable core's Retro68 port is still in flight (the Retro68
# POSIX void -- clock_gettime binding, _PTHREAD_ONCE_INIT, tm_gmtoff, and
# the rest of the "core assumes modern POSIX" gaps -- is the current work).
# Default build-mac.sh stays the green cn_ot_smoke gate; build the seam with
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
set (UNISTRING_DIR   "${LIBUNISTRING_RETRO68}" CACHE PATH "" FORCE)
set (UNISTRING_ICONV NO                        CACHE BOOL "" FORCE)
set (PCRE2_ROOT      "${PCRE2_RETRO68}"        CACHE PATH "" FORCE)
set (ZLIB_ROOT       "${ZLIB_RETRO68}"         CACHE PATH "" FORCE)

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
    target_link_libraries (the_Foundation PUBLIC cn_ot)
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
endif ()

# --- the_Foundation seam fetch proof: a Gemini fetch through iTlsRequest over
#     OT + mbedTLS-ppc on the macos9 guest -- the L9 mirror of d8_tls_smoke. ---
add_application (tls_smoke CONSOLE TYPE "APPL" CREATOR "CnOs"
    FILES
        tls_smoke.c
        tls_smoke.r
)
target_link_libraries (tls_smoke
    the_Foundation::the_Foundation
    OpenTransportAppPPC OpenTransportLib OpenTptInternetLib
    ThreadsLib)
endif ()  # LAGRANGE_TFDN_SEAM
