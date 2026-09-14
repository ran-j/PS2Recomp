#!/usr/bin/env python3
"""Emit native C++ VU block instantiations from user-supplied microcode images."""

import argparse
import struct
from pathlib import Path


def read_blocks(paths, parser):
    blocks = set()
    for path in paths:
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
    return blocks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--shards", type=int, default=0,
                        help="Emit this many independent native instantiation sources")
    parser.add_argument("--pair-images", type=Path, nargs="*", default=[],
                        help="Compile individual pairs from these images or profiles")
    parser.add_argument("images", type=Path, nargs="*")
    args = parser.parse_args()
    if args.shards < 0:
        parser.error("--shards must be nonnegative")
    blocks = read_blocks(args.images, parser)
    pair_blocks = blocks | read_blocks(args.pair_images, parser)
    pairs = {((word,), unit) for block, unit in pair_blocks for word in block}
    if not pairs:
        parser.error("no nonempty microcode blocks")
    lines = ["// Generated from local microcode; do not distribute game data."]
    externs = [lines[0]]
    shards = [[lines[0], '#include "ps2_vu1_exec.inl"'] for _ in range(args.shards)]
    for index, (block, unit) in enumerate(sorted(blocks | pairs)):
        values = ", ".join(f"0x{word:016x}ull" for word in block)
        lines.append(f"{{Unit::VU{unit}, {{{values}}}, &runCompiledBlock<Unit::VU{unit}, {values}>, {len(block) * 8}u}},")
        if args.shards:
            instance = (f"bool VU1Interpreter::runCompiledBlock<VU1Interpreter::Unit::VU{unit}, {values}>"
                        "(VU1Interpreter &, uint64_t);")
            externs.append(f"extern template {instance}")
            shards[index % args.shards].append(f"template {instance}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n")
    if args.shards:
        args.output.with_name(f"{args.output.stem}_extern.inc").write_text("\n".join(externs) + "\n")
        for index, source in enumerate(shards):
            args.output.with_name(f"{args.output.stem}_{index}.cpp").write_text("\n".join(source) + "\n")
    print(f"Compiled VU input: {len(blocks)} four-pair blocks, {len(pairs)} individual pairs")


if __name__ == "__main__":
    main()
