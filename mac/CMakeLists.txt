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
