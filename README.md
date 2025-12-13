## PS2Recomp: PlayStation 2 Static Recompiler (Not ready)

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
```
### Usage

1. Create a configuration file (see `./ps2xRecomp/example_config.toml`)
2. Run the recompiler: 
```
./ps2recomp your_config.toml
```

Compile the generated C++ code
Link with a runtime implementation

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

### Reference Projects

We have collected useful reference implementations in the `reference/` directory:

| Project | Purpose | Key Files |
|---------|---------|-----------|
| **parallel-gs** | GS emulation (Vulkan) | `gs/gs_interface.hpp` |
| **ps2-recompiler-Cactus** | Threading, semaphores | `src/ps2lib/threading/` |
| **Ps2Recomp-menaman123** | Event Flags (SDL) | `host_app/syscalls.cpp` |
| **SotCStaticRecompilation** | Config examples | `config.yaml` |

See `reference/README.md` for detailed documentation.

### Project Documentation

See the `docs/` folder in the project root:
* `docs/ai-assisted-workflow.md` - How to use AI for reverse engineering
* `docs/ps2recomp-analysis.md` - Full technical analysis
* `docs/ps2recomp-bug-report.md` - Bug fixes we've made

###  Acknowledgments

* Inspired by N64Recomp
* Uses ELFIO for ELF parsing
* Uses toml11 for TOML parsing
* Uses fmt for string formatting
* Reference implementations from parallel-gs, ps2-recompiler (Cactus), and Ps2Recomp (menaman123)
