#!/usr/bin/env python3
"""
Extract symbols from Sly 1 decomp project and generate PS2Recomp config.
Parses symbol_addrs.txt to create a functions list with addresses and sizes.
"""

import re
import json
from pathlib import Path

def parse_symbol_addrs(filepath: Path) -> list[dict]:
    """Parse symbol_addrs.txt and extract functions."""
    functions = []

    # Pattern: FunctionName = 0xADDRESS; // type:func
    pattern = re.compile(r'^(\w+)\s*=\s*(0x[0-9A-Fa-f]+)\s*;\s*//\s*type:func')

    with open(filepath, 'r') as f:
        for line in f:
            match = pattern.match(line.strip())
            if match:
                name = match.group(1)
                addr = int(match.group(2), 16)
                functions.append({
                    'name': name,
                    'address': addr
                })

    # Sort by address
    functions.sort(key=lambda x: x['address'])

    # Calculate sizes (next_addr - current_addr)
    for i in range(len(functions) - 1):
        functions[i]['size'] = functions[i + 1]['address'] - functions[i]['address']

    # Last function - estimate size (assume 0x100 bytes or until end of text section)
    if functions:
        functions[-1]['size'] = 0x100  # Default estimate

    return functions


def generate_toml_functions(functions: list[dict]) -> str:
    """Generate TOML array of functions."""
    lines = ['# Auto-generated function list from symbol_addrs.txt',
             '# Total functions: {}'.format(len(functions)),
             '',
             '[[functions]]']

    for func in functions:
        lines.append('[[functions]]')
        lines.append(f'name = "{func["name"]}"')
        lines.append(f'address = "0x{func["address"]:X}"')
        lines.append(f'size = "0x{func["size"]:X}"')
        lines.append('')

    return '\n'.join(lines)


def generate_json_functions(functions: list[dict]) -> str:
    """Generate JSON array of functions."""
    return json.dumps(functions, indent=2)


def main():
    # Paths
    project_root = Path(__file__).parent.parent.parent
    symbol_addrs = project_root / 'config' / 'symbol_addrs.txt'
    output_json = Path(__file__).parent / 'sly1_functions.json'

    print(f"Reading symbols from: {symbol_addrs}")

    if not symbol_addrs.exists():
        print(f"Error: {symbol_addrs} not found")
        return 1

    functions = parse_symbol_addrs(symbol_addrs)
    print(f"Found {len(functions)} functions")

    # Show first few functions
    print("\nFirst 10 functions:")
    for func in functions[:10]:
        print(f"  {func['name']:40} @ 0x{func['address']:06X} (size: 0x{func['size']:X})")

    # Generate JSON output
    with open(output_json, 'w') as f:
        f.write(generate_json_functions(functions))
    print(f"\nWrote function list to: {output_json}")

    # Print some stats
    total_size = sum(f['size'] for f in functions)
    print(f"\nStats:")
    print(f"  Total functions: {len(functions)}")
    print(f"  Address range: 0x{functions[0]['address']:X} - 0x{functions[-1]['address']:X}")
    print(f"  Total code size: 0x{total_size:X} bytes ({total_size / 1024:.1f} KB)")

    return 0


if __name__ == '__main__':
    exit(main())
