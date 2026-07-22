#!/usr/bin/env bash
# Builds the PS2 runtime (ps2xIOP + ps2xRuntime + eboot.bin/vpk) for PS Vita.
#
# Usage:
#   ./vita/build.sh [boot_elf_path_on_vita]
#
# Environment overrides:
#   PS2X_BOOT_ELF   guest ELF path baked into the runner (default: ux0:data/ps2x/game.elf)
#   BUILD_DIR       build directory (default: <repo>/outvita)
#   CLEAN=1         wipe the build directory before configuring
#
# Prerequisites (run ./vita/setup.sh once to satisfy all of these):
#   - VitaSDK installed with $VITASDK exported (https://vitasdk.org)
#   - The PVR GL stack: PVR_PSP2 + gl4es4vita + SDL2(PVR) + raylib 5.5
#   - ffmpeg vita libs available via vdpm (libavcodec etc.)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/outvita}"
BOOT_ELF="${1:-${PS2X_BOOT_ELF:-ux0:data/ps2x/game.elf}}"

fail() {
    echo "error: $*" >&2
    exit 1
}

# --- checks -----------------------------------------------------------

[ -n "${VITASDK:-}" ] || fail "VITASDK is not set. Install VitaSDK and 'export VITASDK=/usr/local/vitasdk'."
[ -f "$VITASDK/share/vita.toolchain.cmake" ] || fail "missing $VITASDK/share/vita.toolchain.cmake"
[ -f "$VITASDK/share/vita.cmake" ] || fail "missing $VITASDK/share/vita.cmake"
command -v cmake >/dev/null || fail "cmake not found (sudo apt install cmake)"
command -v arm-vita-eabi-gcc >/dev/null || fail "arm-vita-eabi-gcc not in PATH (add \$VITASDK/bin to PATH)"

[ -f "$VITASDK/arm-vita-eabi/include/SDL2/SDL.h" ] ||
    fail "SDL2 headers missing in VitaSDK. Run ./vita/setup.sh first."
[ -f "$VITASDK/arm-vita-eabi/include/gpu_es4/psp2_pvr_hint.h" ] ||
    fail "PVR headers missing (SDL2 would build without GL). Run ./vita/setup.sh first."

if command -v arm-vita-eabi-pkg-config >/dev/null &&
    ! arm-vita-eabi-pkg-config --exists libavcodec 2>/dev/null; then
    echo "warning: libavcodec not found via arm-vita-eabi-pkg-config; if configure fails, run: vdpm ffmpeg" >&2
fi

if [ ! -f "$REPO_ROOT/ps2xRuntime/src/runner/register_functions.cpp" ]; then
    echo "warning: ps2xRuntime/src/runner/ has no recompiled game code; the runner will link without a game." >&2
fi

case "$REPO_ROOT" in
/mnt/*)
    echo "warning: building under /mnt/ (Windows filesystem) is slow in WSL; consider cloning into the Linux filesystem." >&2
    ;;
esac

# --- configure ---------------------------------------------------------------

if [ "${CLEAN:-0}" = "1" ]; then
    rm -rf "$BUILD_DIR"
fi

# A cache configured on another machine/OS (e.g. the Windows-side outvita) is unusable
if [ -f "$BUILD_DIR/CMakeCache.txt" ] &&
    ! grep -q "CMAKE_HOME_DIRECTORY:INTERNAL=$REPO_ROOT\$" "$BUILD_DIR/CMakeCache.txt"; then
    echo "stale CMake cache from another environment detected; recreating $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

GENERATOR_ARGS=()
if command -v ninja >/dev/null; then
    GENERATOR_ARGS=(-G Ninja)
fi

echo "==> configuring (boot ELF: $BOOT_ELF)"
cmake -S "$REPO_ROOT" -B "$BUILD_DIR" "${GENERATOR_ARGS[@]}" \
    -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DPS2X_BUILD_RECOMP=OFF \
    -DPS2X_BUILD_ANALYZER=OFF \
    -DPS2X_BUILD_TEST=OFF \
    -DPS2X_BUILD_STUDIO=OFF \
    -DPS2X_ENABLE_SCCACHE=OFF \
    -DPS2X_DEFAULT_BOOT_ELF="$BOOT_ELF"

# --- build -------------------------------------------------------------------

echo "==> building runtime"
cmake --build "$BUILD_DIR" -j"$(nproc)"

# --- report ------------------------------------------------------------------

VPK="$(find "$BUILD_DIR" -name '*.vpk' -print -quit)"
if [ -n "$VPK" ]; then
    echo ""
    echo "==> done: $VPK"
    echo "    install it with VitaShell, then push the game files to ux0:data/... (keep the ELF's real name)"
else
    echo ""
    echo "==> build finished, but no .vpk was produced (PS2X_VITA_CREATE_PACKAGE may be OFF)"
fi
