#!/usr/bin/env python3
"""Emit native C++ VU block instantiations from user-supplied microcode images."""

import argparse
import struct
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("images", type=Path, nargs="+")
    args = parser.parse_args()
    blocks = set()
    for path in args.images:
        data = path.read_bytes()
        if data.startswith(b"VU-BLOCKS 1\n"):
            for line in data.decode("ascii").splitlines()[1:]:
                fields = line.split()
                if len(fields) != 5:
                    parser.error(f"{path}: invalid block profile record")
                try:
                    unit = int(fields[0])
                    block = tuple(int(word, 16) for word in fields[1:])
                except ValueError:
                    parser.error(f"{path}: invalid numeric block profile record")
                if unit not in (0, 1) or any(word < 0 or word >= 1 << 64 for word in block):
                    parser.error(f"{path}: invalid unit or instruction bits")
                blocks.add((block, unit))
            continue
        if len(data) not in (4096, 16384):
            parser.error(f"{path}: expected a 4 KiB VU0 or 16 KiB VU1 code image")
        unit = 0 if len(data) == 4096 else 1
        words = [word for (word,) in struct.iter_unpack("<Q", data)]
        for index in range(len(words) - 3):
            block = tuple(words[index:index + 4])
            if any(block):
                blocks.add((block, unit))
    if not blocks:
        parser.error("no nonempty microcode blocks")
    lines = ["// Generated from local microcode; do not distribute game data."]
    for block, unit in sorted(blocks):
        values = ", ".join(f"0x{word:016x}ull" for word in block)
        lines.append(f"{{Unit::VU{unit}, {{{values}}}, &runCompiledBlock<Unit::VU{unit}, {values}>}},")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n")
    print(f"Compiled VU input: {len(args.images)} images, {len(blocks)} distinct four-pair blocks")


if __name__ == "__main__":
    main()
