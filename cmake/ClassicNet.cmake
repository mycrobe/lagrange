# cmake/ClassicNet.cmake
#
# ClassicNet host slice -- Phase 1 (N1 host wiring).
#
# Pulling the vendored vendor/ClassicNet submodule's portable core into the
# lagrange build and exposing the `classicnet` static lib it produces, built
# with the host darwin8 transport (cn_darwin8.c, BSD sockets) and the mbedTLS
# TLS layer (cn_tls.c over mbedTLS 3.6). This is the host-verified network
# slice the Socket/TlsRequest seam (N2) will be built on.
#
# Enabled by -DENABLE_CLASSICNET=ON. A config for the N1 host gate:
#
#   cmake -S . -B build-classicnet -DENABLE_CLASSICNET=ON -DENABLE_GUI=OFF
#   cmake --build build-classicnet --target gmclassicnet_smoke
#   scripts/test-classicnet-n1.sh
#
# The stock SDL/OpenSSL `app` is unaffected (this block only runs when the
# option is on), which keeps build-host green as the regression gate.

if (NOT ENABLE_CLASSICNET)
    return ()
endif ()

set (CLASSICNET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/ClassicNet"
    CACHE PATH "ClassicNet project (git submodule)")
if (NOT EXISTS "${CLASSICNET_DIR}/CMakeLists.txt")
    message (FATAL_ERROR "ClassicNet submodule missing at ${CLASSICNET_DIR}. \
Run: scripts/setup-classicnet.sh")
endif ()

# Host mbedTLS 3.6 (vanilla, x86_64 host) -- a build artifact provisioned by
# scripts/setup-classicnet.sh (gitignored, not shipped; cf. vendor/ClassicNet's
# deps pattern). Same 3.6 line as the PPC flavor, so the host slice exercises
# the same wire behaviour the target build will.
set (MBEDTLS_ROOT "${CLASSICNET_DIR}/deps/mbedtls-host3"
    CACHE PATH "host mbedTLS 3.6 tree for ClassicNet")
if (NOT EXISTS "${MBEDTLS_ROOT}/include/mbedtls/ssl.h")
    message (FATAL_ERROR "host mbedTLS not found at ${MBEDTLS_ROOT}. \
Run: scripts/setup-classicnet.sh")
endif ()

# ClassicNet host-slice configuration (its own cache vars).
set (CN_HOST         ON  CACHE BOOL "" FORCE)
set (CN_WITH_MBEDTLS ON  CACHE BOOL "" FORCE)
# ClassicNet's own test suite runs from its own build dir (the 13/13 gate,
# driven by scripts/test-classicnet-n1.sh), not inside the lagrange ctest.
set (CN_BUILD_TESTS  OFF CACHE BOOL "" FORCE)

add_subdirectory ("${CLASSICNET_DIR}")

# Conservative wire floor (plan T-1 lesson): pin TLS 1.2 so the fetchers avoid
# the TLS-1.3 alert-bug path. mbedTLS host 3.6 still offers the modern path in
# the seam; the on-target flavors carry the same define.
if (TARGET classicnet)
    target_compile_definitions (classicnet PRIVATE CN_TLS_FORCE_TLS12=1)
endif ()

# The the_Foundation Seam (TFDN_CLASSICNET) compiles socket.c/tlsrequest.c to
# drive a ClassicNet CNTransport; attach the classicnet static lib + its PUBLIC
# usage requirements (CN_HOST/CN_WITH_DARWIN8 defines, include dir) so the_Foundation's
# classicnet sources compile. Done here because the classicnet target is defined by
# add_subdirectory above.
if (TARGET the_Foundation AND TARGET classicnet)
    target_link_libraries (the_Foundation PUBLIC classicnet)
endif ()

# N1 host smoke: a real Gemini fetch over cn_darwin8 + cn_tls. Driven by
# scripts/test-classicnet-n1.sh against a local TLS Gemini test server.
add_executable (gmclassicnet_smoke tests/classicnet/gmclassicnet_smoke.c)
set_property (TARGET gmclassicnet_smoke PROPERTY C_STANDARD 11)
target_link_libraries (gmclassicnet_smoke PRIVATE classicnet)
# classicnet sets its sanitizers via directory-scoped add_compile_options, which
# do not reach a target defined in the parent dir; the smoke executable must
# carry its own so the ASan/UBSan runtime is linked at the final link.
if (CN_HOST)
    target_compile_options (gmclassicnet_smoke PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    target_link_options (gmclassicnet_smoke PRIVATE
        -fsanitize=address,undefined)
endif ()

# N2 host unit test: the ClassicNet-backed iSocket. Links the_Foundation (now
# built with the classicnet socket backend via TFDN_CLASSICNET) + classicnet.
add_executable (t_classicnet_socket tests/classicnet/t_classicnet_socket.c)
set_property (TARGET t_classicnet_socket PROPERTY C_STANDARD 11)
target_link_libraries (t_classicnet_socket PRIVATE the_Foundation::the_Foundation)
if (CN_HOST)
    target_compile_options (t_classicnet_socket PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    target_link_options (t_classicnet_socket PRIVATE
        -fsanitize=address,undefined)
endif ()

enable_testing ()
add_test (NAME classicnet_n1
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test-classicnet-n1.sh"
            "$<TARGET_FILE:gmclassicnet_smoke>")
add_test (NAME classicnet_socket
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test-classicnet-socket.sh"
            "$<TARGET_FILE:t_classicnet_socket>")
