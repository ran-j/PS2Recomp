#!/usr/bin/env python3
"""Iteratively add missing entry points until no more are needed"""

import subprocess
import re
import os
import shutil
import json

os.chdir("C:/Users/User/Documents/decompilations/sly1-ps2-decomp/tools/ps2recomp")

def run_game():
    try:
        result = subprocess.run(
            ["./build/ps2xRuntime/ps2EntryRunner.exe",
             "C:/Users/User/Documents/decompilations/sly1-ps2-decomp/disc/SCUS_971.98"],
            capture_output=True, text=True, timeout=15
        )
        return result.stdout + result.stderr
    except subprocess.TimeoutExpired:
        return "TIMEOUT"

def add_entry(addr):
    with open('sly1_functions.json', 'r') as f:
        data = json.load(f)

    for func in data:
        if func['address'] <= addr < func['address'] + func['size']:
            if func['address'] == addr:
                # Entry already exists, but maybe not rebuilt yet
                return "exists"

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
            return "added"
    return "not_found"

def regenerate():
    subprocess.run(["./build/ps2xRecomp/ps2recomp.exe", "sly1_config.toml"],
                   capture_output=True)
    # Copy files
    shutil.copy("../../recomp_output/ps2_recompiled_functions.cpp", "ps2xRuntime/src/runner/")
    shutil.copy("../../recomp_output/register_functions.cpp", "ps2xRuntime/src/runner/")
    shutil.copy("../../recomp_output/ps2_recompiled_functions.h", "ps2xRuntime/include/")
    shutil.copy("../../recomp_output/ps2_recompiled_stubs.h", "ps2xRuntime/include/")

def rebuild():
    subprocess.run(
        ["C:/msys64/msys2_shell.cmd", "-mingw64", "-defterm", "-no-start", "-c",
         "cd /c/Users/User/Documents/decompilations/sly1-ps2-decomp/tools/ps2recomp/build && cmake --build . --config Release 2>&1"],
        capture_output=True
    )

last_calls = 0
stale_count = 0

for i in range(100):
    print(f"\n=== Iteration {i+1} ===")

    output = run_game()

    match = re.search(r"No func at (0x[0-9a-f]+)", output)
    calls_match = re.search(r"Total calls: (\d+)", output)
    calls = int(calls_match.group(1)) if calls_match else 0

    print(f"Total calls: {calls}")

    if "TIMEOUT" in output:
        print("Game timed out (running successfully!)")
        break

    if not match:
        print("No more missing functions!")
        break

    addr = int(match.group(1), 16)
    print(f"Missing: 0x{addr:x}")

    result = add_entry(addr)
    if result == "added":
        print(f"Added entry 0x{addr:x}")
    elif result == "exists":
        print(f"Entry 0x{addr:x} exists, rebuilding...")
    else:
        print(f"Could not find function containing 0x{addr:x}")
        break

    regenerate()
    rebuild()

    # Check if we're making progress
    if calls == last_calls:
        stale_count += 1
        if stale_count > 3:
            print("No progress, stopping...")
            break
    else:
        stale_count = 0
    last_calls = calls

print("\nDone!")
