#!/usr/bin/env python3
"""
Analyze the ELF to find all JAL instructions and extract their return addresses.
These return addresses need to be registered as function entry points.
"""

import struct
import json
import sys

def read_elf_code_section(elf_path):
    """Read the .text section from the ELF"""
    with open(elf_path, 'rb') as f:
        # Read ELF header
        f.seek(0)
        magic = f.read(4)
        if magic != b'\x7fELF':
            raise ValueError("Not an ELF file")

        f.seek(0x1C)  # e_phoff (32-bit ELF)
        phoff = struct.unpack('<I', f.read(4))[0]

        f.seek(0x2A)  # e_phentsize
        phentsize = struct.unpack('<H', f.read(2))[0]

        f.seek(0x2C)  # e_phnum
        phnum = struct.unpack('<H', f.read(2))[0]

        # Find loadable segments
        segments = []
        for i in range(phnum):
            f.seek(phoff + i * phentsize)
            p_type = struct.unpack('<I', f.read(4))[0]
            p_offset = struct.unpack('<I', f.read(4))[0]
            p_vaddr = struct.unpack('<I', f.read(4))[0]
            p_paddr = struct.unpack('<I', f.read(4))[0]
            p_filesz = struct.unpack('<I', f.read(4))[0]
            p_memsz = struct.unpack('<I', f.read(4))[0]

            if p_type == 1:  # PT_LOAD
                f.seek(p_offset)
                data = f.read(p_filesz)
                segments.append((p_vaddr, data))

        return segments

def find_jal_returns(segments):
    """Find all JAL instructions and their return addresses"""
    jal_returns = set()

    for base_addr, data in segments:
        # Process each 4-byte instruction
        for offset in range(0, len(data) - 3, 4):
            instr = struct.unpack('<I', data[offset:offset+4])[0]
            opcode = (instr >> 26) & 0x3F

            # JAL = opcode 3
            if opcode == 3:
                addr = base_addr + offset
                return_addr = addr + 8  # JAL + delay slot
                jal_returns.add(return_addr)

            # JALR = SPECIAL (opcode 0) with function code 9
            if opcode == 0:
                func = instr & 0x3F
                if func == 9:  # JALR
                    addr = base_addr + offset
                    return_addr = addr + 8
                    jal_returns.add(return_addr)

    return sorted(jal_returns)

def main():
    elf_path = "C:/Users/User/Documents/decompilations/sly1-ps2-decomp/disc/SCUS_971.98"
    json_path = "sly1_functions.json"

    print(f"Reading ELF: {elf_path}")
    segments = read_elf_code_section(elf_path)
    print(f"Found {len(segments)} loadable segments")

    print("Scanning for JAL instructions...")
    jal_returns = find_jal_returns(segments)
    print(f"Found {len(jal_returns)} unique JAL return addresses")

    # Load existing functions
    with open(json_path, 'r') as f:
        functions = json.load(f)

    # Get existing entry points
    existing_entries = {f['address'] for f in functions}
    print(f"Existing entry points: {len(existing_entries)}")

    # Find which JAL returns are missing
    missing = [addr for addr in jal_returns if addr not in existing_entries]
    print(f"Missing entry points: {len(missing)}")

    if missing:
        print("\nFirst 20 missing entry points:")
        for addr in missing[:20]:
            print(f"  0x{addr:x}")

    # Find which functions need to be split
    splits_needed = []
    for addr in missing:
        for func in functions:
            if func['address'] < addr < func['address'] + func['size']:
                splits_needed.append((addr, func['name'], func['address']))
                break

    print(f"\nFunctions that need splitting: {len(splits_needed)}")

    # Ask if we should add them
    if len(sys.argv) > 1 and sys.argv[1] == '--apply':
        print("\nApplying splits...")
        new_functions = []

        for func in functions:
            # Check if any missing address falls within this function
            func_start = func['address']
            func_end = func_start + func['size']

            # Find all split points within this function
            split_points = sorted([addr for addr in missing if func_start < addr < func_end])

            if not split_points:
                new_functions.append(func)
            else:
                # Split the function at each point
                prev = func_start
                for i, split in enumerate(split_points):
                    if prev < split:
                        new_functions.append({
                            "name": func['name'] if prev == func_start else f"entry_{prev:x}",
                            "address": prev,
                            "size": split - prev
                        })
                    prev = split

                # Add the final part
                if prev < func_end:
                    new_functions.append({
                        "name": f"entry_{prev:x}",
                        "address": prev,
                        "size": func_end - prev
                    })

        # Sort by address
        new_functions.sort(key=lambda f: f['address'])

        # Write back
        with open(json_path, 'w') as f:
            json.dump(new_functions, f, indent=2)

        print(f"New total functions: {len(new_functions)}")
    else:
        print("\nRun with --apply to add missing entry points")

if __name__ == "__main__":
    import os
    os.chdir("C:/Users/User/Documents/decompilations/sly1-ps2-decomp/tools/ps2recomp")
    main()
