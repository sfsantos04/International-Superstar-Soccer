#!/usr/bin/env bash
# build_ios.sh - runs on a macOS runner (GitHub Actions macos-14/15 or a Mac).
# Produces build-ios/ISSDNative.ipa (unsigned) ready for SideStore/ESign.
#
#   ./ios/build_ios.sh [/path/to/issd-native]
#
set -euo pipefail
ROOT="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
cd "$ROOT"
IOS="$ROOT/ios"
BUILD="$ROOT/build-ios"
PREFIX="$ROOT/sdl2-ios"
SDL_VER="2.32.10"
SDL_TGZ="SDL2-$SDL_VER.tar.gz"
SDL_URL="https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/$SDL_TGZ"

echo "== 1/4 SDL2 $SDL_VER para iOS"
if [ ! -f "$PREFIX/lib/libSDL2.a" ]; then
  [ -f "/tmp/$SDL_TGZ" ] || curl -sSL -o "/tmp/$SDL_TGZ" "$SDL_URL"
  rm -rf "/tmp/SDL2-$SDL_VER" && tar xzf "/tmp/$SDL_TGZ" -C /tmp
  cmake -S "/tmp/SDL2-$SDL_VER" -B "/tmp/sdl2-ios-build" \
        -DCMAKE_TOOLCHAIN_FILE="$IOS/ios.toolchain.cmake" \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DCMAKE_BUILD_TYPE=Release \
        -DSDL_STATIC=ON -DSDL_SHARED=OFF -DSDL_TEST=OFF \
        -DSDL_TESTS=OFF >/dev/null
  cmake --build "/tmp/sdl2-ios-build" -j"$(sysctl -n hw.ncpu)" >/dev/null
  cmake --install "/tmp/sdl2-ios-build" >/dev/null
fi

echo "== 2/4 configurando o projeto"
rm -rf "$BUILD"
cmake -S "$ROOT" -B "$BUILD" \
      -DCMAKE_TOOLCHAIN_FILE="$IOS/ios.toolchain.cmake" \
      -DCMAKE_PREFIX_PATH="$PREFIX" \
      -DCMAKE_BUILD_TYPE=Release \
      -DSDL2_INCLUDE_DIR="$PREFIX/include/SDL2" \
      -DSDL2_LIBRARY="$PREFIX/lib/libSDL2.a" \
      -DCMAKE_EXE_LINKER_FLAGS="-framework UIKit -framework OpenGLES -framework AVFoundation -framework CoreHaptics -framework GameController -framework CoreMotion -framework QuartzCore -framework CoreGraphics -framework AudioToolbox -framework CoreAudio -framework Metal -framework CoreVideo -liconv -lz"

echo "== 3/4 compilando"
cmake --build "$BUILD" -j"$(sysctl -n hw.ncpu)"

echo "== 4/4 empacotando o .ipa"
APP="$BUILD/Payload/ISSDNative.app"
rm -rf "$BUILD/Payload"
mkdir -p "$APP"
cp "$BUILD/ISSDNative" "$APP/ISSDNative" 2>/dev/null || cp "$BUILD"/ISSDNative.app/ISSDNative "$APP/ISSDNative"
cp "$IOS/Info.plist" "$APP/Info.plist"
[ -f "$ROOT/recomp/aot_boot_deny.txt" ] && cp "$ROOT/recomp/aot_boot_deny.txt" "$APP/"
[ -f "$ROOT/issd_config.json" ] && cp "$ROOT/issd_config.json" "$APP/"
[ -d "$ROOT/mods" ] && cp -R "$ROOT/mods" "$APP/"
chmod 755 "$APP/ISSDNative"
( cd "$BUILD" && rm -f ISSDNative.ipa && zip -qry ISSDNative.ipa Payload )
echo "ipa: $BUILD/ISSDNative.ipa"
ls -lh "$BUILD/ISSDNative.ipa"
