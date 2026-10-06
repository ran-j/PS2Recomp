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

A recording only holds the programs that ran while it was made. With
--uploads, make also looks for the DMA packets that upload microcode with VIF
MPG commands in the game files. A program no recording holds is described in a
recorded image in place of one loaded at the same address, entered where that
one was.
"""

import argparse
import importlib.util
import struct
import sys
from pathlib import Path, PurePosixPath

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


def raw_code(image):
    return b"".join(struct.pack("<II", pair.lower, pair.upper) for pair in image)


# DMA tags followed by their data: cnt, next, call, ret and end.
INLINE_TAGS = {1, 2, 5, 6, 7}
# One-word VIF commands, from NOP to MSCNT, that may sit around an MPG.
ONE_WORD_VIF = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x10, 0x11, 0x13, 0x14, 0x15, 0x17}


def uploads_in(data):
    """The VIF MPG uploads of each DMA packet in a file, as (offset, first pair, pairs)."""
    packets = []
    for tag in range(0, len(data) - 15, 16):
        word, = struct.unpack_from("<I", data, tag)
        end = tag + 16 + (word & 0xFFFF) * 16
        if (word >> 28) & 7 not in INLINE_TAGS or end == tag + 16 or end > len(data):
            continue
        # The tag's upper half carries the first two VIF codes.
        uploads, position = [], tag + 8
        while position < end:
            code, = struct.unpack_from("<I", data, position)
            position += 4
            command = (code >> 24) & 0x7F
            if command == 0x4A:
                # The data follows from any word, as the runtime's VIF reads it.
                count, first = (code >> 16) & 0xFF or 256, code & 0xFFFF
                if position + count * 8 > end or first + count > PAIRS:
                    break
                uploads.append((position, first, count))
                position += count * 8
            elif command not in ONE_WORD_VIF or (command == 0 and code):
                break
        else:
            if uploads:
                packets.append(uploads)
    return packets


def unrecorded_uploads(images, by_image, files):
    """Upload packets no recording holds, each put in a recorded image in place of one
    loaded at the same address, with the entries recorded at that address."""
    raw = {name: raw_code(images[name]) for name in by_image}
    packets = [[(data[offset:offset + count * 8], first, count) for offset, first, count in packet]
               for _, data in files for packet in uploads_in(data)]

    def holds(code, packet):
        return all(code[first * 8:(first + count) * 8] == chunk for chunk, first, count in packet)

    def inside(pc, packet):
        return any(first <= pc < first + count for _, first, count in packet)

    held, siblings = set(), {}
    for index, packet in enumerate(packets):
        for name in sorted(raw):
            if holds(raw[name], packet):
                held.add(index)
                siblings.setdefault(packet[0][1], []).append((name, packet))
    for index, packet in enumerate(packets):
        if index in held or packet[0][1] not in siblings:
            continue
        entries = {pc for name, sibling in siblings[packet[0][1]] for pc in by_image[name]
                   if inside(pc, sibling) and inside(pc, packet)}
        if not entries:
            continue
        # Calls leave the upload, so the rest of the image stays as recorded.
        name, sibling = siblings[packet[0][1]][0]
        code = bytearray(raw[name])
        for _, first, count in sibling:
            code[first * 8:(first + count) * 8] = bytes(count * 8)
        for chunk, first, count in packet:
            code[first * 8:(first + count) * 8] = chunk
        yield [compiler.Pair(*struct.unpack_from("<II", code, i)) for i in range(0, CODE_SIZE, 8)], entries


def place(image, entries, files):
    """The image's footprint runs as they sit in the files, or None if some pair is not there."""
    raw = raw_code(image)
    placed = []
    for first, count in runs_of(footprint_union(image, sorted(entries))):
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
                return None
    return placed


def bad_file_name(name):
    """Why a manifest file name could reach outside the game root, or None if it can't."""
    # Checked as text, so Windows root- and drive-relative forms fail everywhere.
    path = PurePosixPath(name)
    if "\\" in name or ":" in name or path.is_absolute() or ".." in path.parts:
        return "game files must be relative paths below the game root, separated by /"
    if len(name.split()) != 1:
        return "manifest file names cannot contain spaces"
    return None


def game_file_name(path, root, parser):
    """How the manifest names a game file: its path below the game root, else its name."""
    if root is None:
        name = path.name
    else:
        try:
            name = path.resolve().relative_to(root.resolve()).as_posix()
        except ValueError:
            parser.error(f"{path} is not inside the game root {root}")
    problem = bad_file_name(name)
    if problem:
        parser.error(f"{name}: {problem}")
    return name


def make(args, parser):
    images, entries = compiler.load_images(args.profiles, parser)
    by_image = {}
    for name, entry in entries:
        by_image.setdefault(name, set()).add(entry)
    files = [(game_file_name(path, args.game_root, parser), path.read_bytes()) for path in args.game]
    work = [(images[name], by_image[name]) for name in sorted(by_image)]
    extra = list(unrecorded_uploads(images, by_image, files)) if args.uploads else []
    lines = ["# VU1 microcode compiled into the runtime, located in the game's files.",
             "# image <entries> <fnv64 of its runs>; run <first pair> <pairs> <file> <offset>"]
    written, unplaced, seen = 0, 0, set()
    for image, image_entries in work + extra:
        placed = place(image, image_entries, files)
        if placed is None:
            unplaced += 1
            continue
        placed.sort(key=lambda run: run[0])
        key = (tuple(sorted(image_entries)), tuple(run[:4] for run in placed))
        if key in seen:
            continue
        seen.add(key)
        checksum = fnv(b"".join(run[4] for run in placed))
        lines.append("image " + ",".join(f"{entry * 8:x}" for entry in sorted(image_entries))
                     + f" {checksum:016x}")
        lines += [f"run {start} {length} {file} {offset}" for start, length, file, offset, _ in placed]
        written += 1
    args.output.write_text("\n".join(lines) + "\n")
    print(f"vu_program_manifest: {written} images from {len(by_image)} recorded"
          + (f" and {len(extra)} unrecorded uploads" if args.uploads else "")
          + f", {unplaced} with code the game files do not hold")


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
            problem = bad_file_name(fields[3])
            if problem:
                parser.error(f"{args.manifest}:{number}: {fields[3]}: {problem}")
            current[2].append((int(fields[1]), int(fields[2]), fields[3], int(fields[4])))
        else:
            parser.error(f"{args.manifest}:{number}: unexpected line")
    args.output.mkdir(parents=True, exist_ok=True)
    listing, kept, stale = [], 0, 0
    for entries, checksum, runs in images:
        chunks = []
        for start, length, file, offset in runs:
            if file not in files:
                path = args.game_root.joinpath(*PurePosixPath(file).parts)
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
    p_make.add_argument("--game-root", type=Path,
                        help="disc root; game files are then named by their path below it")
    p_make.add_argument("--uploads", action="store_true",
                        help="also describe microcode the game files upload but no recording holds")
    p_make.add_argument("profiles", type=Path, nargs="+")
    p_expand = sub.add_parser("expand", help="rebuild a profile directory from a manifest")
    p_expand.add_argument("--manifest", type=Path, required=True)
    p_expand.add_argument("--game-root", type=Path, required=True)
    p_expand.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    (make if args.command == "make" else expand)(args, parser)


if __name__ == "__main__":
    sys.exit(main())
