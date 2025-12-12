#!/usr/bin/env python3
"""
inject.py - Debug injection script for PS2 Recompiler

This script injects debug prints into the PS2 runtime and generated code.
Run this after regenerating files to add debugging output.

Usage:
    python inject.py [options]

Options:
    --all           Enable all debug options
    --functions     Trace function calls
    --syscalls      Trace syscalls in detail
    --memory        Trace memory accesses
    --registers     Dump registers periodically
    --verbose       Extra verbose output
    --clean         Remove all debug injections
"""

import os
import re
import sys
import argparse
from pathlib import Path

# Base paths
SCRIPT_DIR = Path(__file__).parent
RUNTIME_DIR = SCRIPT_DIR / "ps2xRuntime"
RECOMP_OUTPUT_DIR = SCRIPT_DIR.parent.parent / "recomp_output"

# Files to modify
FILES = {
    "runtime": RUNTIME_DIR / "src" / "lib" / "ps2_runtime.cpp",
    "syscalls": RUNTIME_DIR / "src" / "lib" / "ps2_syscalls.cpp",
    "memory": RUNTIME_DIR / "src" / "lib" / "ps2_memory.cpp",
    "main": RECOMP_OUTPUT_DIR / "main.cpp",
    "register_functions": RECOMP_OUTPUT_DIR / "register_functions.cpp",
}

# Debug marker to identify injected code
DEBUG_MARKER = "/* DEBUG_INJECT */"

def read_file(path):
    """Read file content."""
    if not path.exists():
        print(f"Warning: File not found: {path}")
        return None
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        return f.read()

def write_file(path, content):
    """Write file content."""
    with open(path, 'w', encoding='utf-8') as f:
        f.write(content)
    print(f"Updated: {path}")

def remove_debug_injections(content):
    """Remove all debug injections from content."""
    if content is None:
        return None
    # Remove lines containing the debug marker
    lines = content.split('\n')
    filtered = [line for line in lines if DEBUG_MARKER not in line]
    return '\n'.join(filtered)

def inject_function_tracing(content):
    """Inject function call tracing into the runtime run() loop."""
    if content is None:
        return None

    # Find the run() loop and add detailed tracing
    old_pattern = r'if \(!func\) \{ std::cerr << "No func at 0x" << std::hex << pc << std::dec << std::endl; break; \}'
    new_code = '''if (!func) {
                    std::cerr << "No func at 0x" << std::hex << pc << std::dec << std::endl;
                    std::cerr << "  RA=0x" << std::hex << M128I_U32(m_cpuContext.r[31], 0) << std::dec << std::endl; ''' + DEBUG_MARKER + '''
                    m_cpuContext.dump(); ''' + DEBUG_MARKER + '''
                    break;
                }'''
    content = re.sub(old_pattern, new_code, content)

    # Add more detailed periodic logging
    old_pattern = r'if \(calls % 10000 == 0\) std::cout << "Calls: " << calls << ", PC: 0x" << std::hex << pc << std::dec << std::endl;'
    new_code = f'''if (calls % 1000 == 0) {{ {DEBUG_MARKER}
                    std::cout << "=== Call #" << calls << " ===" << std::endl; {DEBUG_MARKER}
                    std::cout << "  PC: 0x" << std::hex << pc << std::dec << std::endl; {DEBUG_MARKER}
                    std::cout << "  RA: 0x" << std::hex << M128I_U32(m_cpuContext.r[31], 0) << std::dec << std::endl; {DEBUG_MARKER}
                    std::cout << "  SP: 0x" << std::hex << M128I_U32(m_cpuContext.r[29], 0) << std::dec << std::endl; {DEBUG_MARKER}
                    std::cout << "  A0: 0x" << std::hex << M128I_U32(m_cpuContext.r[4], 0) << std::dec << std::endl; {DEBUG_MARKER}
                    std::cout << "  A1: 0x" << std::hex << M128I_U32(m_cpuContext.r[5], 0) << std::dec << std::endl; {DEBUG_MARKER}
                    std::cout << "  V0: 0x" << std::hex << M128I_U32(m_cpuContext.r[2], 0) << std::dec << std::endl; {DEBUG_MARKER}
                }}'''
    content = re.sub(old_pattern, new_code, content)

    return content

