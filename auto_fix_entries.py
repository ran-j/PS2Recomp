#!/usr/bin/env python3
"""Automatically fix missing entry points by running the game and adding splits"""

import subprocess
import re
import sys
import os

def run_recompiler():
    """Run the recompiler"""
    result = subprocess.run(
        ["./build/ps2xRecomp/ps2recomp.exe", "sly1_config.toml"],
        capture_output=True, text=True, cwd="."
    )
    return "Recompilation completed successfully" in result.stdout

def copy_files():
    """Copy generated files"""
    import shutil
    for fname in ["ps2_recompiled_functions.cpp", "register_functions.cpp"]:
        src = f"../../recomp_output/{fname}"
        dst = f"ps2xRuntime/src/runner/{fname}"
        shutil.copy(src, dst)
    for fname in ["ps2_recompiled_functions.h", "ps2_recompiled_stubs.h", "ps2_runtime_macros.h"]:
        src = f"../../recomp_output/{fname}"
        dst = f"ps2xRuntime/include/{fname}"
        shutil.copy(src, dst)

def build():
    """Build the runtime"""
    result = subprocess.run(
        ["C:/msys64/msys2_shell.cmd", "-mingw64", "-defterm", "-no-start", "-c",
         "cd /c/Users/User/Documents/decompilations/sly1-ps2-decomp/tools/ps2recomp/build && cmake --build . --config Release 2>&1"],
        capture_output=True, text=True
    )
    return "error" not in result.stdout.lower() or "FAILED" not in result.stdout

def run_game():
    """Run the game and capture output"""
    try:
        result = subprocess.run(
            ["./build/ps2xRuntime/ps2EntryRunner.exe",
             "C:/Users/User/Documents/decompilations/sly1-ps2-decomp/disc/SCUS_971.98"],
            capture_output=True, text=True, timeout=20
        )
        return result.stdout + result.stderr
    except subprocess.TimeoutExpired:
        return "TIMEOUT"

def add_entry(addr):
    """Add entry point"""
    import json
    with open('sly1_functions.json', 'r') as f:
        data = json.load(f)

    for func in data:
        if func['address'] <= addr < func['address'] + func['size']:
            if func['address'] == addr:
                return False  # Already exists

            # Split
            new_data = []
            for f in data:
                if f['address'] == func['address']:
                    new_data.append({
                        "name": f['name'],
                        "address": f['address'],
                        "size": addr - f['address']
                    })
                    new_data.append({
                        "name": f"entry_{addr:x}",
                        "address": addr,
                        "size": f['address'] + f['size'] - addr
                    })
                else:
                    new_data.append(f)

            with open('sly1_functions.json', 'w') as out:
                json.dump(new_data, out, indent=2)
            return True
    return False

def main():
    max_iterations = 100
    for i in range(max_iterations):
        print(f"\n=== Iteration {i+1} ===")

        print("Recompiling...")
        if not run_recompiler():
            print("Recompilation failed!")
            return

        print("Copying files...")
        copy_files()

        print("Building...")
        build()

        print("Running game...")
        output = run_game()

        # Check for missing function
        match = re.search(r"No func at (0x[0-9a-f]+)", output)
        if match:
            addr = int(match.group(1), 16)
            print(f"Missing entry point: 0x{addr:x}")

            if add_entry(addr):
                print(f"Added entry point 0x{addr:x}")
                continue
            else:
                print(f"Could not add entry point 0x{addr:x}")
                break

        # Check total calls
        calls_match = re.search(r"Total calls: (\d+)", output)
        if calls_match:
            print(f"Total calls: {calls_match.group(1)}")

        if "TIMEOUT" in output:
            print("Game timed out (good - it's running!)")
            break

        if "No func" not in output:
            print("No more missing functions!")
            break

    print("\nDone!")

if __name__ == "__main__":
    os.chdir("C:/Users/User/Documents/decompilations/sly1-ps2-decomp/tools/ps2recomp")
    main()
