# ios.toolchain.cmake - CMake toolchain for building ISSDNative for iOS.
#
# Usage (on a macOS runner with Xcode):
#   cmake -S . -B build-ios \
#         -DCMAKE_TOOLCHAIN_FILE=$PWD/ios/ios.toolchain.cmake \
#         -DCMAKE_PREFIX_PATH=/path/to/sdl2-ios \
#         -DCMAKE_BUILD_TYPE=Release
#
set(CMAKE_SYSTEM_NAME iOS)
set(CMAKE_SYSTEM_VERSION 16.0)
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "")
set(CMAKE_OSX_SYSROOT iphoneos CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET 16.0 CACHE STRING "")

# Cross-compiling: never pick up host libraries.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_C_FLAGS_INIT "-fembed-bitcode-marker")
set(CMAKE_CXX_FLAGS_INIT "-fembed-bitcode-marker")
