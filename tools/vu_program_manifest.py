#!/usr/bin/env python3
"""Describe recorded VU1 microcode by where it sits in the game's own files.

A PS2_VU_PROGRAM_PROFILE recording holds 16 KiB code images, which are game
data and cannot be distributed. The routines compile_vu_programs.py builds
from them only read a small footprint of each image, though, and that code
also sits in the game's files. A manifest records, for every image, the
footprint's runs of instruction pairs as file offsets, and one checksum over
all of them; a checksum per run would give the shortest runs' code away:

    make     recordings + game files -> manifest (no game data inside)
    expand   manifest + a user's game files -> a profile directory that
             compile_vu_programs.py compiles exactly like the recordings

Pairs outside the footprints are left zero; the compiler never reads them.
"""

import argparse
import importlib.util
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("compile_vu_programs", HERE / "compile_vu_programs.py")
compiler = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(compiler)

CODE_SIZE = compiler.CODE_SIZE
PAIRS = compiler.PAIRS


def fnv(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & (2**64 - 1)
    return value


def footprint_union(pairs, entries):
    """Every pair the routines reachable from these entries read."""
    indices, work, seen = set(), list(entries), set()
    while work:
        start = work.pop()
        if start in seen or start >= PAIRS:
            continue
        seen.add(start)
        routine = compiler.Routine(pairs, start)
        indices.update(routine.footprint())
        work.extend(routine.calls)
    return sorted(indices)


def runs_of(indices):
    runs, start = [], None
    for position, index in enumerate(indices):
        if start is None:
            start = index
        if position + 1 == len(indices) or indices[position + 1] != index + 1:
            runs.append((start, index + 1 - start))
            start = None
    return runs


def locate(chunk, files):
    for name, data in files:
        offset = data.find(chunk)
        if offset >= 0:
            return name, offset
    return None


def make(args, parser):
    images, entries = compiler.load_images(args.profiles, parser)
    by_image = {}
    for name, entry in entries:
        by_image.setdefault(name, set()).add(entry)
    files = [(path.name, path.read_bytes()) for path in args.game]
    lines = ["# VU1 microcode compiled into the runtime, located in the game's files.",
             "# image <entries> <fnv64 of its runs>; run <first pair> <pairs> <file> <offset>"]
    written, unplaced, seen = 0, 0, set()
    for name in sorted(by_image):
        image = images[name]
        raw = b"".join(struct.pack("<II", pair.lower, pair.upper) for pair in image)
        placed = []
        for first, count in runs_of(footprint_union(image, sorted(by_image[name]))):
            # A run the files do not hold whole may still be there in pieces.
            pending = [(first, count)]
            while pending:
                start, length = pending.pop()
                chunk = raw[start * 8:(start + length) * 8]
                where = locate(chunk, files)
                if where:
                    placed.append((start, length, where[0], where[1], chunk))
                elif length > 1:
                    half = length // 2
                    pending += [(start + half, length - half), (start, half)]
                else:
                    placed = None
                    break
            if placed is None:
                break
        if placed is None:
            unplaced += 1
            continue
        placed.sort(key=lambda run: run[0])
        key = (tuple(sorted(by_image[name])), tuple(run[:4] for run in placed))
        if key in seen:
            continue
        seen.add(key)
        checksum = fnv(b"".join(run[4] for run in placed))
        lines.append("image " + ",".join(f"{entry * 8:x}" for entry in sorted(by_image[name]))
                     + f" {checksum:016x}")
        lines += [f"run {start} {length} {file} {offset}" for start, length, file, offset, _ in placed]
        written += 1
    args.output.write_text("\n".join(lines) + "\n")
    print(f"vu_program_manifest: {written} images from {len(by_image)} recorded, "
          f"{unplaced} with code the game files do not hold")


def expand(args, parser):
    files = {}
    images, current = [], None
    for number, line in enumerate(args.manifest.read_text().splitlines(), 1):
        fields = line.split()
        if not fields or fields[0].startswith("#"):
            continue
        if fields[0] == "image" and len(fields) == 3:
            current = ([int(entry, 16) for entry in fields[1].split(",")], int(fields[2], 16), [])
            images.append(current)
        elif fields[0] == "run" and len(fields) == 5 and current is not None:
            current[2].append((int(fields[1]), int(fields[2]), fields[3], int(fields[4])))
        else:
            parser.error(f"{args.manifest}:{number}: unexpected line")
    args.output.mkdir(parents=True, exist_ok=True)
    listing, kept, stale = [], 0, 0
    for entries, checksum, runs in images:
        chunks = []
        for start, length, file, offset in runs:
            if file not in files:
                path = args.game_root / file
                files[file] = path.read_bytes() if path.is_file() else b""
            chunks.append(files[file][offset:offset + length * 8])
        if any(len(chunk) != run[1] * 8 for chunk, run in zip(chunks, runs)) \
                or fnv(b"".join(chunks)) != checksum:
            stale += 1
            continue
        image = bytearray(CODE_SIZE)
        for (start, length, _, _), chunk in zip(runs, chunks):
            image[start * 8:(start + length) * 8] = chunk
        name = f"{fnv(image):016x}"
        (args.output / f"vu1-{name}.code").write_bytes(image)
        listing += [f"{name} {entry:x}" for entry in entries]
        kept += 1
    (args.output / "entries.txt").write_text("\n".join(listing) + "\n")
    print(f"vu_program_manifest: {kept} images expanded"
          + (f", {stale} skipped: their code differs in these game files" if stale else ""))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    p_make = sub.add_parser("make", help="write a manifest from recordings")
    p_make.add_argument("--output", type=Path, required=True)
    p_make.add_argument("--game", type=Path, action="append", required=True,
                        help="game file to look the microcode up in (repeatable)")
    p_make.add_argument("profiles", type=Path, nargs="+")
    p_expand = sub.add_parser("expand", help="rebuild a profile directory from a manifest")
    p_expand.add_argument("--manifest", type=Path, required=True)
    p_expand.add_argument("--game-root", type=Path, required=True)
    p_expand.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    (make if args.command == "make" else expand)(args, parser)


if __name__ == "__main__":
    sys.exit(main())
