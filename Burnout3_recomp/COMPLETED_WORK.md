# Burnout 3 USA (SLUS_210.50): recompilation workspace

All paths below are relative to the PS2Recomp repo root. Run every command from there.

## Git policy

- Only the TOML configs, `tools/`, this file and `.gitignore` are tracked.
- Never commit game files or anything derived from the game: the ELF, disc images,
  IRX modules, extracted assets, the Ghidra project, function maps, reports, logs,
  backups, or generated C++ (`output*/`). `Burnout3_recomp/.gitignore` enforces this.
- Machine-specific values (ELF SHA-256, original ELF location) live in the ignored
  `Burnout3_recomp/local/burnout3_local.json`; the tools read them from there.
- Generated C++ is reproducible from the ELF, TOML, CSV and the recompiler commit,
  so it is never tracked.

## Workspace layout

| Path | Tracked | Contents |
|---|---|---|
| `SLUS_210.50` | no | Working copy of the ELF, byte-identical to the original |
| `ghidra_project/` | no | Ghidra 12.1.4 project `Burnout3_USA_Recomp` (r5900:LE:32:default) |
| `burnout3_usa_ghidra_functions.csv` | no | Original exporter map (71,108 entries) |
| `burnout3_usa_ghidra_functions_instruction_bounded.csv` | no | Repaired map (71,112 entries) |
| `burnout3_usa_recomp.toml` | yes | Original exporter config, writes `output/` |
| `burnout3_usa_recomp_instruction_bounded.toml` | yes | Repaired-map config, writes `output/` |
| `burnout3_usa_recomp_instruction_bounded_output_bounded.toml` | yes | Same, writes `output_bounded/` |
| `tools/` | yes | Ghidra scripts and the repair/verify scripts |
| `reports/`, `logs/`, `backups/` | no | Analysis/validation reports, console logs, pre-repair exports |
| `output/`, `output_bounded/` | no | Generated C++ |
| `disc_image/` | no | Read-only mount point for your own Burnout 3 ISO (empty when detached) |
| `disc/` | no | Runtime launch folder: relative symlinks into `disc_image/`, an APFS clone of `SLUS_210.50`, and a writable `mc0/` for saves |

Mount before running the game, and detach afterwards. The runtime maps `cdrom0:` to the folder containing the launched ELF.

```bash
hdiutil attach -readonly -nobrowse -mountpoint "$PWD/Burnout3_recomp/disc_image" "<path to your Burnout 3 ISO>"
```

```bash
hdiutil detach Burnout3_recomp/disc_image
```

## Completed steps

1. **Ghidra import and analysis.** 7,776 functions, 880,944 instructions. Single
   executable section `0x00100000–0x004E2680` (code and data mixed), entry `0x00100008`.
   12 error bookmarks and 1 warning bookmark (see "Known issues").
2. **Export.** The unchanged repository script `ps2xRecomp/tools/ghidra/ExportPS2Functions.java`
   produced `burnout3_usa_recomp.toml` and `burnout3_usa_ghidra_functions.csv`
   (7,776 functions + 63,332 code labels, 58 runtime stubs).
3. **Baseline recompilation** with `burnout3_usa_recomp.toml` into `output/`.
   It completed, but the original map has two defects:
   - **Data treated as code.** 4,270 unique `Unhandled …` instructions (emitted as
     `throw std::runtime_error`) in four files: `entry_004b9238`, `entry_004ca498`,
     `entry_004dd7e0` and `entry_004e2118`. All lie in `0x4B9440–0x4E2664`, and none
     are inside Ghidra's instruction ranges.
   - **Noncontiguous function bodies.** 453 rows have `End - Start != Size`, because
     the exporter sets End to the body's highest address. That produced 887 MB of
     output, including `FUN_0014d410` at 139 MB.
