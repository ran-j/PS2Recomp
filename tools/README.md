# PS2Recomp Tools

This directory contains utility scripts for PS2Recomp development.

## `auto_fix_missing_targets.py`

A script to automatically recover missing function entry points (branch targets) that Ghidra's export might have missed (such as vtables or callbacks).

If the game crashes at runtime due to jumping to an unknown address (reporting a `[guest-branch:missing-target]` error), this script:
1. Captures the missing target address from the runtime log.
2. Disassembles the original ELF file to find where the function ends.
3. Appends the recovered function to your CSV map.
4. Recompiles and rebuilds the project.

The script runs in a loop: it boots the game, catches the missing target, fixes it, and boots again until all paths are covered. Because it is demand-driven, it only adds functions the game actually attempts to jump to, keeping the map clean of speculative matches.

### Requirements
- Python 3
- `capstone` (`pip install capstone`)

### Usage

Basic usage:
```bash
python3 tools/auto_fix_missing_targets.py \
  --config config.toml \
  --run "./build-run/ps2xRuntime/ps2EntryRunner game.elf"
```

Additional parameters (as needed):
- `--build` — command used to build the runtime (default: `cmake --build build-run...`)
- `--recomp` — path to the recompiler binary (default: `./build-run/ps2xRecomp/ps2_recomp`)
- `--timeout` — time in seconds to let the game run before safely killing it (default: 20)
- `--max-iterations` — maximum number of recovery loops per run
- `--cwd` — working directory against which relative paths in the config are resolved
- `--scan-only` — report the next missing target and exit without editing the map
- `--dry-run` — run the full loop without actually modifying the CSV file
