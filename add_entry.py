#!/usr/bin/env python3
"""Add missing entry points to sly1_functions.json"""

import json
import sys

def add_entry_point(target_addr):
    """Add entry point at target_addr by splitting the containing function"""
    with open('sly1_functions.json', 'r') as f:
        data = json.load(f)

    # Find the function containing target_addr
    containing_func = None
    for func in data:
        addr = func['address']
        size = func['size']
        if addr <= target_addr < addr + size:
            containing_func = func
            break

    if not containing_func:
        print(f"Error: No function contains address 0x{target_addr:x}")
        return False

    if containing_func['address'] == target_addr:
        print(f"Entry point 0x{target_addr:x} already exists: {containing_func['name']}")
        return True

    # Split the function
    old_addr = containing_func['address']
    old_end = old_addr + containing_func['size']
    old_name = containing_func['name']

    new_entries = []
    for func in data:
        if func['address'] == old_addr:
            # Part 1: old_addr to target_addr
            new_entries.append({
                "name": old_name,
                "address": old_addr,
                "size": target_addr - old_addr
            })
            # Part 2: target_addr to old_end
            new_entries.append({
                "name": f"entry_{target_addr:x}",
                "address": target_addr,
                "size": old_end - target_addr
            })
            print(f"Split {old_name} (0x{old_addr:x}-0x{old_end:x}) at 0x{target_addr:x}")
        else:
            new_entries.append(func)

    with open('sly1_functions.json', 'w') as f:
        json.dump(new_entries, f, indent=2)

    print(f"Total functions: {len(new_entries)}")
    return True

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: add_entry.py <address>")
        print("Example: add_entry.py 0x1f7128")
        sys.exit(1)

    addr = int(sys.argv[1], 16) if sys.argv[1].startswith("0x") else int(sys.argv[1])
    add_entry_point(addr)
