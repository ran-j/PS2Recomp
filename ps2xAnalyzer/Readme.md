# PS2 ELF Analyzer Tool

The PS2 ELF Analyzer Tool automates the creation of TOML configuration files for the PS2Recomp static recompiler. It identifies function boundaries, library stubs, and problematic instructions.

## Analysis Paths

The analyzer supports three distinct paths for discovering code within a PS2 binary:

### 1. DWARF Debug Information
If the ELF was compiled with debug symbols (`-g`), the analyzer uses `libdwarf` to extract perfect function names and exact start/end addresses. This is common in homebrew or early development builds.

### 2. Native Heuristic Scanner (Test only)
For commercial games where symbols are stripped, the analyzer uses a "JAL Scanner":
* It scans executable sections for `JAL` (Jump and Link) instructions.
* It infers function start points based on jump targets.
* It generates names like `sub_XXXXXXXX`.

Use this path only as a quick fallback when you do not yet have a Ghidra project. It is not the preferred workflow for retail games.

### 3. SCE SDK Symbol Database (For SDK Function Names)
For stripped retail games, the analyzer can identify SCE/PS2SDK library functions from a
`sce-symbol-scanner` compatible database. A snapshot of the database is embedded in the
analyzer. Pass the directory that contains `symbols.json` and `tree.json`, or point
`PS2RECOMP_SCE_SYMBOL_DB` at that directory, only when you want to override the embedded
snapshot.

This path is meant to recover names such as CD/DVD, pad, DMA, GS, kernel, and libc SDK
functions so they can be classified before the expensive analysis passes run.

The current database was built from PS2 games with debug information, primarily the
Japanese set, and depends on samples that retained relocations. Treat the result as a
high-confidence hint rather than a complete SDK catalog: it can miss SDK variants that
were not present in the sampled games, and ambiguous matches are intentionally ignored.

### 4. Ghidra Integration (For Retail and Stripped Games, Preferred)
This is the recommended workflow for almost every commercial game:
1. Use the provided script: `ps2xRecomp/tools/ghidra/ExportPS2Functions.java`.
2. Run it in Ghidra to export a CSV map of all functions.
3. Let the script generate the TOML, and keep the CSV path in `ghidra_output = "path/to/map.csv"`.
4. Run the recompiler with that exported TOML.
5. The recompiler will prioritize Ghidra's boundaries over its own heuristics.

## Key Features

* Analyzes PS2 ELF binaries to extract symbols, functions, and structure
* Identifies common library functions that should be stubbed
* Reports risky instruction patterns for manual review without auto-skipping functions
* Detects potential instruction patterns that may need patching
* Generates a ready-to-use TOML configuration file for PS2Recomp

## Using the Analyzer
```bash
ps2_analyzer <input_elf> <output_toml> [sce_symbol_db_dir]
```

### Parameters:

* `input_elf`: Path to the PS2 ELF file.
* `output_toml`: Path where the generated TOML configuration will be saved.
* `sce_symbol_db_dir`: Optional override path to a directory containing `symbols.json` and `tree.json`.

## Example Workflow
1. Open `game.elf` in Ghidra.
2. Run `ps2xRecomp/tools/ghidra/ExportPS2Functions.java`.
3. Use the exported TOML and CSV.
4. Run the recompiler:
   `ps2recomp config.toml`

Fallback:
1. Run `ps2_analyzer game.elf config.toml`.
2. Use that TOML only for quick bring-up or symbol-rich builds.

## Generated Configuration
The tool creates a TOML file with the following sections:
* `[general]`: Paths to ELF and Ghidra maps.
* `stubs`: Runtime-known functions to be replaced by C++ stubs or syscall handlers.
* `untracked_stubs`: Detected library-like functions without runtime handlers. This is
  informational only and is ignored by the recompiler.
* `skip`: Legacy compatibility field. The analyzer no longer auto-populates it.
* `[patches]`: Individual instructions that need to be replaced (SYSCALLs, COP0, etc.).

## Limitations

* Heuristics may not catch all special cases in highly optimized code.
* Self-modifying code is flagged but requires manual review.
* Indirect jumps (jump tables) are detected but complex ones might need manual TOML entries.

For more details on the recompilation process, see the [Main README](../README.md).
