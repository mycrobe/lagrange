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
project(LagrangeOSX C OBJC)
set(CMAKE_C_STANDARD 90)
set(CMAKE_C_STANDARD_REQUIRED ON)

# The proven flag set (Starscape T-1 d8_get on-device): -O2 -Wall, the
# CN/TLS/userconfig defines, the include set, -lresolv, and -Wl,-force_cpusubtype_ALL
# (ppc_970 emutls trap) on every link line.
set(CMAKE_C_FLAGS "-O2 -Wall")
# The Aqua host is ObjC (.m).  gnu99 so C99 for-loops in the .m bodies work;
# -fobjc-exceptions for @try/@catch around AppKit UTF8String/menu assembly.
set(CMAKE_OBJC_FLAGS "-O2 -Wall -fobjc-exceptions -std=gnu99")

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

# NOTE: the CarbonCore/MACCORE include dir must NOT be a project-wide
# include_directories() -- CarbonCore ships its own resources.h, which would
# shadow lagrange's src/resources.h for the widget kit.  Attach MACCORE only to
# the targets that actually pull CarbonCore types (cn_d8 + the_Foundation), as
# PUBLIC usage requirements, so it lands AFTER the app's own src/ include path.

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
    ${MBEDTLS_D8_ROOT}/include
    ${MACCORE})
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

# zlib: the_Foundation finds it via pkg-config (missing in the cross container),
# so its archive.h only declares iArchive when iHaveZlib is set.  Tiger ships
# zlib in libSystem (/usr/lib/libz.dylib) and the SDK has zlib.h, so force the
# define and resolve the symbols with -lz at the app link (see the L4 target
# below).  Depends.cmake only ever sets iHaveZlib=YES (never NO), so this cache
# value survives its ZLIB_FOUND check.  The L4 target adds -lz deliberately.
set (iHaveZlib YES CACHE BOOL "zlib from libSystem (Tiger)" FORCE)

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

    # the_Foundation's tfdn_link_depends() adds PCRE1's include dirs but only
    # PCRE2's *libs* (its include-dir block omits PCRE2_INCLUDE_DIRS), so
    # regexp.c can't find pcre2.h.  Add them here (build-side workaround; the
    # clean fix belongs in the the_Foundation fork, see docs/arcana.md).
    target_include_directories (the_Foundation PUBLIC ${PCRE2_INCLUDE_DIRS})
    target_link_directories (the_Foundation PUBLIC ${PCRE2_LIBRARY_DIRS})
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

# --- the_Foundation archive-read proof (darwin8): opens resources.lgr on-device
#     and drains an entry, proving the big-endian iFile/iArchive ZIP path. This
#     is the minimal reproducer for the Tiger resource-loading blocker. ---
add_executable (d8_archive_smoke d8_archive_smoke.c)
set_property (TARGET d8_archive_smoke PROPERTY C_STANDARD 11)
target_link_libraries (d8_archive_smoke PRIVATE
    the_Foundation::the_Foundation
    -lz
    -lresolv
    -Wl,-force_cpusubtype_ALL
)

