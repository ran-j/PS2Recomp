#!/usr/bin/env bash
# Setup of the PS Vita GL toolchain that the PS2 runtime needs.
#
# Installs, into $VITASDK, the full PowerVR OpenGL ES stack the runtime's raylib
# backend depends on, plus stages the runtime .suprx modules into the repo so the
# vpk can bundle them. Run this ONCE per machine (idempotent; safe to re-run).
#
#   Prerequisites: VitaSDK installed with $VITASDK exported, plus curl/unzip/cmake.
#   Usage: ./vita/setup.sh
#
# Stack::
#   PVR_PSP2 v3.9   : the PowerVR SGX driver modules + EGL/GLES headers/stubs
#   gl4es4vita 1.1.4: desktop-GL-over-GLES translation (libGL.suprx) + stubs
#   SDL2 + PVR      : SDL2 2.32.2 rebuilt with -DVIDEO_VITA_PVR=ON (GL support)
#   raylib 5.5      : Quenom's SDL2-based Vita port (raylib itself)

set -euo pipefail

fail() { echo "error: $*" >&2; exit 1; }

[ -n "${VITASDK:-}" ] || fail "VITASDK is not set. 'export VITASDK=/usr/local/vitasdk' first."
[ -f "$VITASDK/share/vita.toolchain.cmake" ] || fail "missing $VITASDK/share/vita.toolchain.cmake"
command -v curl  >/dev/null || fail "curl not found"
command -v unzip >/dev/null || fail "unzip not found"
command -v cmake >/dev/null || fail "cmake not found"

export PATH="$VITASDK/bin:$PATH"
PREFIX="$VITASDK/arm-vita-eabi"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODULE_DEST="$REPO_ROOT/ps2xRuntime/vita/module"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$MODULE_DEST"

PVR_VER=3.9
GL4ES_VER=1.1.4
SDL_VER=2.32.2

echo "==> [1/4] PVR_PSP2 v$PVR_VER (driver modules, stubs, headers)"
cd "$WORK"
curl -sfL "https://github.com/GrapheneCt/PVR_PSP2/archive/refs/tags/v${PVR_VER}.tar.gz" -o pvr_src.tar.gz
curl -sfL "https://github.com/GrapheneCt/PVR_PSP2/releases/download/v${PVR_VER}/vitasdk_stubs.zip" -o pvr_stubs.zip
curl -sfL "https://github.com/GrapheneCt/PVR_PSP2/releases/download/v${PVR_VER}/PSVita_Release.zip" -o pvr_mods.zip
tar xzf pvr_src.tar.gz
mkdir -p pvr_stubs pvr_mods
unzip -oq pvr_stubs.zip -d pvr_stubs
unzip -oq pvr_mods.zip -d pvr_mods
# stub archives (the zip nests each stub inside a dir named *.a — take the files)
find pvr_stubs -type f -name '*.a' -exec install -D -t "$PREFIX/lib/" {} +
# headers
for d in EGL GLES GLES2 KHR; do
    install -d "$PREFIX/include/$d"
    install -D -t "$PREFIX/include/$d/" "PVR_PSP2-${PVR_VER}/include/$d/"*.h
done
install -D -t "$PREFIX/include/gpu_es4/" "PVR_PSP2-${PVR_VER}/include/gpu_es4/psp2_pvr_hint.h"
# runtime modules -> repo (bundled into the vpk at app0:module/)
find pvr_mods -name '*.suprx' -exec cp -v {} "$MODULE_DEST/" \;

echo "==> [2/4] gl4es4vita v$GL4ES_VER (libGL.suprx + gl4es stubs/headers)"
cd "$WORK"
GL4ES_BASE="https://github.com/SonicMastr/gl4es4vita/releases/download/v${GL4ES_VER}-vita"
curl -sfL "$GL4ES_BASE/include.zip"        -o gl_inc.zip
curl -sfL "$GL4ES_BASE/vitasdk_stubs.zip"  -o gl_stubs.zip
curl -sfL "$GL4ES_BASE/PSVita_Release.zip" -o gl_mods.zip
mkdir -p gl_inc gl_stubs gl_mods
unzip -oq gl_inc.zip   -d gl_inc
unzip -oq gl_stubs.zip -d gl_stubs
unzip -oq gl_mods.zip  -d gl_mods
find gl_inc   -name '*.h' | while read -r h; do install -D "$h" "$PREFIX/include/${h#gl_inc/}"; done
find gl_stubs -type f -name '*.a' -exec install -D -t "$PREFIX/lib/" {} +
find gl_mods  -name '*.suprx' -exec cp -v {} "$MODULE_DEST/" \;

echo "==> [3/4] SDL2 $SDL_VER with -DVIDEO_VITA_PVR=ON"
cd "$WORK"
curl -sfL "https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VER}/SDL2-${SDL_VER}.tar.gz" -o sdl.tar.gz
tar xzf sdl.tar.gz
[ -f "$PREFIX/lib/libSDL2.a.bakgxm" ] || cp "$PREFIX/lib/libSDL2.a" "$PREFIX/lib/libSDL2.a.bakgxm" 2>/dev/null || true
mkdir -p "SDL2-${SDL_VER}/build"
cd "SDL2-${SDL_VER}/build"
cmake .. -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
         -DCMAKE_INSTALL_PREFIX="$PREFIX" -DVIDEO_VITA_PVR=ON >/dev/null
make -j"$(nproc)" >/dev/null
make install >/dev/null

echo "==> [4/4] raylib 5.5 (Quenom SDL2 Vita port)"
cd "$WORK"
[ -f "$PREFIX/lib/libraylib.a.bak42" ] || cp "$PREFIX/lib/libraylib.a" "$PREFIX/lib/libraylib.a.bak42" 2>/dev/null || true
git clone --depth 1 https://github.com/Quenom/raylib-5.5-vita raylib
make -C raylib/src PLATFORM=PLATFORM_VITA -j"$(nproc)" >/dev/null
make -C raylib/src install >/dev/null

echo ""
echo "==> setup complete. Modules staged in ps2xRuntime/vita/module/:"
ls "$MODULE_DEST"
echo ""
echo "Now build the vpk with:  ./vita/build.sh <boot-elf-path-on-vita>"
