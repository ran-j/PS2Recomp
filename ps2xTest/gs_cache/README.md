# GS backend tests

This standalone package compiles the actual CPU backend, frontend and local
memory implementation. It uses synthetic VRAM and public GS APIs, with no game
assets, generated guest functions or runtime substitutes.

```sh
cmake -S ps2xTest/gs_cache -B build/gs-cache
cmake --build build/gs-cache --config RelWithDebInfo
ctest --test-dir build/gs-cache -C RelWithDebInfo --output-on-failure
```

For native Windows MSVC, configure with `-G "Visual Studio 17 2022" -A x64` and
`-DCMAKE_CXX_FLAGS_RELWITHDEBINFO="/O2 /Ob1 /DNDEBUG /Zi /fp:strict"`.
`PS2_GS_RUNTIME_DIR` selects another runtime source checkout; the default is
this repository's `ps2xRuntime`. Optional GCC/Clang sanitizers use
`PS2_GS_CACHE_SANITIZERS=ON`.

The two `gs_cache.sprite.*` cases add sprite coverage and texture alignment to
the 45 existing texture, CLUT and memory-cache cases:

- `coordinates` uses an independent scalar interpolation oracle across all 16
  UV fractional nibbles, nearest and bilinear filtering, reversed X/Y axes,
  fractional XYOFFSET, clipping, integer pixel sample locations, ceil-exclusive
  bounds, second-vertex flat Q/color, degenerate sprites and untextured bounds.
- `feedback` draws four reversed 64-pixel CT32 identity strips through the real
  shared single-page texture cache without intervening TEXFLUSH. Every pixel
  stays unchanged and every tested boundary retains the original gradient.

Bilinear color checks permit one unit for floating-point operation-order
rounding. Coverage, untouched pixels and feedback results are exact. Identity
feedback has the same expected pixels for any valid cache-invalidation policy;
the test does not impose a full-frame snapshot rule on arbitrary feedback.

The fixture sets TEST.ZTE=1/ZTST=ALWAYS and ZMSK=1 to isolate sprite alignment
from the separate disabled-depth-test issue. This change does not include a
depth-pipeline fix. This upstream backend is synchronous: the package does not
claim to test the private runtime's asynchronous raster workers.

Coordinate rules are supported by pinned primary PCSX2 implementations:
[vertex conversion](https://github.com/PCSX2/pcsx2/blob/355608952714678b3c57832fb82dc6a42956ed25/pcsx2/GS/Renderers/SW/GSRendererSW.cpp#L219)
preserves fractional XYOFFSET/UV and supplies the second vertex's Q for sprites;
[sprite rasterization](https://github.com/PCSX2/pcsx2/blob/355608952714678b3c57832fb82dc6a42956ed25/pcsx2/GS/Renderers/SW/GSRasterizer.cpp#L1088)
sorts the matching texture component per axis, uses ceil-exclusive bounds and
integer pixel sample locations. Linear sampling applies its separate half-texel
offset.

Native Windows MSVC 19.44 validation passed all 47 CTest entries with
RelWithDebInfo `/O2 /fp:strict`. The two alignment cases passed 853,552 checks
with zero failures, zero changed feedback pixels and zero artificial boundary
steps. Compiling the same fixture against the unchanged main-branch backend
failed 38,810 coordinate checks and 8,029 feedback checks, including 7,812 altered
pixels and 217 extra boundary steps. This control changed only the backend
source under test, keeping the fixture and compiler configuration identical.

Filter new cases with `ctest --test-dir build/gs-cache -C RelWithDebInfo
-R gs_cache.sprite --output-on-failure`. Full runtime, retail gameplay and Linux
validation are outside this package. The fixture is GPL-3.0-only, like the
linked runtime; see the repository license.