# --- Aqua (Tiger/Leopard PPC) canvas host -> L4.app ----------------------
# The T-tier UI: a single programmatic AppKit window (aquaview.m) over the same
# portable portable core + widget kit + software-framebuffer shim (sdlcompat.c)
# the canvaswin target uses, with the native menu bar (canvasmenu_impl_SDL.m).
# This is the "canvas host" pattern from PLAN.md Phase 0/2 -- the widget kit is
# untouched; only the window/event backend is AppKit instead of SDL2.
set (AQUA_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/bookmarks.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/color.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/export.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/feeds.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/fontpack.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gempub.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gmcerts.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gmdocument.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gmrequest.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gmtypesetter.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gmutil.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/gopher.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/guppy.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/history.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/lang.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/lookup.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/media.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/mimehooks.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/misfin.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/periodic.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/prefs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/resources.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sitespec.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/snippets.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/updater.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/visited.c
    # rendering (STB text; the fontpack stack, no FreeType)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/render/attributedtext.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/render/font.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/render/paint.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/render/text.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/render/text_stb.c
    # user interface (widget kit, unchanged)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/banner.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/command.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/documentview.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/gamepad.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/keys.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/linkinfo.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/mediaui.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/metrics.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/mobile.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/root.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/translation.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/util.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/visbuf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/window.c
    # widgets
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/widget.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/bindingswidget.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/certimportwidget.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/certlistwidget.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/documentwidget.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/indicatorwidget.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/inputwidget.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/keyboardwidget.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/labelwidget.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/listwidget.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/lookupwidget.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/scrollwidget.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/sidebarwidget.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/snippetwidget.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/uploadwidget.c
    # audio
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/buf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/player.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/stb_vorbis.c
    # canvas shim: software framebuffer + the SDL-surface the widget kit calls
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/canvas/sdlcompat.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/canvas/touch_stub.c
    # portable native-menu contract: 10.4-era AppKit NSMenu bar (src/macos/
    # canvasmenu_impl_aqua.m).  The Aqua host runs the AppKit main loop
    # ([NSApp run] + a timer stepping the widget kit), so the native menu bar
    # actually renders; the backend builds the app + Window menus the widget kit
    # leaves to the OS.  Also the Aqua window backend + main.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/macos/canvasmenu_impl_aqua.m
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/macos/aquaview.m
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/macos/aquamain.c
)
# dev-build identity: describe the flavor + carry the HEAD short hash
execute_process (
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE LAG_DEV_GIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
# Where the app finds resources.lgr at runtime (baked into LAGRANGE_EMB_BIN).
set (AQUA_EMB_BIN "resources.lgr" CACHE STRING
    "resources.lgr path baked into the Aqua host (absolute for a deployed .app)")
add_executable (L4 ${AQUA_SOURCES})
set_property (TARGET L4 PROPERTY C_STANDARD 11)
set_property (TARGET L4 PROPERTY C_STANDARD_REQUIRED ON)
target_compile_definitions (L4 PRIVATE
    CN_WITH_DARWIN8=1
    CN_WITH_MBEDTLS=1
    CN_TLS_FORCE_TLS12=1
    "MBEDTLS_USER_CONFIG_FILE=\"d8_userconfig.h\""
    # dev-build identity (AGENTS.md version-string policy)
    "LAGRANGE_APP_VERSION=\"1.21.1-dev+L4-tiger (${LAG_DEV_GIT_SHA})\""
    LAGRANGE_CANVAS=1
    # Native AppKit NSMenu bar (src/macos/canvasmenu_impl_aqua.m).  The Aqua host
    # runs [NSApp run] (aquaview.m) so the native bar renders; the widget kit
    # builds it via window.c insertMacMenus_ and stops drawing the in-window
    # menubar (LAGRANGE_MENUBAR is not defined).
    LAGRANGE_NATIVE_MENU=1
    # Resource blob path.  Default is an exec-relative "resources.lgr" name (the
    # widget kit joins execPath + EMB_BIN), but app.c's execPath *includes* the
    # binary name so "./L4/../resources.lgr" is ENOTDIR -- deployed bundles
    # need an absolute path here (set -DAQUA_EMB_BIN=...).  The host canvas uses
    # an absolute path too; see docs/arcana.md.
    "LAGRANGE_EMB_BIN=\"${AQUA_EMB_BIN}\""
    LAGRANGE_ENABLE_AUDIO=1
    LAGRANGE_ENABLE_STB_TRUETYPE=1
    LAGRANGE_HAVE_SDL_TEXTEDITING=1
    # desktop widget-kit behaviour + the darwin8 shim selector
    iPlatformDesktop=1
    iPlatformPcDesktop=1
    DARWIN8=1
    # Release like the host canvas regression gate: several widget-kit blocks
    # (renderer-info texture formats, debug theme-seed shortcut) are #if !NDEBUG.
    NDEBUG=1
)
target_include_directories (L4 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/canvas
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/canvas/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/macos
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CLASSICNET_DIR}/include
    ${CLASSICNET_DIR}/target
    ${MBEDTLS_D8_ROOT}/include
    ${MACCORE}
    ${CMAKE_CURRENT_SOURCE_DIR}/darwin8_sdk_shim
)
# the shim's clock_gettime() (missing on Tiger) etc. must be visible to the app
# sources too (sdlcompat.c), so force-include the darwin8 POSIX shim throughout.
target_compile_options (L4 PRIVATE
    -include ${CMAKE_CURRENT_SOURCE_DIR}/darwin8_posix_shim.h
    -Werror=implicit-function-declaration
    -Wno-deprecated-declarations
    -DSTB_VORBIS_NO_STDIO=1
    -DSTB_VORBIS_NO_INTEGER_CONVERSION=1
)
# resolve the widget kit's stb_vorbis comparison warning
set_source_files_properties (${CMAKE_CURRENT_SOURCE_DIR}/../src/audio/stb_vorbis.c PROPERTIES
    COMPILE_OPTIONS "-Wno-tautological-compare")
target_link_libraries (L4 PRIVATE
    the_Foundation::the_Foundation
    -lresolv
    -lz                 # zlib from libSystem (see the iHaveZlib note above)
    "-framework Foundation"
    "-framework AppKit"
    -Wl,-force_cpusubtype_ALL
)
# pcre2-8 is a transitive the_Foundation dependency; give the linker its lib dir.
target_link_directories (L4 PRIVATE ${PCRE2_LIBRARY_DIRS})

# --- L4.app bundle, assembled in POST_BUILD (no nibs, no Xcode; the
#     minimal-plist shape proven in starscape T-0). ---
set (AQUA_BUNDLE ${CMAKE_CURRENT_BINARY_DIR}/L4.app)
set (AQUA_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../build-canvas/resources.lgr)
if (NOT EXISTS ${AQUA_RESOURCES})
    set (AQUA_RESOURCES ${CMAKE_CURRENT_BINARY_DIR}/resources.lgr)
endif ()
add_custom_command (TARGET L4 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${AQUA_BUNDLE}/Contents/MacOS
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist
            ${AQUA_BUNDLE}/Contents/Info.plist
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/PkgInfo
            ${AQUA_BUNDLE}/Contents/PkgInfo
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:L4>
            ${AQUA_BUNDLE}/Contents/MacOS/L4
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${AQUA_RESOURCES}
            ${AQUA_BUNDLE}/Contents/MacOS/resources.lgr
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${AQUA_RESOURCES}
            ${AQUA_BUNDLE}/Contents/resources.lgr
    COMMENT "assembling L4.app")

