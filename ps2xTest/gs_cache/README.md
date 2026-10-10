# GS backend tests

This standalone CMake package compiles the actual GS CPU backend, frontend and
memory implementation. It covers texture, CLUT and memory caches, plus the pixel
pipeline's depth-test behavior. It uses synthetic VRAM and public backend APIs;
no game assets, generated guest code or runtime substitutes are included.

```sh
cmake -S ps2xTest/gs_cache -B build/gs-cache
cmake --build build/gs-cache --config RelWithDebInfo
ctest --test-dir build/gs-cache -C RelWithDebInfo --output-on-failure
```

On Windows, configure with `-G "Visual Studio 17 2022" -A x64` and, to reproduce
the native MSVC check, add
`-DCMAKE_CXX_FLAGS_RELWITHDEBINFO="/O2 /Ob1 /DNDEBUG /Zi /fp:strict"`.
`PS2_GS_RUNTIME_DIR` can select another runtime source tree; the default is this
repository's `ps2xRuntime`. GCC/Clang sanitizers are optional through
`PS2_GS_CACHE_SANITIZERS=ON`.

The six `gs_cache.depth.*` cases cover:

- A synthetic 640x448 texture drawn with the observed opening-movie register and
  field-sprite state: `TEST=0`, RGB DECAL, zero vertex color and two 224-line fields.
- Every stored ZTST method with ZTE disabled, including preservation of unmasked Z.
- Enabled NEVER, ALWAYS, GEQUAL and GREATER, including equality boundaries.
- ZBUF write masking and alpha-failure ZB_ONLY with disabled, rejected and enabled Z.

Filter these cases with `ctest --test-dir build/gs-cache -C RelWithDebInfo
-R gs_cache.depth --output-on-failure`. The package does not exercise the complete
runtime, movie decoding or presentation, and does not establish retail gameplay
or Linux validation.