def inject_syscall_tracing(content):
    """Inject detailed syscall tracing."""
    if content is None:
        return None

    # Add syscall number lookup table
    syscall_table = '''
// PS2 EE Kernel Syscall Names ''' + DEBUG_MARKER + '''
static const char* getSyscallName(int num) { ''' + DEBUG_MARKER + '''
    switch(num) { ''' + DEBUG_MARKER + '''
        case 0x02: return "SetGsCrt"; ''' + DEBUG_MARKER + '''
        case 0x04: return "Exit"; ''' + DEBUG_MARKER + '''
        case 0x06: return "LoadExecPS2"; ''' + DEBUG_MARKER + '''
        case 0x07: return "ExecPS2"; ''' + DEBUG_MARKER + '''
        case 0x10: return "AddIntcHandler"; ''' + DEBUG_MARKER + '''
        case 0x11: return "RemoveIntcHandler"; ''' + DEBUG_MARKER + '''
        case 0x12: return "AddDmacHandler"; ''' + DEBUG_MARKER + '''
        case 0x13: return "RemoveDmacHandler"; ''' + DEBUG_MARKER + '''
        case 0x14: return "_EnableIntc"; ''' + DEBUG_MARKER + '''
        case 0x15: return "_DisableIntc"; ''' + DEBUG_MARKER + '''
        case 0x16: return "_EnableDmac"; ''' + DEBUG_MARKER + '''
        case 0x17: return "_DisableDmac"; ''' + DEBUG_MARKER + '''
        case 0x20: return "CreateThread"; ''' + DEBUG_MARKER + '''
        case 0x21: return "DeleteThread"; ''' + DEBUG_MARKER + '''
        case 0x22: return "StartThread"; ''' + DEBUG_MARKER + '''
        case 0x23: return "ExitThread"; ''' + DEBUG_MARKER + '''
        case 0x24: return "ExitDeleteThread"; ''' + DEBUG_MARKER + '''
        case 0x25: return "TerminateThread"; ''' + DEBUG_MARKER + '''
        case 0x29: return "ChangeThreadPriority"; ''' + DEBUG_MARKER + '''
        case 0x2a: return "iChangeThreadPriority"; ''' + DEBUG_MARKER + '''
        case 0x2b: return "RotateThreadReadyQueue"; ''' + DEBUG_MARKER + '''
        case 0x2f: return "GetThreadId"; ''' + DEBUG_MARKER + '''
        case 0x30: return "ReferThreadStatus"; ''' + DEBUG_MARKER + '''
        case 0x32: return "SleepThread"; ''' + DEBUG_MARKER + '''
        case 0x33: return "WakeupThread"; ''' + DEBUG_MARKER + '''
        case 0x34: return "iWakeupThread"; ''' + DEBUG_MARKER + '''
        case 0x37: return "SuspendThread"; ''' + DEBUG_MARKER + '''
        case 0x39: return "ResumeThread"; ''' + DEBUG_MARKER + '''
        case 0x3c: return "SetupThread"; ''' + DEBUG_MARKER + '''
        case 0x3d: return "SetupHeap"; ''' + DEBUG_MARKER + '''
        case 0x3e: return "EndOfHeap"; ''' + DEBUG_MARKER + '''
        case 0x40: return "CreateSema"; ''' + DEBUG_MARKER + '''
        case 0x41: return "DeleteSema"; ''' + DEBUG_MARKER + '''
        case 0x42: return "SignalSema"; ''' + DEBUG_MARKER + '''
        case 0x43: return "iSignalSema"; ''' + DEBUG_MARKER + '''
        case 0x44: return "WaitSema"; ''' + DEBUG_MARKER + '''
        case 0x45: return "PollSema"; ''' + DEBUG_MARKER + '''
        case 0x47: return "ReferSemaStatus"; ''' + DEBUG_MARKER + '''
        case 0x48: return "iReferSemaStatus"; ''' + DEBUG_MARKER + '''
        case 0x64: return "FlushCache"; ''' + DEBUG_MARKER + '''
        case 0x68: return "FlushCache (alt)"; ''' + DEBUG_MARKER + '''
        case 0x70: return "GsGetIMR"; ''' + DEBUG_MARKER + '''
        case 0x71: return "GsPutIMR"; ''' + DEBUG_MARKER + '''
        case 0x73: return "SetVSyncFlag"; ''' + DEBUG_MARKER + '''
        case 0x74: return "SetSyscall"; ''' + DEBUG_MARKER + '''
        case 0x76: return "SifDmaStat"; ''' + DEBUG_MARKER + '''
        case 0x77: return "SifSetDma"; ''' + DEBUG_MARKER + '''
        case 0x78: return "SifSetDChain"; ''' + DEBUG_MARKER + '''
        case 0x79: return "SifSetReg"; ''' + DEBUG_MARKER + '''
        case 0x7a: return "SifGetReg"; ''' + DEBUG_MARKER + '''
        case 0x7c: return "Deci2Call"; ''' + DEBUG_MARKER + '''
        case 0x7e: return "MachineType"; ''' + DEBUG_MARKER + '''
        case 0x7f: return "GetMemorySize"; ''' + DEBUG_MARKER + '''
        default: return "Unknown"; ''' + DEBUG_MARKER + '''
    } ''' + DEBUG_MARKER + '''
} ''' + DEBUG_MARKER + '''
'''

    # Find a good place to insert - after the includes
    insert_pos = content.find('#include "ps2_syscalls.h"')
    if insert_pos == -1:
        insert_pos = content.find('#include')

    if insert_pos != -1:
        # Find end of that include line
        end_include = content.find('\n', insert_pos) + 1
        content = content[:end_include] + syscall_table + content[end_include:]

    # Update the handleSyscall function to use the table
    old_pattern = r'void PS2Runtime::handleSyscall\(uint8_t \*rdram, R5900Context \*ctx\) \{ std::cout << "Syscall " << M128I_U32\(ctx->r\[3\], 0\) << " at PC: 0x" << std::hex << ctx->pc << std::dec << std::endl; \}'
    new_code = f'''void PS2Runtime::handleSyscall(uint8_t *rdram, R5900Context *ctx) {{
    int syscall_num = M128I_U32(ctx->r[3], 0); {DEBUG_MARKER}
    std::cout << "=== SYSCALL " << syscall_num << " (" << getSyscallName(syscall_num) << ") ===" << std::endl; {DEBUG_MARKER}
    std::cout << "  PC: 0x" << std::hex << ctx->pc << std::dec << std::endl; {DEBUG_MARKER}
    std::cout << "  RA: 0x" << std::hex << M128I_U32(ctx->r[31], 0) << std::dec << std::endl; {DEBUG_MARKER}
    std::cout << "  A0: 0x" << std::hex << M128I_U32(ctx->r[4], 0) << std::dec << std::endl; {DEBUG_MARKER}
    std::cout << "  A1: 0x" << std::hex << M128I_U32(ctx->r[5], 0) << std::dec << std::endl; {DEBUG_MARKER}
    std::cout << "  A2: 0x" << std::hex << M128I_U32(ctx->r[6], 0) << std::dec << std::endl; {DEBUG_MARKER}
    std::cout << "  A3: 0x" << std::hex << M128I_U32(ctx->r[7], 0) << std::dec << std::endl; {DEBUG_MARKER}
}}'''
    content = re.sub(old_pattern, new_code, content, flags=re.DOTALL)

    return content

