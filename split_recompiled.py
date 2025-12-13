#!/usr/bin/env python3
"""
Split ps2_recompiled_functions.cpp into smaller files by address range.
"""
import re
import os
from collections import defaultdict

INPUT_FILE = "ps2xRuntime/src/runner/ps2_recompiled_functions.cpp"
OUTPUT_DIR = "ps2xRuntime/src/runner/recompiled"

# Common header for all files
COMMON_HEADER = '''// Auto-generated split file - DO NOT EDIT DIRECTLY
// Edit the original ps2_recompiled_functions.cpp and re-run split_recompiled.py

#include "ps2_recompiled_functions.h"
#include "ps2_runtime_macros.h"
#include "ps2_runtime.h"
#include "ps2_recompiled_stubs.h"
#include "ps2_stubs.h"

'''

def get_address_from_name(func_name):
    """Extract address from function name like entry_1a0008 or FUN_001002c0"""
    # entry_XXXXXX format
    match = re.match(r'entry_([0-9a-fA-F]+)', func_name)
    if match:
        return int(match.group(1), 16)

    # FUN_XXXXXXXX format
    match = re.match(r'FUN_([0-9a-fA-F]+)', func_name)
    if match:
        return int(match.group(1), 16)

    return None

def get_range_key(addr):
    """Get the 64KB range key for an address"""
    if addr is None:
        return "misc"
    return f"{(addr >> 16):02x}xxxx"

def main():
    print(f"Reading {INPUT_FILE}...")

    with open(INPUT_FILE, 'r', encoding='utf-8', errors='replace') as f:
        content = f.read()

    # Find all function definitions
    # Pattern: void funcname(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    func_pattern = re.compile(
        r'^(void\s+(\w+)\s*\([^)]*\)\s*\{)',
        re.MULTILINE
    )

    # Find all function starts
    functions = []
    for match in func_pattern.finditer(content):
        func_start = match.start()
        func_name = match.group(2)
        functions.append((func_start, func_name))

    print(f"Found {len(functions)} functions")

    # Group functions by address range
    range_functions = defaultdict(list)

    for i, (start, name) in enumerate(functions):
        # Find end of function (next function start or end of file)
        if i + 1 < len(functions):
            end = functions[i + 1][0]
        else:
            end = len(content)

        func_code = content[start:end].rstrip() + '\n\n'

        addr = get_address_from_name(name)
        range_key = get_range_key(addr)

        range_functions[range_key].append((name, func_code))

    # Create output directory
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    # Write split files
    all_files = []
    for range_key, funcs in sorted(range_functions.items()):
        filename = f"recomp_{range_key}.cpp"
        filepath = os.path.join(OUTPUT_DIR, filename)
        all_files.append(filename)

        print(f"Writing {filename} ({len(funcs)} functions)...")

        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(COMMON_HEADER)
            for name, code in funcs:
                f.write(code)

    # Write a header that declares all functions
    print("Writing recomp_all.h...")
    with open(os.path.join(OUTPUT_DIR, "recomp_all.h"), 'w', encoding='utf-8') as f:
        f.write("// Auto-generated - all recompiled function declarations\n")
        f.write("#pragma once\n\n")
        f.write('#include "ps2_runtime.h"\n\n')

        for range_key, funcs in sorted(range_functions.items()):
            f.write(f"// {range_key}\n")
            for name, _ in funcs:
                f.write(f"void {name}(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime);\n")
            f.write("\n")

    print(f"\nDone! Created {len(all_files)} files in {OUTPUT_DIR}/")
    print("\nFiles created:")
    for f in sorted(all_files):
        print(f"  {f}")

    # Show summary
    print("\nSummary by range:")
    for range_key in sorted(range_functions.keys()):
        count = len(range_functions[range_key])
        print(f"  {range_key}: {count} functions")

if __name__ == "__main__":
    main()
