#!/usr/bin/env python3
"""Compile VU1 microprogram routines to C++ from locally recorded microcode.

Inputs are directories written by a run with PS2_VU_PROGRAM_PROFILE set: 16 KiB
VU1 code images named vu1-<hash>.code and an entries.txt of "<hash> <pc>"
lines. A routine is the code reachable from one entry through branches; calls
and register jumps end it, and their targets become routines of their own.
The output is generated from game microcode and must stay local.
"""

import argparse
import struct
from pathlib import Path

CODE_SIZE = 16384
PAIRS = CODE_SIZE // 8
# Run::kMaxBlockPairs: each block reserves flag queue room for its pairs.
MAX_BLOCK_PAIRS = 128
BRANCHES = {0x20, 0x21, 0x24, 0x25, 0x28, 0x29, 0x2C, 0x2D, 0x2E, 0x2F}
CONDITIONAL = {0x28, 0x29, 0x2C, 0x2D, 0x2E, 0x2F}
CALLS = {0x21, 0x25}
REGISTER_JUMPS = {0x24, 0x25}
LOWER_OPS = {0x00, 0x01, 0x04, 0x05, 0x08, 0x09, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
             0x16, 0x17, 0x18, 0x1A, 0x1B, 0x1C} | BRANCHES
LOWER_DIRECT = {0x30, 0x31, 0x32, 0x34, 0x35}
LOWER_SPECIAL = {0x30, 0x31, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D,
                 0x3E, 0x3F, 0x64, 0x68, 0x69, 0x6C} | set(range(0x70, 0x7E))
FDIV_SPECIAL = {0x38, 0x39, 0x3A, 0x3B}
EFU_SPECIAL = set(range(0x70, 0x7E))


def upper_supported(upper):
    op = upper & 0x3F
    if op <= 0x2F:
        return True
    if op < 0x3C:
        return False
    special = (upper & 3) | ((upper >> 4) & 0x7C)
    return special <= 0x2A or special in (0x2C, 0x2D, 0x2E, 0x2F, 0x30)


def lower_special(lower):
    return (lower & 3) | ((lower >> 4) & 0x7C)


def lower_supported(lower):
    if lower in (0, 0x8000033C):
        return True
    op = lower >> 25
    if op in LOWER_OPS:
        return True
    if op != 0x40:
        return False
    direct = lower & 0x3F
    if direct in LOWER_DIRECT:
        return True
    return direct >= 0x3C and lower_special(lower) in LOWER_SPECIAL


class Pair:
    __slots__ = ("lower", "upper")

    def __init__(self, lower, upper):
        self.lower = lower
        self.upper = upper

    @property
    def immediate(self):
        return bool(self.upper & 0x80000000)

    @property
    def ebit(self):
        return bool(self.upper & 0x40000000)

    @property
    def op(self):
        return None if self.immediate else self.lower >> 25

    @property
    def branch(self):
        return self.op in BRANCHES

    @property
    def supported(self):
        return upper_supported(self.upper) and (self.immediate or lower_supported(self.lower))

    @property
    def word(self):
        return self.lower | (self.upper << 32)

    def branch_target(self, index):
        offset = self.lower & 0x7FF
        if offset & 0x400:
            offset -= 0x800
        return (index + 1 + offset) & (PAIRS - 1)

    def stall_bound(self):
        """Cycles the pair can take, stalls included, ignoring PATH1 waits."""
        bound = 5
        if self.immediate or self.lower >> 25 != 0x40 or (self.lower & 0x3F) < 0x3C:
            return bound
        special = lower_special(self.lower)
        if special in FDIV_SPECIAL:
            bound += 13
        if special in EFU_SPECIAL:
            bound += 54
        if special == 0x6C:
            bound += 8192
        return bound


