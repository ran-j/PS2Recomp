# Burnout 3 USA (SLUS_210.50) recompilation workspace

Workspace for statically recompiling Burnout 3 (USA) with PS2Recomp. Game files and
game-derived data are never tracked (see `.gitignore`); use your own disc only.

- `ROADMAP.md`: the overall plan, steps A–D.
- `COMPLETED_WORK.md`: what has been done so far.

All commands below run from the PS2Recomp repo root.

## Step C: build `ps2EntryRunner`, in 4 milestones

Step C turns the integrated generated code (step B) into one executable,
`out/build/ps2xRuntime/ps2EntryRunner`.

In unity mode, CMake joins the 71,113 generated files in `ps2xRuntime/src/runner/` into
about 2,200 batches of 32. A precompiled header parses the 6 MB
`ps2_recompiled_functions.h` once instead of 71,113 times. The linker then combines the
batches with `libps2_runtime.a` and the dependencies already in `out/build/_deps`.

**Done when:** the executable links and its log is saved. Booting the game is step D.

| # | Milestone | Duration | Main risk |
|---|---|---|---|
| 1 | Pre-flight and reconfigure | minutes | Stale or missing runner files; an unexpected download |
| 2 | Runtime library at the new flags | minutes | Toolchain or flag errors |
| 3 | Compile the generated code | hours | Running out of memory on the heaviest batches |
| 4 | Link, triage, record | minutes to hours | Missing symbols |

### Milestone 1: pre-flight and reconfigure

Confirm the inputs, then switch `out/build` to the step C settings.

1. Check the runner files and headers match the integrated output:
   - 71,113 `.cpp` files in `ps2xRuntime/src/runner/`, identical to `Burnout3_recomp/output_bounded/`
   - both `ps2_recompiled_*.h` headers in `ps2xRuntime/include/`
   - `register_functions.cpp` marked `S` (skip-worktree) in `git ls-files -v`
2. Check the dependencies are present in `out/build/_deps` (raylib, SDL2, imgui, rlImGui and
   the rest), so no network is needed.
3. Check free disk space: expect several GB of object files, plus up to 10 GB of sccache cache.
4. Reconfigure (aggressive logs off, `-O1`, asserts kept, so no `-DNDEBUG`):

```bash
cmake -S . -B out/build -DPS2X_ENABLE_AGRESSIVE_LOGS=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE="-O1"
```

**Done when:** configure finishes without downloading anything, and `out/build/CMakeCache.txt`
shows `PS2X_ENABLE_AGRESSIVE_LOGS=OFF`, `CMAKE_BUILD_TYPE=Release` and `CMAKE_CXX_FLAGS_RELEASE=-O1`.

### Milestone 2: runtime library at the new flags

Rebuild the runtime and its dependencies alone, before the hours-long part. This catches
toolchain or flag problems in minutes.

```bash
cmake --build out/build --target ps2_runtime -j8 2>&1 | tee Burnout3_recomp/logs/09a_build_ps2_runtime.log
```

**Done when:** `out/build/ps2xRuntime/libps2_runtime.a` is rebuilt (newer timestamp) with no errors.

### Milestone 3: compile the generated code

The long step. Run it in the background and watch memory use.

```bash
cmake --build out/build --target ps2EntryRunner -j8 2>&1 | tee Burnout3_recomp/logs/09_build_ps2EntryRunner.log
```

- **Heaviest batches:** the one containing `register_functions.cpp` (21.8 MB, a lookup table
  for about 4 million guest addresses) and those with the largest functions, such as
  `FUN_00186850` (3.6 MB of source).
- **If memory runs out or the compiler crashes:** rerun the same command with `-j6`, then
  `-j4`. Make resumes where it stopped.
- **If a single file is the problem:** shrinking the unity batch size or excluding that file
  from unity mode means editing upstream CMake, so it needs review first.
- **Possible name collisions:** two files in one batch could define file-local helpers with the
  same name. This is unlikely, because generated names include addresses.

**Done when:** every unity batch compiles. The log shows no `error:` lines before the link step.

### Milestone 4: link, triage, record

- **Missing symbols:** link errors usually mean declared functions have no implementation, such
  as stubs the generated code references that the runtime lacks. Group them by name in the log.
- **Fix them, preferring the runtime side:**
  - Burnout 3-specific fixes go in a game override, kept in `Burnout3_recomp/overrides/` and
    cloned into `ps2xRuntime/src/runner/`. That rebuilds one batch.
  - Generic gaps go in `ps2xRuntime/src/lib`, which is upstream source, after review.
  - TOML changes regenerate code and force a full rebuild, so batch them.
  - Never hand-edit generated files.
- **Record the result:** note the outcome in `COMPLETED_WORK.md` and update the status table
  in `ROADMAP.md`.

**Done when:** `out/build/ps2xRuntime/ps2EntryRunner` links cleanly and is newer than the old
placeholder build, and the log is saved at `Burnout3_recomp/logs/09_build_ps2EntryRunner.log`.
This proves the code compiles. Missing syscalls, IOP modules and graphics gaps show up in step D.

### Reverting the build settings

```bash
cmake -S . -B out/build -DCMAKE_BUILD_TYPE= -DPS2X_ENABLE_AGRESSIVE_LOGS=ON
```
