# Burnout 3 USA (SLUS_210.50): recompilation roadmap

Branch: `Burnout3` (local; push only compliant content). All paths are relative to the
PS2Recomp repo root. Completed history is in `COMPLETED_WORK.md`.

Ground rules:
- Never track game files or game-derived data: ELF, disc contents, IRX modules, Ghidra
  project, function maps, reports, logs or generated C++. `Burnout3_recomp/.gitignore`
  enforces this.
- Use your own disc only. Never download game files or a BIOS.
- Don't modify the original ELF, the ISO, or existing PS2Recomp source.

## Status

| Step | What | Status |
|---|---|---|
| A | Disc files available to the runtime | Done |
| B | Generated code integrated into `ps2xRuntime` | Done |
| C | Build `ps2EntryRunner` | Next, not started |
| D | First boot and bring-up loop | Pending |

## A. Disc files for the runtime (done)

**Why:** the generated C++ holds only the game's code. At runtime the code still reads
`cdrom0:\…` paths for assets, audio, video and the IRX modules that `ps2xIOP` executes.
The runtime maps `cdrom0:` to the folder containing the launched ELF, and puts memory-card
saves in `mc0/` next to it.

**Layout** (all ignored by `/disc*/`; nothing is copied, nothing outside PS2Recomp is written):
- `Burnout3_recomp/disc_image/`: your ISO mounted read-only.
- `Burnout3_recomp/disc/`: the launch folder, containing:
  - relative symlinks into `disc_image/`
  - `SLUS_210.50` as an APFS clone (a real file, so `mc0/` resolves inside `disc/`, not on the read-only mount)
  - a writable `mc0/`

The mount does not survive a reboot. Attach it before step D:

```bash
hdiutil attach -readonly -nobrowse -mountpoint "$PWD/Burnout3_recomp/disc_image" "<path to your Burnout 3 ISO>"
```

```bash
hdiutil detach Burnout3_recomp/disc_image
```

**Open risks:**
- Untested whether the runtime follows the symlinks. The fallback is copying the disc into
  `disc/` (2.7 GB, still ignored).
- The disc ships `IOP/IOPRP280.IMG` (IOP reboot image) and `IOP/DNAS280.IMG` (online
  authentication). `ps2xIOP` support for either is unverified.

## B. Runtime integration (done)

- **Code:** the 71,113 `.cpp` files from `Burnout3_recomp/output_bounded/` are in
  `ps2xRuntime/src/runner/`, as APFS clones.
- **Headers:** `ps2_recompiled_functions.h` and `ps2_recompiled_stubs.h` are in
  `ps2xRuntime/include/`.
- **Git:** upstream's `.gitignore` already ignores both locations. The tracked placeholder
  `ps2xRuntime/src/runner/register_functions.cpp` is marked `--skip-worktree`, so `git status`
  stays clean.

**Re-sync rule** (whenever `ps2_recomp` output is regenerated): first delete the old generated
files, then copy the new ones. CMake compiles **every** `src/runner/*.cpp`, so stale files
from an earlier run would be built too, causing duplicate or outdated functions.

```bash
find ps2xRuntime/src/runner -name '*.cpp' ! -name register_functions.cpp -delete
```

```bash
find Burnout3_recomp/output_bounded -maxdepth 1 -name '*.cpp' -print0 | xargs -0 -J % cp -c % ps2xRuntime/src/runner/
```

```bash
cp -c Burnout3_recomp/output_bounded/ps2_recompiled_*.h ps2xRuntime/include/
```

Undo instructions are in `COMPLETED_WORK.md`.

## C. Build `ps2EntryRunner` (next)

Reuse the existing `out/build`. It already holds every dependency (raylib, SDL2, imgui,
rlImGui, fmt, toml11, …), so the build needs no network. raylib is required by the
graphics backend, so it can't be avoided anyway. The FFmpeg download only happens on Windows.

