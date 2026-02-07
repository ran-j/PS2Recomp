## PS2Recomp: PlayStation 2 Static Recompiler (Not ready)

[![Discord](https://img.shields.io/badge/Discord-Join%20Server-5865F2?logo=discord&logoColor=white)](https://discord.gg/JQ8mawxUEf)

* Note this is an experiment and doesn't work as it should, feel free to open a PR to help the project.

PS2Recomp is a tool designed to statically recompile PlayStation 2 ELF binaries into C++ code that can be compiled for any modern platform. This enables running PS2 games natively on PC and other platforms without traditional emulation.

### Features

* Translates MIPS R5900 instructions to C++ code
* Supports PS2-specific 128-bit MMI instructions
* Handles VU0 in macro mode
* Supports relocations and overlays
* Configurable via TOML files
* Single-file or multi-file output options
* Function stubbing and skipping

### How It Works
PS2Recomp works by:

Parsing a PS2 ELF file to extract functions, symbols, and relocations
Decoding the MIPS R5900 instructions in each function
Translating those instructions to equivalent C++ code
Generating a runtime that can execute the recompiled code

The translated code is very literal, with each MIPS instruction mapping to a C++ operation. For example, `addiu $r4, $r4, 0x20` becomes `ctx->r4 = ADD32(ctx->r4, 0X20);`.

### Requirements

* CMake 3.20 or higher
* C++20 compatible compiler (I only test with MSVC)
* SSE4/AVX support for 128-bit operations

#### Building
```bash
git clone --recurse-submodules https://github.com/ran-j/PS2Recomp.git
cd PS2Recomp

# Create build directory
mkdir build
cd build

cmake ..
cmake --build .
ctest --output-on-failure
```

Optional CMake flags:
- `-DPS2X_RUNTIME=OFF` to skip building `ps2xRuntime`
- `-DPS2X_TESTS=OFF` to skip building `ps2xTest`

### Usage

1. **Analyze the ELF**: Use the `ps2_analyzer` tool to generate an initial configuration.
```bash
./ps2_analyzer your_game.elf config.toml
```
*For better results on retail games, see the [Ghidra Workflow](ps2xAnalyzer/Readme.md#3-ghidra-integration-recommended-for-complex-games).*

2. **Recompile**: Run the recompiler using the generated configuration.
```bash
./ps2recomp config.toml
```

3. **Compile Output**: 
* Compile the generated C++ code in the `output/` directory.
* Link with the `ps2xRuntime` implementation.

### Configuration
PS2Recomp uses TOML configuration files to specify:

* Input ELF file
* Output directory
* Functions to stub or skip
* Instruction patches

#### Example configuration:
```toml
[general]
input = "path/to/game.elf"
output = "output/"
single_file_output = false

# Functions to stub
stubs = ["printf", "malloc", "free"]

# Functions to skip
skip = ["abort", "exit"]

# Patches
[patches]
instructions = [
  { address = "0x100004", value = "0x00000000" }
]
```

### Runtime
To execute the recompiled code, you'll need to implement or use a runtime that provides:

* Memory management
* System call handling
* PS2-specific hardware simulation

A basic runtime lib is provided in `ps2xRuntime` folder.

### Limitations

* VU1 microcode support is limited
* Graphics Synthesizer and other hardware components need external implementation
* Some PS2-specific features may not be fully supported yet

###  Acknowledgments

* Inspired by N64Recomp
* Uses ELFIO for ELF parsing
* Uses toml11 for TOML parsing
* Uses fmt for string formatting
