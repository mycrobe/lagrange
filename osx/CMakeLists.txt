# lagrange Tiger flavor (T-tier, 10.4 PPC) -- darwin8 cross build, run INSIDE
# the amd64 container by scripts/build-osx.sh (the cross compiler only runs
# there).  See PLAN.md Phase 2 and the user-level darwin8 toolchain doc.
#
#   cmake -S osx -B build-osx \
#     -DCMAKE_TOOLCHAIN_FILE=osx/darwin8.toolchain.cmake
#   cmake --build build-osx   -> build-osx/d8_smoke
#
# This is the *network-first* first slice of the T-tier port: it cross-builds
# the vendored ClassicNet darwin8 slice (cn_darwin8 BSD sockets + cn_tls
# mbedTLS) + mbedTLS-d8 and links a raw TLS Gemini fetch -- proving the
# network seam is portable to Tiger before any UI.  The full Aqua canvas host
# (src/macos/) and the the_Foundation seam are the next slices.
cmake_minimum_required(VERSION 3.13)
project(LagrangeOSX C)
set(CMAKE_C_STANDARD 90)
set(CMAKE_C_STANDARD_REQUIRED ON)

# The proven flag set (Starscape T-1 d8_get on-device): -O2 -Wall, the
# CN/TLS/userconfig defines, the include set, -lresolv, and -Wl,-force_cpusubtype_ALL
# (ppc_970 emutls trap) on every link line.
set(CMAKE_C_FLAGS "-O2 -Wall")

# Configure-time link probe -- this flavor's analog of the OS 9 Toolbox
# packing guard: 64-bit integer division on PPC32 pulls libgcc's
# __udivdi3/__divdi3/__umoddi3/__moddi3 members, which is exactly what tripped
# cctools ld against danupsher's prebuilt libgcc ("sectionForAddress(0x1CC0)
# address not in any section"); gcc-install/ carries our own
# all-target-libgcc, and this smoke links them for real.  A toolchain
# regression fails configure here, never at an app link.
include(CheckCSourceCompiles)
check_c_source_compiles(
"int main(void) {
    volatile unsigned long long a = 12345678901234567ULL, b = 1000ULL;
    volatile long long c = -12345678901234567LL;
    unsigned long long q = a / b, r = a % b;
    long long q2 = c / 7LL, m2 = c % 7LL;
    return (int)((q + r + (unsigned long long)q2 + (unsigned long long)m2) & 1);
}"
    GM_D8_DIV64_LINK_OK)
if(NOT GM_D8_DIV64_LINK_OK)
    message(FATAL_ERROR
        "64-bit division does not LINK (libgcc vs cctools ld regression; "
        "the toolchain's gcc-install must carry the version-matched "
        "all-target-libgcc -- see ~/.local/share/doc/darwin8-toolchain.md).")
endif()

# The CarbonCore headers of the bundled 10.4u SDK: plain-C sources that
# include cn_types.h on the target path pull MacTypes.h (UInt16/OSStatus)
# from here (the SDK mount point is fixed by the toolchain doc).
set(MACCORE
    /sdk/MacOSX10.4u.sdk/System/Library/Frameworks/CoreServices.framework/Versions/A/Frameworks/CarbonCore.framework/Versions/A/Headers)

set(CLASSICNET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/ClassicNet"
    CACHE PATH "ClassicNet project (git submodule)")
set(MBEDTLS_D8_ROOT "${CLASSICNET_DIR}/deps/mbedtls-darwin8"
    CACHE PATH "vanilla mbedTLS 3.6.0 cross-built for darwin8 (scripts/setup-classicnet.sh / vendor/ClassicNet/scripts/setup-mbedtls.sh)")

if(NOT EXISTS "${MBEDTLS_D8_ROOT}/library/libmbedtls.a")
    message(FATAL_ERROR
        "mbedTLS (darwin8) missing at ${MBEDTLS_D8_ROOT} -- cross-build it "
        "via vendor/ClassicNet/scripts/setup-mbedtls.sh (the darwin8 step).")
endif()

add_compile_definitions(
    CN_WITH_DARWIN8=1
    CN_WITH_MBEDTLS=1
    CN_TLS_FORCE_TLS12=1
    "MBEDTLS_USER_CONFIG_FILE=\"d8_userconfig.h\"")
include_directories(${MACCORE})

# --- ClassicNet's darwin8 slice: BSD-sockets transport + TLS (verify-none;
#     the app's trust is the TOFU pin, layered above) + the mbedTLS platform
#     glue (d8_time.c) ---
add_library(cn_d8 STATIC
    ${CLASSICNET_DIR}/src/cn_darwin8.c
    ${CLASSICNET_DIR}/src/cn_tls.c
    ${CLASSICNET_DIR}/target/d8_time.c)
target_include_directories(cn_d8 PUBLIC
    ${CLASSICNET_DIR}/include
    ${CLASSICNET_DIR}/target
    ${MBEDTLS_D8_ROOT}/include)