| Setting | Value | Reason |
|---|---|---|
| Build folder | `out/build` (Unix Makefiles) | Dependencies already present; Ninja would need a new folder and new downloads |
| `PS2X_ENABLE_AGRESSIVE_LOGS` | `OFF` | When on, it injects `PS_LOG_ENTRY` into all 71,113 functions (`PS2_FUNCTION_LOG_TRACKER`), slowing both the build and the game |
| `PS2X_ENABLE_RUNTIME_LOGS` | `ON` (default) | Keeps syscall, IOP and IO diagnostics |
| Optimization | `-O1`, asserts kept | `-O0` (current default) runs too slowly; `-O2`/`-O3` compile far slower on functions up to 3.6 MB of source. Leaving out `-DNDEBUG` keeps runtime `assert()` checks during bring-up |
| Unity build + precompiled header | `ON` (default) | About 2,200 batches of 32 files; the 6 MB generated header is parsed once instead of 71,113 times |
| `PS2X_ENABLE_SCCACHE` | `ON` (default) | Harmless; cache at `~/Library/Caches/Mozilla.sccache`, capped at 10 GB. No help on the first build |
| Parallel jobs | `-j8` | 8 performance cores, about 3 GB RAM per job. Drop to `-j6` if memory runs out |
| Target | `ps2EntryRunner` only | Skips the recompiler and tests |

```bash
cmake -S . -B out/build -DPS2X_ENABLE_AGRESSIVE_LOGS=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE="-O1"
```

```bash
cmake --build out/build --target ps2EntryRunner -j8 2>&1 | tee Burnout3_recomp/logs/09_build_ps2EntryRunner.log
```

**Expected cost (estimates):**
- **Time:** hours.
- **Disk:** `out/build` is 1.6 GB today and will grow by several GB of object files. 262 GB is free.
- **Shared build folder:** the new flags are stored in `out/build`, so every target there
  (including `ps2_recomp`, raylib and SDL2) recompiles once at `-O1` the next time it is built.
  To revert:

  ```bash
  cmake -S . -B out/build -DCMAKE_BUILD_TYPE= -DPS2X_ENABLE_AGRESSIVE_LOGS=ON
  ```

**Done when:** `out/build/ps2xRuntime/ps2EntryRunner` links. Fix compile and link errors
through the TOML, or through runtime code (see D). Never hand-edit generated files.

**Optional:** add `-gline-tables-only` to the flags for crash backtraces with file and line.
Cost: longer compiles and more disk (not measured).

## D. First boot and bring-up (pending)

1. Attach the disc (step A), then launch:

   ```bash
   ./out/build/ps2xRuntime/ps2EntryRunner Burnout3_recomp/disc/SLUS_210.50 2>&1 | tee Burnout3_recomp/logs/10_first_boot.log
   ```

2. Expected early blockers, in likely order:
   - missing or TODO syscalls
   - IOP reboot with `IOPRP280.IMG`
   - IRX imports `ps2xIOP` lacks
   - "function not found" for addresses reached through the 413,683 indirect-jump fallback entries
   - code Ghidra could not follow at `0x74000–0x76680`, probably copied to low RAM at runtime
   - GS and VU1 rendering
   - DNAS and network (stub them)
3. Iterate with the README loop: classify with temporary `ret0`/`ret1` stubs, then replace
   them with real fixes.

**Prefer runtime-side fixes over regeneration.** This is the main consequence of the step C setup:
- **Regenerating** (any TOML change, including `handler@0xADDR` stubs) changes
  `ps2_recompiled_functions.h`. That header is in the precompiled header, so **all 71k files
  recompile, taking hours**. Batch TOML changes together.
- **Game overrides** (`PS2_REGISTER_GAME_OVERRIDE`, `ps2_game_overrides::bindAddressHandler`)
  bind addresses to handlers at load time and leave generated code untouched. Keep their
  sources tracked in `Burnout3_recomp/overrides/` and clone them into `ps2xRuntime/src/runner/`,
  which is compiled automatically and ignored by git. An edit then rebuilds one unity batch
  and relinks, which takes minutes.
- **Generic syscall and stub gaps** belong in `ps2xRuntime/src/lib`. That is upstream source:
  change it only after explicit review, and keep any patch separate from Burnout 3-specific code.

**Debugging trade-off:** with aggressive logs off there is no per-function entry trace. When
a crash location is unclear, temporarily rebuild with `PS2X_ENABLE_AGRESSIVE_LOGS=ON` (a full
rebuild) or use the debug UI.

## Later (after D)

- **Jump tables:** resolve the 5,636 unresolved JR/JALR jumps in Ghidra. This improves
  performance and is not needed for correctness.
- **Optimization:** move to `-O2` once boot is stable, if speed requires it.
- **Audio and memory cards:** audio through the disc's sound IRX modules; memory-card saves
  go to `Burnout3_recomp/disc/mc0/`.