def inject_memory_tracing(content):
    """Inject memory access tracing."""
    if content is None:
        return None

    # Add logging to write32
    old_pattern = r'(void PS2Memory::write32\(uint32_t address, uint32_t value\)\s*\{)'
    new_code = r'''\1
    if (address >= 0x10000000 && address < 0x20000000) { ''' + DEBUG_MARKER + '''
        std::cout << "HW Write32: 0x" << std::hex << address << " = 0x" << value << std::dec << std::endl; ''' + DEBUG_MARKER + '''
    } ''' + DEBUG_MARKER
    content = re.sub(old_pattern, new_code, content)

    return content

def inject_startup_info(content):
    """Inject startup info into main.cpp."""
    if content is None:
        return None

    # Add more startup info
    old_pattern = r'(std::cout << "ELF loaded successfully\." << std::endl;)'
    new_code = r'''\1
    std::cout << "=== DEBUG BUILD ===" << std::endl; ''' + DEBUG_MARKER + '''
    std::cout << "Debug injection active" << std::endl; ''' + DEBUG_MARKER
    content = re.sub(old_pattern, new_code, content)

    return content

def inject_register_functions_debug(content):
    """Inject debug into register_functions.cpp to show registered functions."""
    if content is None:
        return None

    # Count registrations
    old_pattern = r'(void registerAllFunctions\(PS2Runtime& runtime\)\s*\{)'
    new_code = r'''\1
    int count = 0; ''' + DEBUG_MARKER
    content = re.sub(old_pattern, new_code, content)

    # Add counter to each registration (this is a simplified version)
    # For a full version, we'd need to modify each line

    return content