target_link_libraries(cn_d8 PUBLIC
    ${MBEDTLS_D8_ROOT}/library/libmbedtls.a
    ${MBEDTLS_D8_ROOT}/library/libmbedx509.a
    ${MBEDTLS_D8_ROOT}/library/libmbedcrypto.a)

# --- raw TLS Gemini fetch smoke: the first T-tier network proof.  Links the
#     slice + mbedTLS-8 and REPORTS the exchange on stdout (verify-none; the
#     TOFU pin is layered by the app above this seam).  Run on petal. ---
add_executable(d8_smoke d8_smoke.c)
target_link_libraries(d8_smoke cn_d8 -lresolv -Wl,-force_cpusubtype_ALL)

# --- the_Foundation + ClassicNet seam cross-build (darwin8) -------------------
# The big next slice: get the_Foundation (socket.c/tlsrequest.c ClassicNet
# backends, selected by TFDN_CLASSICNET) cross-compiling for Tiger so lagrange's
# real gmrequest/gmcerts fetch path ports.  Built as a static lib (TFDN_STATIC)
# and linked against cn_d8 (ClassicNet darwin8 slice) + mbedTLS-d8.
#
# Mandatory the_Foundation dependency: libunistring, cross-built for darwin8
# into deps/libunistring-darwin8 (see the arcana entry; the GNU 1.4.2 tarball,
# source + build under deps/libunistring-*, is a gitignored local artifact).
set (LIBUNISTRING_D8 "${CLASSICNET_DIR}/deps/libunistring-darwin8")
if (NOT EXISTS "${LIBUNISTRING_D8}/include/unistr.h")
    message (FATAL_ERROR
        "libunistring (darwin8) missing at ${LIBUNISTRING_D8} -- cross-build the "
        "GNU 1.4.2 tarball from deps/ (--host=powerpc-apple-darwin8 "
        "--disable-shared --enable-static, AVOID_ANY_THREADS config fix; see arcana).")
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

# libunistring for darwin8 (tells Depends.cmake where the cross-built tree is;
# iconv stays linked from libSystem, so no separate iconv lib).
set (UNISTRING_DIR  "${LIBUNISTRING_D8}" CACHE PATH   "" FORCE)
set (UNISTRING_ICONV NO                    CACHE BOOL "" FORCE)

add_subdirectory (${CMAKE_CURRENT_SOURCE_DIR}/../lib/the_Foundation
                  ${CMAKE_CURRENT_BINARY_DIR}/the_Foundation)
add_library (the_Foundation::the_Foundation ALIAS the_Foundation)

if (TARGET the_Foundation)
    # Attach ClassicNet + mbedTLS-d8 usage requirements so the classicnet
    # socket.c/tlsrequest.c seam compiles against the same defines/includes as
    # cn_d8 (CN_WITH_DARWIN8/MBEDTLS + the d8 userconfig, the MACCORE headers
    # that cn_types.h pulls in, and the mbedTLS include dir).
    target_include_directories (the_Foundation PUBLIC
        ${CLASSICNET_DIR}/include
        ${CLASSICNET_DIR}/target
        ${MBEDTLS_D8_ROOT}/include
        ${MACCORE}
    )
    target_compile_definitions (the_Foundation PUBLIC
        CN_WITH_DARWIN8=1
        CN_WITH_MBEDTLS=1
        CN_TLS_FORCE_TLS12=1
        "MBEDTLS_USER_CONFIG_FILE=\"d8_userconfig.h\""
    )
    target_link_libraries (the_Foundation PUBLIC cn_d8)

    # Tiger POSIX shims (strnlen / clock_gettime / pthread_setname_np): the
    # the_Foundation core sources assume modern macOS/POSIX.  Force-include the
    # darwin8 build-time shim (static inline no-ops/equivalents) so they compile
    # unchanged.  PRIVATE: only the_Foundation's own TUs need it, not consumers.
    # See osx/darwin8_posix_shim.h + docs/arcana.md.
    target_compile_definitions (the_Foundation PRIVATE DARWIN8=1)
    target_compile_options (the_Foundation PRIVATE
        -include ${CMAKE_CURRENT_SOURCE_DIR}/darwin8_posix_shim.h)

    # Tiger lacks <spawn.h>: provide a real posix_spawn over fork/exec so
    # iProcess (platform/posix/process.c) compiles AND works.  The shim dir is on
    # the include path (so `<spawn.h>` resolves) and spawn.c is compiled into the
    # static lib.  PRIVATE to the_Foundation; see osx/darwin8_sdk_shim/ + arcana.
    target_include_directories (the_Foundation PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/darwin8_sdk_shim)
    target_sources (the_Foundation PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/darwin8_sdk_shim/spawn.c)
endif ()

# --- the_Foundation TLS-seam fetch proof: cross-compiles the seam and does a
#     real Gemini fetch on-device through iTlsRequest (the gmrequest path). ---
add_executable (d8_tls_smoke d8_tls_smoke.c)
set_property (TARGET d8_tls_smoke PROPERTY C_STANDARD 11)
target_link_libraries (d8_tls_smoke PRIVATE
    the_Foundation::the_Foundation
    -lresolv
    -Wl,-force_cpusubtype_ALL
)
