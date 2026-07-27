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
