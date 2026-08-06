#!/usr/bin/env python3
"""Recover function entry points that are missing from a PS2Recomp function map.

Ghidra exports often miss functions that are only called via registers 
(like vtables or callbacks). When the game runs and tries to jump to these, 
the runtime crashes with a missing target error. 

This tool automates fixing this: it boots the game, catches the missing target, 
finds the end of the function, adds it to the CSV map, recompiles, rebuilds, and repeats.
Because it only tracks addresses the game actually jumps to, the map stays clean 
without speculative matches.

Everything is read from the recompiler's own TOML config, so it works for any
game without editing this file:

    python3 auto_fix_missing_targets.py --config config.toml

Requires capstone (pip install capstone) and a build that already runs far
enough to report a missing target.
"""

import argparse
import bisect
import csv
import os
import re
import shutil
import struct
import subprocess
import sys

try:
    from capstone import CS_ARCH_MIPS, CS_MODE_LITTLE_ENDIAN, CS_MODE_MIPS64, Cs
except ImportError:
    sys.exit("capstone is required: pip install capstone")


# Configuration parsing

def read_config(path):
    """Pull the few keys we need out of a PS2Recomp TOML config.

    We use regex instead of a full TOML parser to avoid requiring tomllib (Python 3.11+) 
    or a third-party dependency, since we only need three strings.
    """
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        text = handle.read()

    def key(name):
        match = re.search(r'^\s*%s\s*=\s*"([^"]*)"' % name, text, re.MULTILINE)
        return match.group(1) if match else None

    values = {name: key(name) for name in ("input", "ghidra_output", "output")}
    if not values["input"]:
        sys.exit("config %s has no [general] input = \"...\"" % path)
    if not values["ghidra_output"]:
        sys.exit("config %s has no ghidra_output; this tool edits that CSV" % path)
    return values


def resolve(path, base):
    """Resolve a config path the way the recompiler does: relative to the CWD it
    is invoked from, which is what `base` carries."""
    if os.path.isabs(path):
        return path
    return os.path.normpath(os.path.join(base, path))


# ELF Handling

class GuestImage:
    """Maps guest virtual addresses onto bytes of the ELF.

    We read the program headers to correctly map virtual addresses to file offsets 
    for any game, instead of hardcoding them.
    """

    def __init__(self, path):
        with open(path, "rb") as handle:
            self.data = handle.read()
        if self.data[:4] != b"\x7fELF":
            sys.exit("%s is not an ELF file" % path)
        if self.data[4] != 1:
            sys.exit("%s is not a 32-bit ELF" % path)

        phoff = struct.unpack_from("<I", self.data, 0x1C)[0]
        phentsize = struct.unpack_from("<H", self.data, 0x2A)[0]
        phnum = struct.unpack_from("<H", self.data, 0x2C)[0]
        self.segments = []
        for index in range(phnum):
            base = phoff + index * phentsize
            p_type, p_offset, p_vaddr, _, p_filesz, _ = struct.unpack_from(
                "<IIIIII", self.data, base)
            if p_type == 1 and p_filesz:  # PT_LOAD with file backing
                self.segments.append((p_vaddr, p_offset, p_filesz))
        if not self.segments:
            sys.exit("%s has no loadable segments" % path)

    def read(self, address, length):
        for vaddr, offset, size in self.segments:
            if vaddr <= address < vaddr + size:
                start = offset + (address - vaddr)
                available = size - (address - vaddr)
                return self.data[start:start + min(length, available)]
        return b""

    def contains(self, address):
        return any(vaddr <= address < vaddr + size
                   for vaddr, offset, size in self.segments)


# Function map parsing

class FunctionMap:
    """The Ghidra CSV: Name,Start,End,Size."""

    def __init__(self, path):
        self.path = path
        self.rows = []
        self.header = ["Name", "Start", "End", "Size"]
        if not os.path.exists(path):
            sys.exit("function map %s does not exist" % path)
        with open(path, "r", newline="", encoding="utf-8", errors="replace") as handle:
            reader = csv.reader(handle)
            for index, row in enumerate(reader):
                if len(row) < 4:
                    continue
                if index == 0 and not row[1].lower().startswith("0x"):
                    self.header = row
                    continue
                try:
                    self.rows.append((int(row[1], 16), int(row[2], 16), row[0]))
                except ValueError:
                    continue
        self.rows.sort()
        self._starts = [row[0] for row in self.rows]

    def covering(self, address):
        """The function whose body contains `address`, if any."""
        index = bisect.bisect_right(self._starts, address) - 1
        if index >= 0:
            start, end, name = self.rows[index]
            if start <= address < end:
                return (start, end, name)
        return None

    def next_start_after(self, address):
        index = bisect.bisect_right(self._starts, address)
        return self._starts[index] if index < len(self._starts) else None

    def overlaps(self, start, end):
        """Any existing entry intersecting [start, end)."""
        index = bisect.bisect_right(self._starts, start) - 1
        if index >= 0:
            row_start, row_end, name = self.rows[index]
            if row_start < end and start < row_end:
                return (row_start, row_end, name)
        index = bisect.bisect_left(self._starts, start)
        if index < len(self.rows):
            row_start, row_end, name = self.rows[index]
            if row_start < end and start < row_end:
                return (row_start, row_end, name)
        return None

    def append(self, start, end, name):
        with open(self.path, "a", newline="", encoding="utf-8") as handle:
            csv.writer(handle).writerow(
                [name, "0x%08X" % start, "0x%08X" % end, end - start])
        self.rows.append((start, end, name))
        self.rows.sort()
        self._starts = [row[0] for row in self.rows]


# Finding function bounds

