#!/usr/bin/env bash
# Cross-compiles the Windows release with MinGW-w64 and packs Xpilot-TNG_windows.zip:
# statically linked .exe files + lib/ (maps, assets), no DLLs needed, relocatable.
# Needs: x86_64-w64-mingw32-gcc-posix / g++-posix (Debian/Ubuntu: g++-mingw-w64-x86-64-posix),
#        cmake, git, zip, network access on the first run (SDL3, SDL3_ttf, opus, zlib, expat).
# Usage: ./build_win.sh            (BUILD_DIR=build-win by default)
set -euo pipefail
cd "$(dirname "$0")"

BUILD_DIR=${BUILD_DIR:-build-win}
NAME=Xpilot-TNG
OUT=Xpilot-TNG_windows.zip
DIST=$BUILD_DIR/dist

[ -f "$BUILD_DIR/CMakeCache.txt" ] ||
    cmake -B "$BUILD_DIR" -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake \
          -DCMAKE_BUILD_TYPE=Release >/dev/null
rm -rf "$DIST" "$BUILD_DIR/$NAME"
cmake --build "$BUILD_DIR" -j"$(nproc)" --target dist

x86_64-w64-mingw32-strip "$DIST"/*.exe
cp -f COPYING "$DIST/COPYING.txt"
cp -f README.md "$DIST/"
mv "$DIST" "$BUILD_DIR/$NAME"
rm -f "$OUT"
(cd "$BUILD_DIR" && zip -qr9 "../$OUT" "$NAME")
echo "Created $OUT ($(du -h "$OUT" | cut -f1))"