4. **Range repair.** `tools/ExportBurnout3InstructionRanges.java` exported Ghidra's
   contiguous instruction runs to `reports/burnout3_usa_ghidra_instruction_ranges.csv`.
   `tools/repair_burnout3_export_ranges.py` bounded every range to those runs. It kept
   all 71,108 original entries, added 4 fragments, and excluded 193,076 non-instruction
   bytes. `tools/verify_burnout3_exports.py` passes all 30 checks.
5. **Repaired-map recompilation** into `output_bounded/`, logged to
   `logs/08_ps2_recomp_instruction_bounded.log`. It ran in 4m50s with exit code 0.

   | | Baseline `output/` | Repaired `output_bounded/` |
   |---|---|---|
   | Functions in map | 71,108 | 71,112 (all recompiled or stubbed, 0 decode failures) |
   | Unhandled instructions | 4,270 unique | **0** |
   | `// Unimplemented` register markers | 61 | 0 |
   | Total size | 887 MB | 517 MB |
   | Largest function file | 139 MB (`FUN_0014d410`) | 3.6 MB (`FUN_00186850`, a genuinely 77 KB function) |
   | Errors | not captured | 0 |
   | Warnings | not captured | 5,636, all `control-flow` |

   Each warning is "unresolved JR/JALR; promoted N fallback entries". These are
   indirect jumps, mostly `switch` jump tables (`caseD_*`, `FUN_*`, `entry_*`), that the
   recompiler could not resolve statically. It registered 413,683 fallback entry points
   instead, so these jumps go through the runtime dispatch table (`register_functions.cpp`
   is 21.8 MB). They are not build blockers, but they are slower and are the next thing
   to triage. Whether the 4 split fragments are always reached correctly can only be
   confirmed at runtime.

   `output_bounded/` is the output to integrate next. Keep `output/` only for comparison.

## Known issues

- Ghidra error bookmarks: 11 flows from `0x484414–0x485654` into `0x74000–0x76680`,
  which is memory not in the ELF (probably code copied to low RAM at runtime; this
  is inferred), and conflicting data at `0x200024`.
- Ghidra decompiler `VarnodeContext` errors for `vf1`, `vf8` and `vf22` at `0x1700B0`,
  `0x17C01C` and `0x18EF0C`. These affect the decompiler only, not `ps2_recomp`.

## Commands

```bash
python3 Burnout3_recomp/tools/verify_burnout3_exports.py
```

```bash
./out/build/ps2xRecomp/ps2_recomp Burnout3_recomp/burnout3_usa_recomp_instruction_bounded_output_bounded.toml > Burnout3_recomp/logs/08_ps2_recomp_instruction_bounded.log 2>&1
```

```bash
sed -n '/PS2Recomp report/,/Events:/p' Burnout3_recomp/logs/08_ps2_recomp_instruction_bounded.log
```

## Not done yet

- Triage the 5,636 control-flow warnings by resolving jump tables in Ghidra.
- Build, link, then handle runtime work (syscalls, GS/VU1, IOP/IRX), using your own disc for assets.

## Runtime integration (done)

`output_bounded/` is integrated into `ps2xRuntime`:
- **Code:** all 71,113 `.cpp` files, cloned with `cp -c` (no extra disk space), are in
  `ps2xRuntime/src/runner/`.
- **Headers:** `ps2_recompiled_functions.h` and `ps2_recompiled_stubs.h` are in
  `ps2xRuntime/include/`.
- **Git:** upstream's `.gitignore` already ignores both locations. The tracked placeholder
  `ps2xRuntime/src/runner/register_functions.cpp` is marked `--skip-worktree`, so the
  generated version does not show as modified.

To undo (restores the upstream placeholder):

```bash
find ps2xRuntime/src/runner -name '*.cpp' ! -name register_functions.cpp -delete
```

```bash
rm ps2xRuntime/include/ps2_recompiled_functions.h ps2xRuntime/include/ps2_recompiled_stubs.h
```

```bash
git update-index --no-skip-worktree ps2xRuntime/src/runner/register_functions.cpp
```

```bash
git checkout -- ps2xRuntime/src/runner/register_functions.cpp
```