def find_function_end(image, function_map, start, window=0x4000):
    """End address of the function at `start`, or None.

    Finds the first `jr $ra` (return) instruction and includes its delay slot. 
    We clamp the result to the next known function to ensure a bad decode 
    doesn't accidentally swallow the neighbor function.
    """
    limit = function_map.next_start_after(start)
    span = window if limit is None else min(window, limit - start)
    if span <= 0:
        return None

    code = image.read(start, span)
    if len(code) < 8:
        return None

    md = Cs(CS_ARCH_MIPS, CS_MODE_MIPS64 + CS_MODE_LITTLE_ENDIAN)
    md.skipdata = True
    for instruction in md.disasm(code, start):
        if instruction.mnemonic == "jr" and instruction.op_str == "$ra":
            end = instruction.address + 8  # delay slot belongs to the function
            if limit is not None and end > limit:
                return None
            return end
    return None


# Build and run execution

MISSING_RE = re.compile(r"\[guest-branch:missing-target\][^\n]*?target=(0x[0-9a-fA-F]+)")


def run_game(command, timeout, verbose):
    if verbose:
        print("  $ %s (timeout: %ds)" % (command, timeout))
    try:
        result = subprocess.run(command, shell=True, capture_output=True, text=True, timeout=timeout)
        output = (result.stdout or "") + (result.stderr or "")
    except subprocess.TimeoutExpired as e:
        output = (e.stdout or "") + (e.stderr or "")
        if isinstance(output, bytes):
            output = output.decode('utf-8', errors='ignore')
    match = MISSING_RE.search(output)
    return (int(match.group(1), 16) if match else None), output


def run_step(command, label):
    print("  %s: %s" % (label, command))
    result = subprocess.run(command, shell=True)
    if result.returncode != 0:
        sys.exit("%s failed (exit %d)" % (label, result.returncode))




def main():
    parser = argparse.ArgumentParser(
        description="Append runtime-reported missing function entry points to a "
                    "PS2Recomp function map, then recompile and rebuild.")
    parser.add_argument("--config", required=True,
                        help="recompiler TOML config (input/ghidra_output are read from it)")
    parser.add_argument("--run", required=True,
                        help="command that boots the game, e.g. "
                             "'./build-run/ps2xRuntime/ps2EntryRunner game.elf'")
    parser.add_argument("--recomp", default="./build-run/ps2xRecomp/ps2_recomp",
                        help="recompiler binary (default: %(default)s)")
    parser.add_argument("--build", default="cmake --build build-run --target ps2EntryRunner -j 8",
                        help="build command (default: %(default)s)")
    parser.add_argument("--cwd", default=".",
                        help="directory the config's relative paths resolve against")
    parser.add_argument("--timeout", type=int, default=20,
                        help="seconds to let the game run each pass (default: %(default)s)")
    parser.add_argument("--max-iterations", type=int, default=15)
    parser.add_argument("--name-prefix", default="REC_",
                        help="prefix for recovered names (default: %(default)s)")
    parser.add_argument("--scan-only", action="store_true",
                        help="report the next missing target and exit without editing anything")
    parser.add_argument("--dry-run", action="store_true",
                        help="do everything except writing the CSV and rebuilding")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    config = read_config(args.config)
    elf_path = resolve(config["input"], args.cwd)
    csv_path = resolve(config["ghidra_output"], args.cwd)
    print("ELF        : %s" % elf_path)
    print("function map: %s" % csv_path)

    image = GuestImage(elf_path)
    function_map = FunctionMap(csv_path)
    print("known functions: %d" % len(function_map.rows))

    backup = csv_path + ".bak"
    if not args.dry_run and not args.scan_only and not os.path.exists(backup):
        shutil.copy2(csv_path, backup)
        print("backup      : %s" % backup)

    recompile_cmd = "%s %s" % (args.recomp, args.config)
    attempted = set()
    added = 0

    for iteration in range(1, args.max_iterations + 1):
        print("\n--- pass %d ---" % iteration)
        target, _ = run_game(args.run, args.timeout, args.verbose)

        if target is None:
            print("no missing target reported — nothing left for this tool to fix")
            break

        print("missing target: 0x%08X" % target)
        if args.scan_only:
            break

        if target in attempted:
            print("already handled 0x%08X this session and it came back — stopping so "
                  "the loop cannot spin. It is probably reached mid-function, or the "
                  "rebuild did not pick up the map." % target)
            break
        attempted.add(target)

        if not image.contains(target):
            print("0x%08X is not inside any loadable segment — not a function" % target)
            break

        covering = function_map.covering(target)
        if covering:
            start, end, name = covering
            print("0x%08X already lies inside %s (0x%08X..0x%08X). A CSV row cannot "
                  "express a mid-function entry, so this one needs the recompiler's "
                  "internal-entry handling rather than a map edit." % (target, name, start, end))
            break

        end = find_function_end(image, function_map, target)
        if end is None:
            print("could not determine where the function at 0x%08X ends "
                  "(no `jr $ra` before the next known function)" % target)
            break

        clash = function_map.overlaps(target, end)
        if clash:
            print("0x%08X..0x%08X would overlap %s (0x%08X..0x%08X) — refusing"
                  % (target, end, clash[2], clash[0], clash[1]))
            break

        name = "%s%08x" % (args.name_prefix, target)
        print("adding %s,0x%08X,0x%08X,%d" % (name, target, end, end - target))
        if args.dry_run:
            print("(dry run: not written)")
            break

        function_map.append(target, end, name)
        added += 1
        run_step(recompile_cmd, "recompile")
        run_step(args.build, "build")

    print("\nrecovered %d function(s)" % added)
    if added:
        print("original map kept at %s" % backup)


if __name__ == "__main__":
    main()
