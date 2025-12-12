#!/usr/bin/env python3
"""
Fix function boundaries that split on delay slots.

When a function ends with a branch/jump instruction, the delay slot (next instruction)
must be included in the same function. This script adjusts function boundaries to
ensure delay slots are not separated from their branch instructions.
"""

import struct
import json
import sys

# MIPS opcodes that have delay slots
BRANCH_OPCODES = {
    0x02: 'J',      # Jump
    0x03: 'JAL',    # Jump and Link
    0x04: 'BEQ',    # Branch on Equal
    0x05: 'BNE',    # Branch on Not Equal
    0x06: 'BLEZ',   # Branch on Less Than or Equal to Zero
    0x07: 'BGTZ',   # Branch on Greater Than Zero
    0x14: 'BEQL',   # Branch on Equal Likely
    0x15: 'BNEL',   # Branch on Not Equal Likely
    0x16: 'BLEZL', # Branch on Less Than or Equal to Zero Likely
    0x17: 'BGTZL', # Branch on Greater Than Zero Likely
}

# SPECIAL opcodes (opcode 0) that have delay slots
SPECIAL_BRANCH_FUNCS = {
    0x08: 'JR',     # Jump Register
    0x09: 'JALR',   # Jump and Link Register
}

# REGIMM opcodes (opcode 1) that have delay slots
REGIMM_BRANCH_RT = {
    0x00: 'BLTZ',
    0x01: 'BGEZ',
    0x02: 'BLTZL',
    0x03: 'BGEZL',
    0x10: 'BLTZAL',
    0x11: 'BGEZAL',
    0x12: 'BLTZALL',
    0x13: 'BGEZALL',
}

def read_elf_segments(elf_path):
    """Read loadable segments from ELF file"""
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

def read_instruction(segments, addr):
    """Read a 32-bit instruction from the given address"""
    for base_addr, data in segments:
        if base_addr <= addr < base_addr + len(data):
            offset = addr - base_addr
            if offset + 4 <= len(data):
                return struct.unpack('<I', data[offset:offset+4])[0]
    return None

def is_branch_instruction(instr):
    """Check if instruction is a branch/jump with delay slot"""
    if instr is None:
        return False

    opcode = (instr >> 26) & 0x3F

    # Check main opcodes
    if opcode in BRANCH_OPCODES:
        return True

    # Check SPECIAL opcodes (opcode 0)
    if opcode == 0x00:
        func = instr & 0x3F
        if func in SPECIAL_BRANCH_FUNCS:
            return True

    # Check REGIMM opcodes (opcode 1)
    if opcode == 0x01:
        rt = (instr >> 16) & 0x1F
        if rt in REGIMM_BRANCH_RT:
            return True

    # Check COP1 (FPU) branches (opcode 0x11)
    if opcode == 0x11:
        fmt = (instr >> 21) & 0x1F
        if fmt == 0x08:  # BC1
            return True

    return False

def fix_delay_slot_boundaries(elf_path, json_path, output_path=None):
    """Fix function boundaries that split delay slots"""
    print(f"Loading ELF: {elf_path}")
    segments = read_elf_segments(elf_path)

    print(f"Loading functions: {json_path}")
    with open(json_path, 'r') as f:
        functions = json.load(f)

    # Sort by address
    functions.sort(key=lambda f: f['address'])

    # Build address -> function index map
    addr_to_idx = {f['address']: i for i, f in enumerate(functions)}

    fixes_needed = []

    # Check each function
    for i, func in enumerate(functions):
        func_start = func['address']
        func_end = func_start + func['size']

        # Check if the last instruction (at func_end - 4) is a branch
        last_instr_addr = func_end - 4
        last_instr = read_instruction(segments, last_instr_addr)

        if is_branch_instruction(last_instr):
            # The delay slot is at func_end
            delay_slot_addr = func_end

            # Check if this delay slot is the start of another function
            if delay_slot_addr in addr_to_idx:
                next_func_idx = addr_to_idx[delay_slot_addr]
                next_func = functions[next_func_idx]

                fixes_needed.append({
                    'func_idx': i,
                    'func_name': func['name'],
                    'func_end': func_end,
                    'next_func_idx': next_func_idx,
                    'next_func_name': next_func['name'],
                    'next_func_start': next_func['address'],
                    'delay_slot_addr': delay_slot_addr
                })

    print(f"Found {len(fixes_needed)} functions that need delay slot fixes")

    if fixes_needed:
        print("\nFirst 20 fixes needed:")
        for fix in fixes_needed[:20]:
            print(f"  {fix['func_name']} @ 0x{fix['func_end']-4:x} -> delay slot 0x{fix['delay_slot_addr']:x} ({fix['next_func_name']})")

    # Apply fixes: extend each function by 4 bytes to include delay slot,
    # and shrink the next function by 4 bytes
    for fix in fixes_needed:
        func = functions[fix['func_idx']]
        next_func = functions[fix['next_func_idx']]

        # Extend current function to include delay slot
        func['size'] += 4

        # Shrink next function (starts 4 bytes later)
        next_func['address'] += 4
        next_func['size'] -= 4

        # If next function becomes empty, mark for removal
        if next_func['size'] <= 0:
            next_func['_remove'] = True

    # Remove empty functions
    functions = [f for f in functions if not f.get('_remove', False)]

    # Sort by address again
    functions.sort(key=lambda f: f['address'])

    # Write output
    if output_path is None:
        output_path = json_path

    with open(output_path, 'w') as f:
        json.dump(functions, f, indent=2)

    print(f"\nWrote fixed functions to: {output_path}")
    print(f"Total functions: {len(functions)}")

if __name__ == "__main__":
    import os
    os.chdir("C:/Users/User/Documents/decompilations/sly1-ps2-decomp/tools/ps2recomp")

    elf_path = "C:/Users/User/Documents/decompilations/sly1-ps2-decomp/disc/SCUS_971.98"
    json_path = "sly1_functions.json"

    if len(sys.argv) > 1 and sys.argv[1] == '--apply':
        fix_delay_slot_boundaries(elf_path, json_path)
    else:
        # Dry run - don't modify the file
        fix_delay_slot_boundaries(elf_path, json_path, "sly1_functions_fixed.json")
        print("\nRun with --apply to modify sly1_functions.json in place")
