# Android arm64-v8a toolchain wrapper.
#
# Wraps the NDK toolchain to fix two problems when building the vendored
# dependencies (which configure themselves as nested ExternalProjects):
#
#  1. The nested sub-builds inherit the toolchain file but not ANDROID_ABI /
#     ANDROID_PLATFORM, so they fall back to the NDK default (armeabi-v7a).
#     Pinning them here makes every sub-build target arm64-v8a consistently.
#
#  2. Newer NDK lld defaults to --no-undefined-version and, together with
#     --fatal-warnings, turns zlib's version script (zlib.map references
#     gz_intmax, which is not emitted) into a hard link error. Appending
#     --undefined-version restores the tolerant behaviour for shared/module
#     libraries.

if(NOT DEFINED ANDROID_NDK)
  if(DEFINED ENV{ANDROID_NDK_HOME})
    set(ANDROID_NDK "$ENV{ANDROID_NDK_HOME}")
  elseif(DEFINED ENV{ANDROID_NDK_LATEST_HOME})
    set(ANDROID_NDK "$ENV{ANDROID_NDK_LATEST_HOME}")
  endif()
endif()

set(ANDROID_ABI arm64-v8a)
set(ANDROID_PLATFORM android-24)

include("${ANDROID_NDK}/build/cmake/android.toolchain.cmake")

string(APPEND CMAKE_SHARED_LINKER_FLAGS_INIT " -Wl,--undefined-version")
string(APPEND CMAKE_MODULE_LINKER_FLAGS_INIT " -Wl,--undefined-version")

# Disable _FORTIFY_SOURCE for the Android build. The NDK enables =2 by default,
# whose runtime checks abort the process on benign out-of-bounds reads in the old
# vendored C code (Lua 5.1's GC trips __strchr_chk during InitLua). The NDK's
# legacy toolchain (the default) puts -D_FORTIFY_SOURCE=2 directly into
# CMAKE_C_FLAGS, not the *_INIT seeds, so append our override to CMAKE_C_FLAGS
# itself, after the NDK include, so =0 is the last definition on the command line.
# This flows to every sub-build, which re-processes this toolchain file.
string(APPEND CMAKE_C_FLAGS " -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0")
string(APPEND CMAKE_CXX_FLAGS " -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0")