def clean_all_files():
    """Remove debug injections from all files."""
    print("Cleaning debug injections...")
    for name, path in FILES.items():
        content = read_file(path)
        if content:
            cleaned = remove_debug_injections(content)
            if cleaned != content:
                write_file(path, cleaned)
                print(f"  Cleaned: {name}")
            else:
                print(f"  No changes: {name}")

def inject_all(args):
    """Inject debug code based on arguments."""
    print("Injecting debug code...")

    # Runtime file
    if args.functions or args.all:
        content = read_file(FILES["runtime"])
        if content:
            content = remove_debug_injections(content)
            content = inject_function_tracing(content)
            content = inject_syscall_tracing(content)
            write_file(FILES["runtime"], content)

    # Memory file
    if args.memory or args.all:
        content = read_file(FILES["memory"])
        if content:
            content = remove_debug_injections(content)
            content = inject_memory_tracing(content)
            write_file(FILES["memory"], content)

    # Main file
    if args.verbose or args.all:
        content = read_file(FILES["main"])
        if content:
            content = remove_debug_injections(content)
            content = inject_startup_info(content)
            write_file(FILES["main"], content)

    print("\nDone! Rebuild with:")
    print("  cd recomp_output/build && ninja")

def fix_cmake_paths():
    """Fix the CMakeLists.txt to use correct library paths."""
    cmake_path = RECOMP_OUTPUT_DIR / "CMakeLists.txt"
    content = read_file(cmake_path)
    if content is None:
        return

    # Check if already fixed
    if "PS2RECOMP_BUILD_DIR" in content:
        print("CMakeLists.txt already has correct paths")
        return

    # Fix the library paths
    old_pattern = r'\$\{PS2RUNTIME_DIR\}/build/libps2_runtime\.a'
    new_path = '${PS2RECOMP_BUILD_DIR}/ps2xRuntime/libps2_runtime.a'
    content = re.sub(old_pattern, new_path, content)

    old_pattern = r'\$\{PS2RUNTIME_DIR\}/build/_deps/raylib-build/raylib/libraylib\.a'
    new_path = '${PS2RECOMP_BUILD_DIR}/_deps/raylib-build/raylib/libraylib.a'
    content = re.sub(old_pattern, new_path, content)

    # Add PS2RECOMP_BUILD_DIR variable if not present
    if "PS2RECOMP_BUILD_DIR" not in content:
        old_line = 'set(PS2RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../tools/ps2recomp/ps2xRuntime")'
        new_line = old_line + '\nset(PS2RECOMP_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../tools/ps2recomp/build")'
        content = content.replace(old_line, new_line)

    write_file(cmake_path, content)
    print("Fixed CMakeLists.txt library paths")

def main():
    parser = argparse.ArgumentParser(description="Inject debug prints into PS2 recompiler")
    parser.add_argument('--all', action='store_true', help='Enable all debug options')
    parser.add_argument('--functions', action='store_true', help='Trace function calls')
    parser.add_argument('--syscalls', action='store_true', help='Trace syscalls')
    parser.add_argument('--memory', action='store_true', help='Trace memory accesses')
    parser.add_argument('--registers', action='store_true', help='Dump registers')
    parser.add_argument('--verbose', action='store_true', help='Extra verbose output')
    parser.add_argument('--clean', action='store_true', help='Remove all debug injections')
    parser.add_argument('--fix-cmake', action='store_true', help='Fix CMakeLists.txt paths')

    args = parser.parse_args()

    # Default to --all if no options specified
    if not any([args.all, args.functions, args.syscalls, args.memory,
                args.registers, args.verbose, args.clean, args.fix_cmake]):
        args.all = True
        args.fix_cmake = True

    if args.fix_cmake:
        fix_cmake_paths()

    if args.clean:
        clean_all_files()
    elif not args.fix_cmake or args.all:
        inject_all(args)

    print("\n=== Build Instructions ===")
    print("1. Rebuild runtime:   cd tools/ps2recomp && ninja -C build")
    print("2. Rebuild game:      cd recomp_output/build && ninja clean && ninja")
    print("3. Run:               cd recomp_output/build && ./SlyCooperRecompiled.exe")

if __name__ == "__main__":
    main()