def load_images(directories, parser):
    images = {}
    entries = []
    for directory in directories:
        listing = directory / "entries.txt"
        if not listing.is_file():
            parser.error(f"{directory}: no entries.txt; record with PS2_VU_PROGRAM_PROFILE")
        for number, line in enumerate(listing.read_text().splitlines(), 1):
            fields = line.split()
            if not fields:
                continue
            try:
                name, pc = fields[0], int(fields[1], 16)
            except (IndexError, ValueError):
                parser.error(f"{listing}:{number}: expected '<hash> <pc>'")
            if name not in images:
                data = (directory / f"vu1-{name}.code").read_bytes()
                if len(data) != CODE_SIZE:
                    parser.error(f"{directory}/vu1-{name}.code: expected a 16 KiB VU1 code image")
                images[name] = [Pair(*struct.unpack_from("<II", data, i)) for i in range(0, CODE_SIZE, 8)]
            if pc % 8 or pc >= CODE_SIZE:
                parser.error(f"{listing}:{number}: invalid entry {pc:#x}")
            entries.append((name, pc // 8))
    return images, entries


def constant_target(pairs, block_start, jump_index):
    """Resolve a register jump set by IADDIU from VI0 earlier in its block."""
    reg = (pairs[jump_index].lower >> 11) & 15
    if reg == 0:
        return 0
    for index in range(jump_index - 1, block_start - 1, -1):
        pair = pairs[index]
        if pair.immediate:
            continue
        lower = pair.lower
        op = lower >> 25
        writes = None
        if op in (0x04, 0x08, 0x09):
            writes = (lower >> 16) & 15
        elif op in (0x14, 0x16, 0x17, 0x18, 0x1A, 0x1B, 0x1C):
            writes = (lower >> 16) & 15
        elif op in (0x10, 0x12, 0x13):
            writes = 1
        elif op == 0x40:
            direct = lower & 0x3F
            if direct in (0x30, 0x31, 0x34, 0x35):
                writes = (lower >> 6) & 15
            elif direct == 0x32:
                writes = (lower >> 16) & 15
            elif direct >= 0x3C:
                special = lower_special(lower)
                if special in (0x34, 0x36):
                    writes = (lower >> 11) & 15
                elif special in (0x35, 0x37, 0x3C, 0x3E, 0x68, 0x69):
                    writes = (lower >> 16) & 15
        if writes != reg:
            continue
        if op == 0x08 and (lower >> 11) & 15 == 0:
            return ((lower & 0x7FF) | ((lower >> 10) & 0x7800)) & (PAIRS - 1)
        return None
    return None


class Routine:
    def __init__(self, pairs, entry):
        self.pairs = pairs
        self.entry = entry
        self.blocks = {}   # start -> (pair indices, terminator)
        self.calls = set() # further routine entries this one can reach
        self.build()

    def terminates(self, index):
        """Why the straight line from index cannot simply continue, if at all."""
        pair = self.pairs[index]
        if not pair.supported or index + 1 >= PAIRS:
            return ("leave",)
        if pair.branch or pair.ebit:
            delay = self.pairs[index + 1]
            if not delay.supported or delay.branch or delay.ebit or index + 2 > PAIRS:
                return ("leave",)
        return None

    def build(self):
        leaders = {self.entry}
        work = [self.entry]
        visited = set()
        while work:
            index = work.pop()
            while index not in visited:
                visited.add(index)
                if self.terminates(index):
                    break
                pair = self.pairs[index]
                if pair.branch:
                    op = pair.op
                    targets = []
                    if op in (0x20,) + tuple(CONDITIONAL):
                        targets.append(pair.branch_target(index))
                    if op in CONDITIONAL:
                        targets.append(index + 2)
                    if not pair.ebit:
                        for target in targets:
                            if target not in leaders:
                                leaders.add(target)
                                work.append(target)
                    break
                if pair.ebit:
                    break
                index += 1
                if index >= PAIRS:
                    break
        pending = sorted(leaders)
        while pending:
            start = pending.pop()
            if start in self.blocks:
                continue
            self.blocks[start] = body, terminator = self.walk(start, leaders)
            # A long straight run was cut; the rest becomes a block of its own.
            if terminator[0] == "fall" and terminator[1] not in leaders:
                leaders.add(terminator[1])
                pending.append(terminator[1])

    def walk(self, start, leaders):
        body = []
        index = start
        while True:
            # Room for a branch and its delay slot must remain.
            if len(body) >= MAX_BLOCK_PAIRS - 1:
                return body, ("fall", index)
            reason = self.terminates(index)
            if reason:
                return body, ("leave", index)
            pair = self.pairs[index]
            if pair.branch:
                body += [index, index + 1]
                op = pair.op
                after = index + 2
                if op in REGISTER_JUMPS:
                    target = constant_target(self.pairs, start, index)
                    if op in CALLS:
                        self.calls.add(after)
                    if target is not None:
                        self.calls.add(target)
                    kind = "call" if op in CALLS else "jump"
                    return body, (kind, target, pair.ebit)
                target = pair.branch_target(index)
                if op == 0x21:
                    self.calls.update((target, after))
                    return body, ("call", target, pair.ebit)
                if pair.ebit:
                    return body, ("end-branch", target, op in CONDITIONAL, after)
                if op == 0x20:
                    return body, ("goto", target)
                return body, ("cond", target, after)
            if pair.ebit:
                body += [index, index + 1]
                return body, ("end", index + 2)
            body.append(index)
            index += 1
            if index in leaders:
                return body, ("fall", index)

    def footprint(self):
        indices = set()
        for body, terminator in self.blocks.values():
            indices.update(body)
            if terminator[0] == "leave":
                indices.add(terminator[1])
        return sorted(indices)

    def key(self):
        return (self.entry, tuple((i, self.pairs[i].word) for i in self.footprint()))


def fnv(values):
    value = 14695981039346656037
    for item in values:
        for byte in struct.pack("<Q", item):
            value = ((value ^ byte) * 1099511628211) & (2**64 - 1)
    return value


def pc(index):
    return f"0x{(index * 8) & (CODE_SIZE - 1):04X}u"


def emit_routine(routine, name):
    pairs = routine.pairs
    targets = set()
    for _, terminator in routine.blocks.values():
        if terminator[0] in ("fall", "goto"):
            targets.add(terminator[1])
        elif terminator[0] == "cond":
            targets.update(terminator[1:])
    # Pairs are named by block and position, which lets each one see the pairs
    # issued before it in the block.
    tables = []
    lines = [f"uint32_t {name}(Run &run, uint64_t budgetEnd)", "{", "    Frame p(run, budgetEnd);"]
    # The entry block comes first; every other block is reached by goto.
    for start, (body, terminator) in sorted(routine.blocks.items(), key=lambda item: item[0] != routine.entry):
        table = f"{name}_{start:04x}"
        if body:
            words = ", ".join(f"0x{pairs[i].word:016x}ull" for i in body)
            tables.append(f"constexpr Block<{len(body)}> {table}{{{pc(start)}, {{{words}}}}};")

        def emit_pair(index, next_pc, checked=True):
            call = f"p.pair<{table}, {index - start}>({next_pc})"
            # A GIF callback that rewrote microcode sends the rest to the interpreter.
            return f"    if (!{call}) return p.leaveStale();" if checked else f"    {call};"

        margin = sum(pairs[i].stall_bound() for i in body) + 1
        if start in targets:
            lines.append(f"b_{start:04x}:")
        lines.append(f"    if (!p.fits<{margin}u, {len(body)}u>()) return p.leave({pc(start)});")
        kind = terminator[0]
        tail = [] if kind in ("fall", "leave") else body[-2:]
        for index in body[:len(body) - len(tail)]:
            lines.append(emit_pair(index, pc(index + 1)))
        if kind == "leave":
            lines.append(f"    return p.leave({pc(terminator[1])});")
            continue
        if kind == "fall":
            lines.append(f"    goto b_{terminator[1]:04x};")
            continue

        first, delay = tail
        if kind == "end":
            # Once the delay slot has issued the program is over either way.
            lines.append(emit_pair(first, pc(delay)))
            lines.append(emit_pair(delay, pc(terminator[1]), checked=False))
            lines.append(f"    return p.end({pc(terminator[1])});")
            continue

        # A branch and its delay slot. The E bit on the branch ends the program
        # once the delay slot has issued.
        if kind in ("call", "jump"):
            target = pc(terminator[1]) if terminator[1] is not None else "p.jumpTarget()"
            ending = terminator[2]
        else:
            target = pc(terminator[1])
            ending = kind == "end-branch"
        conditional = kind == "cond" or (kind == "end-branch" and terminator[2])
        after = f"(p.taken() ? {target} : {pc(delay + 1)})" if conditional else target
        lines.append(emit_pair(first, pc(delay)))
        lines.append(emit_pair(delay, after, checked=not ending))
        if ending:
            lines.append(f"    return p.end({after});")
        elif kind in ("call", "jump"):
            lines.append(f"    return p.exit({after});")
        elif kind == "goto":
            lines.append(f"    goto b_{terminator[1]:04x};")
        else:
            lines.append(f"    if (p.taken()) goto b_{terminator[1]:04x};")
            lines.append(f"    goto b_{terminator[2]:04x};")
    lines.append("}")
    return tables + lines


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True,
                        help="registry include; declarations and shards are written beside it")
    parser.add_argument("--shards", type=int, default=4)
    parser.add_argument("profiles", type=Path, nargs="*")
    args = parser.parse_args()
    if args.shards < 1:
        parser.error("--shards must be positive")

    images, entries = load_images(args.profiles, parser)
    routines = {}
    for name, entry in entries:
        pairs = images[name]
        work, seen = [entry], set()
        while work:
            start = work.pop()
            if start in seen or start >= PAIRS:
                continue
            seen.add(start)
            routine = Routine(pairs, start)
            routines.setdefault(routine.key(), routine)
            work.extend(routine.calls)

    output = args.output
    output.parent.mkdir(parents=True, exist_ok=True)
    header = "// Generated from local VU1 microcode; do not distribute game data.\n"
    ordered = sorted(routines.items(), key=lambda item: (item[0][0], fnv(w for _, w in item[0][1])))
    names = [f"r_{fnv(w for _, w in key[1]):016x}_{i}" for i, (key, _) in enumerate(ordered)]

    registry = [header]
    externs = [header, "namespace ps2_vu_program::generated\n{\n"]
    shards = [[header, '#include "ps2_vu1_program.h"\n\n', "namespace ps2_vu_program::generated\n{\n"]
              for _ in range(args.shards)]
    # Compile time follows pair count, so fill the lightest shard first,
    # biggest routines first.
    load = [0] * args.shards
    assigned = {}
    for (key, routine), name in sorted(zip(ordered, names), key=lambda item: (-len(item[0][1].footprint()), item[1])):
        lightest = load.index(min(load))
        assigned[name] = lightest
        load[lightest] += len(routine.footprint())
    for (key, routine), name in zip(ordered, names):
        footprint = routine.footprint()
        externs.append(f"extern const uint16_t {name}_pairs[];\n")
        externs.append(f"extern const uint64_t {name}_words[];\n")
        externs.append(f"uint32_t {name}(Run &, uint64_t);\n")
        registry.append(f"{{{pc(routine.entry)}, {len(footprint)}u, generated::{name}_pairs, "
                        f"generated::{name}_words, &generated::{name}}},\n")
        shard = shards[assigned[name]]
        shard.append(f"extern const uint16_t {name}_pairs[] = {{"
                     + ", ".join(str(i) for i in footprint) + "};\n")
        shard.append(f"extern const uint64_t {name}_words[] = {{"
                     + ", ".join(f"0x{routine.pairs[i].word:016x}ull" for i in footprint) + "};\n")
        shard.append("\n".join(emit_routine(routine, name)) + "\n\n")
    externs.append("}\n")
    for shard in shards:
        shard.append("}\n")

    output.write_text("".join(registry))
    output.with_name(output.stem + "_extern.inc").write_text("".join(externs))
    for index, shard in enumerate(shards):
        output.with_name(f"{output.stem}_{index}.cpp").write_text("".join(shard))
    pair_total = sum(len(r.footprint()) for r in routines.values())
    print(f"compile_vu_programs: {len(routines)} routines, {pair_total} pairs from "
          f"{len(entries)} entries in {len(images)} images")


if __name__ == "__main__":
    main()
