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
