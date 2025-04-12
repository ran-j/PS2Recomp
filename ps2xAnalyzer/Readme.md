# PS2 ELF Analyzer Tool

The PS2 ELF Analyzer Tool helps automate the process of creating TOML configuration files for the PS2Recomp static recompiler. It analyzes PlayStation 2 ELF files and generates a recommended configuration based on the binary's characteristics.

## Key Features

* Analyzes PS2 ELF binaries to extract symbols, functions, and structure
* Identifies common library functions that should be stubbed
* Flags system functions that should be skipped during recompilation
* Detects potential instruction patterns that may need patching
* Generates a ready-to-use TOML configuration file for PS2Recomp

## Using the Analyzer
```bash
ps2_analyzer <input_elf> <output_toml>
```

### Where:

* `input_elf` is the path to the PS2 ELF file you want to analyze
* `output_toml` is the path where the generated TOML configuration will be saved

## Example:
```bash
ps2_analyzer path/to/your/ps2_game.elf config.toml
```

## How It Works
The analyzer performs the following steps:

* Parses the ELF file using the same ElfParser used by PS2Recomp
* Extracts functions, symbols, sections, and relocations
* Analyzes the entry point to understand initialization patterns
* Identifies library functions by name patterns and signatures
* Maps the call graph to understand relationships between functions
* Analyzes data usage patterns (basic implementation)
* Scans for problematic instructions that might need patching
* Generates a TOML configuration file with all findings

## Generated Configuration
The tool creates a TOML file with the following sections:
```toml
[general]
input = "path/to/your/ps2_game.elf"
output = "output/"
single_file_output = false
runtime_header = "include/ps2_runtime.h"

stubs = [
  # List of identified library functions to stub
  "printf",
  "malloc",
  # ...
]

skip = [
  # List of system functions to skip
  "entry",
  "_start",
  # ...
]

[patches]
instructions = [
  # Potential instruction patches
  { address = "0x100008", value = "0x00000000" },
  # ...
]
```

## Extending the Analyzer
The analyzer is designed to be extensible. You can enhance its capabilities by:

* Adding more library function patterns in initializeLibraryFunctions()
* Improving the call graph analysis in analyzeCallGraph()
* Enhancing data usage pattern detection in analyzeDataUsage()
* Refining patch detection logic in identifyPotentialPatches()

## Limitations

* The analyzer uses basic heuristics and may not catch all special cases
* Function identification relies heavily on symbol names
* Patch recommendations are preliminary and may need manual review
* Complex game-specific behaviors may not be detected 