#!/usr/bin/env python3
"""Write synthetic VU1 programs as a program profile, to test compiled routines.

usage: vu_programs.py DIRECTORY

DIRECTORY gets the layout a run with PS2_VU_PROGRAM_PROFILE records: 16 KiB
vu1-<hash>.code images and an entries.txt. Build the runtime with
PS2X_VU_PROGRAM_PROFILES=DIRECTORY to compile the programs, then run the VU
tests with PS2_VU_PROGRAM_FIXTURES=DIRECTORY: ps2_vu1_program_tests.cpp runs
every entry both compiled and interpreted and compares the two.

A few programs are written out to reach particular paths: blocks split for
length, an E bit on a branch, a program the compiler hands back part-way. The
rest come from a seeded generator that mixes the instructions the compiler
accepts inside counted loops, forward branches, calls and register jumps.
They rely on what the test sets up: VI registers start at zero, data memory
holds floats, and qword 0x3F0 holds a GIF packet for XGKICK.
"""

import random
import struct
import sys
from pathlib import Path

CODE_SIZE = 16384
PAIRS = CODE_SIZE // 8
LOWER_NOP = 0x8000033C
UPPER_NOP = 0x000002FF
E_BIT = 1 << 30
I_BIT = 1 << 31
XYZ = 0b1110

# Registers with fixed jobs, so that loops end and addresses stay in range.
# Generated integer code writes only VI2-VI7, and FCAND and friends VI1.
SCRATCH = (2, 3, 4, 5, 6, 7)
COUNTERS = (8, 9)  # loop counters, by nesting depth
POINTER = 10       # LQI/SQI/LQD/SQD base, reset before each loop
KICK = 11          # XGKICK address
BASE = 12          # LQ/SQ/ILW/ISW base
TARGET = 13        # JALR target
FIXED = 14         # set once by a hand-written program
LINK = 15

KICK_QWORD = 0x3F0  # the test's GIF packet
BASE_QWORD = 0x40
POINTER_QWORD = 0x100

# Upper opcodes 0x00-0x2F: ADD, SUB, MADD, MSUB, MAX, MINI and MUL in their
# broadcast, Q and I forms, then the three-register ones. OPMSUB is left to
# outer_product(), which pairs it with OPMULA.
VECTOR_OPS = [op for op in range(0x30) if op != 0x2E]
# Special upper opcodes that write ACC: the A forms of the above.
ACC_OPS = [*range(0x10), *range(0x18, 0x1D), 0x1E, *range(0x20, 0x2B), 0x2C, 0x2D]
# EFU operations on a vector, with the fields each one reads, and on one field.
EFU_VECTOR = ((0x70, XYZ), (0x71, XYZ), (0x72, XYZ), (0x73, XYZ), (0x74, 0b1100), (0x75, 0b1010), (0x76, 0b1111))
EFU_SCALAR = (0x78, 0x79, 0x7A, 0x7C, 0x7D)

FLOATS = (0.0, -0.0, 1.0, -1.0, 0.5, -2.5, 3.0, 1000.0, 1e-30, 1e30, 3.4e38, -3.4e38)
# Bit patterns without a PS2 float meaning: infinities, NaN, a denormal.
RAW_FLOATS = (0x7F800000, 0xFF800000, 0x7FC00000, 0x00000001, 0x7FFFFFFF)


def upper(op, dest, ft, fs, fd):
    return (dest << 21) | (ft << 16) | (fs << 11) | (fd << 6) | op


def upper_special(op, dest, ft, fs):
    return (dest << 21) | (ft << 16) | (fs << 11) | ((op & 0x7C) << 4) | (op & 3) | 0x3C


def lower_special(op, is_=0, it=0, dest=0):
    return (0x40 << 25) | (dest << 21) | (it << 16) | (is_ << 11) | ((op & 0x7C) << 4) | (op & 3) | 0x3C


def lower_direct(funct, is_, it, id_):
    return (0x40 << 25) | (it << 16) | (is_ << 11) | (id_ << 6) | funct


def immediate15(op, it, is_, value):
    """IADDIU and ISUBIU, whose 15-bit immediate is split around the dest field."""
    return (op << 25) | (((value >> 11) & 15) << 21) | (it << 16) | (is_ << 11) | (value & 0x7FF)


def memory(op, dest, t, s, offset):
    return (op << 25) | (dest << 21) | (t << 16) | (s << 11) | (offset & 0x7FF)


def branch(op, it=0, is_=0, offset=0):
    return (op << 25) | (it << 16) | (is_ << 11) | (offset & 0x7FF)


def flag_immediate(op, it, value):
    return (op << 25) | (((value >> 11) & 1) << 21) | (it << 16) | (value & 0x7FF)


def flag_register(op, it, is_=0):
    return (op << 25) | (it << 16) | (is_ << 11)


class Program:
    """Pairs laid out from an entry. Branch offsets are patched in once the
    target is known."""

    def __init__(self, entry):
        self.entry = entry
        self.pairs = []

    @property
    def next(self):
        return self.entry + len(self.pairs)

    def add(self, lower=LOWER_NOP, upper_word=UPPER_NOP):
        self.pairs.append([lower, upper_word])
        return self.next - 1

    def patch_branch(self, index, target):
        pair = self.pairs[index - self.entry]
        pair[0] = (pair[0] & ~0x7FF) | ((target - index - 1) & 0x7FF)

    def patch_immediate(self, index, value):
        pair = self.pairs[index - self.entry]
        pair[0] = immediate15(pair[0] >> 25, (pair[0] >> 16) & 15, (pair[0] >> 11) & 15, value)


class Subroutine:
    def __init__(self):
        self.calls = []  # BAL pairs to point at it
        self.loads = []  # IADDIU pairs that load its address for JALR


class Generator:
    def __init__(self, seed):
        self.random = random.Random(seed)

    def vf(self):
        return self.random.randrange(32)

    def written_vf(self):
        return self.random.randrange(1, 32)

    def vi(self):
        return self.random.choice((0, 1) + SCRATCH)

    def dest(self):
        return self.random.choice((15, 15, 15, 14, 12, 8, 4, 2, 1, 3, 7, 11, 13))

    def lane(self):
        return self.random.randrange(4)

    def float_bits(self):
        if self.random.random() < 0.15:
            return self.random.choice(RAW_FLOATS)
        value = self.random.choice(FLOATS) if self.random.random() < 0.4 else self.random.uniform(-200.0, 200.0)
        return struct.unpack("<I", struct.pack("<f", value))[0]

    def upper(self):
        """A random upper instruction and the VF register it writes, if any."""
        kind = self.random.choices(("nop", "vector", "acc", "convert", "abs", "clip"), (12, 50, 16, 8, 3, 6))[0]
        dest = self.dest()
        if kind == "vector":
            fd = self.written_vf()
            return upper(self.random.choice(VECTOR_OPS), dest, self.vf(), self.vf(), fd), fd
        if kind == "acc":
            return upper_special(self.random.choice(ACC_OPS), dest, self.vf(), self.vf()), None
        if kind in ("convert", "abs"):
            ft = self.written_vf()
            op = self.random.randrange(0x10, 0x18) if kind == "convert" else 0x1D
            return upper_special(op, dest, ft, self.vf()), ft
        if kind == "clip":
            return upper_special(0x1F, XYZ, self.vf(), self.vf()), None
        return UPPER_NOP, None

    def lower(self, avoid=None):
        """A random lower instruction that neither branches nor writes VF avoid."""
        while True:
            word, writes = self.any_lower()
            if writes is None or writes != avoid:
                return word

    def any_lower(self):
        r = self.random
        kind = r.choices(
            ("nop", "integer", "load", "store", "stream", "integer memory", "move", "transfer",
             "fdiv", "waitq", "efu", "waitp", "mfp", "status", "mac", "clip", "xtop", "kick"),
            (14, 14, 7, 5, 5, 4, 5, 4, 4, 1, 4, 1, 2, 3, 3, 3, 1, 1))[0]
        dest = self.dest()
        if kind == "integer":
            target = r.choice(SCRATCH)
            form = r.randrange(4)
            if form == 0:
                return lower_direct(r.choice((0x30, 0x31, 0x34, 0x35)), self.vi(), self.vi(), target), None
            if form == 1:
                return lower_direct(0x32, self.vi(), target, r.randrange(32)), None  # IADDI
            return immediate15(r.choice((0x08, 0x09)), target, self.vi(), r.randrange(2048)), None
        if kind == "load":
            ft = self.written_vf()
            return memory(0x00, dest, ft, BASE, r.randrange(64)), ft
        if kind == "store":
            return memory(0x01, dest, BASE, self.vf(), r.randrange(64)), None
        if kind == "stream":
            op = r.choice((0x34, 0x35, 0x36, 0x37))  # LQI, SQI, LQD, SQD
            if op in (0x34, 0x36):
                ft = self.written_vf()
                return lower_special(op, POINTER, ft, dest), ft
            return lower_special(op, self.vf(), POINTER, dest), None
        if kind == "integer memory":
            one = 8 >> self.lane()
            form = r.randrange(4)
            if form == 0:
                return memory(0x04, one, r.choice(SCRATCH), BASE, r.randrange(64)), None  # ILW
            if form == 1:
                return memory(0x05, dest, self.vi(), BASE, r.randrange(64)), None     # ISW
            if form == 2:
                return lower_special(0x3E, BASE, r.choice(SCRATCH), one), None       # ILWR
            return lower_special(0x3F, BASE, self.vi(), dest), None                  # ISWR
        if kind == "move":
            ft = self.written_vf()
            return lower_special(r.choice((0x30, 0x31)), self.vf(), ft, dest), ft  # MOVE, MR32
        if kind == "transfer":
            if r.random() < 0.5:
                return lower_special(0x3C, self.vf(), r.choice(SCRATCH), self.lane()), None  # MTIR
            ft = self.written_vf()
            return lower_special(0x3D, self.vi(), ft, dest), ft  # MFIR
        if kind == "fdiv":
            op = r.choice((0x38, 0x39, 0x3A))  # DIV, SQRT, RSQRT
            fields = (self.lane() << 2) | (self.lane() if op != 0x39 else 0)
            return lower_special(op, self.vf() if op != 0x39 else 0, self.vf(), fields), None
        if kind == "waitq":
            return lower_special(0x3B), None
        if kind == "efu":
            if r.random() < 0.5:
                op, fields = r.choice(EFU_VECTOR)
                return lower_special(op, self.vf(), 0, fields), None
            return lower_special(r.choice(EFU_SCALAR), self.vf(), 0, self.lane()), None
        if kind == "waitp":
            return lower_special(0x7B), None
        if kind == "mfp":
            ft = self.written_vf()
            return lower_special(0x64, 0, ft, dest), ft
        if kind == "status":
            op = r.choice((0x14, 0x15, 0x16, 0x17))  # FSEQ, FSSET, FSAND, FSOR
            return flag_immediate(op, r.choice(SCRATCH) if op != 0x15 else 0, r.randrange(4096)), None
        if kind == "mac":
            return flag_register(r.choice((0x18, 0x1A, 0x1B)), r.choice(SCRATCH), self.vi()), None
        if kind == "clip":
            if r.random() < 0.25:
                return flag_register(0x1C, r.choice(SCRATCH)), None  # FCGET
            return (r.choice((0x10, 0x11, 0x12, 0x13)) << 25) | r.randrange(1 << 24), None
        if kind == "xtop":
            return lower_special(r.choice((0x68, 0x69)), 0, r.choice(SCRATCH)), None
        if kind == "kick":
            return lower_special(0x6C, KICK), None
        return LOWER_NOP, None

    def pair(self, program):
        word, writes = self.upper()
        if self.random.random() < 0.06:
            return program.add(self.float_bits(), word | I_BIT)
        return program.add(self.lower(writes), word)

    def outer_product(self, program):
        fs, ft, fd = self.vf(), self.vf(), self.written_vf()
        program.add(self.lower(), upper_special(0x2E, XYZ, ft, fs))
        program.add(self.lower(fd), upper(0x2E, XYZ, fs, ft, fd))

    def straight(self, program, count):
        for _ in range(count):
            if self.random.random() < 0.04:
                self.outer_product(program)
            else:
                self.pair(program)

    def body(self, program, items, depth, subroutines=()):
        for _ in range(items):
            roll = self.random.random()
            if roll < 0.08 and depth < len(COUNTERS):
                self.loop(program, depth, subroutines)
            elif roll < 0.16:
                self.skip(program, depth, subroutines)
            elif roll < 0.20 and subroutines:
                self.call(program, self.random.choice(subroutines))
            else:
                self.straight(program, self.random.randint(1, 4))

    def loop(self, program, depth, subroutines):
        counter = COUNTERS[depth]
        program.add(immediate15(0x08, counter, 0, self.random.randint(1, 8 if depth == 0 else 4)), self.upper()[0])
        program.add(immediate15(0x08, POINTER, 0, POINTER_QWORD), self.upper()[0])
        head = program.next
        self.body(program, self.random.randint(3, 12), depth + 1, subroutines)
        program.add(immediate15(0x09, counter, counter, 1), self.upper()[0])
        # Sometimes the branch reads the counter straight after the decrement.
        if self.random.random() < 0.5:
            self.pair(program)
        at = program.add(branch(0x29, 0, counter), self.upper()[0])  # IBNE
        program.patch_branch(at, head)
        self.pair(program)

    def skip(self, program, depth, subroutines):
        op = self.random.choice((0x20, 0x28, 0x29, 0x2C, 0x2D, 0x2E, 0x2F))
        at = program.add(branch(op, self.vi(), self.vi()), self.upper()[0])
        self.pair(program)
        self.body(program, self.random.randint(1, 4), depth, subroutines)
        program.patch_branch(at, program.next)

    def call(self, program, subroutine):
        if self.random.random() < 0.5:
            subroutine.calls.append(program.add(branch(0x21, LINK), self.upper()[0]))  # BAL
        else:
            subroutine.loads.append(program.add(immediate15(0x08, TARGET, 0, 0), self.upper()[0]))
            self.straight(program, self.random.randint(0, 2))
            program.add(branch(0x25, LINK, TARGET), self.upper()[0])  # JALR
        self.pair(program)

    def end(self, program):
        word, writes = self.upper()
        program.add(self.lower(writes), word | E_BIT)
        self.pair(program)

    def start(self, entry):
        """A program that has set up the registers generated code addresses through."""
        program = Program(entry)
        program.add(immediate15(0x08, KICK, 0, KICK_QWORD), self.upper()[0])
        program.add(immediate15(0x08, BASE, 0, BASE_QWORD), self.upper()[0])
        program.add(immediate15(0x08, POINTER, 0, POINTER_QWORD), self.upper()[0])
        return program

    def program(self, entry):
        program = self.start(entry)
        subroutines = [Subroutine() for _ in range(self.random.randint(0, 2))]
        self.body(program, self.random.randint(12, 40), 0, subroutines)
        self.end(program)
        for subroutine in subroutines:
            start = program.next
            self.body(program, self.random.randint(2, 6), len(COUNTERS))
            program.add(branch(0x24, 0, LINK), self.upper()[0])  # JR
            self.pair(program)
            for at in subroutine.calls:
                program.patch_branch(at, start)
            for at in subroutine.loads:
                program.patch_immediate(at, start)
        return program


def long_straight(generator):
    """Past the compiler's block size, so the run is cut into several blocks."""
    program = generator.start(0)
    generator.straight(program, 300)
    generator.end(program)
    return program


def branch_with_e_bit(generator, taken):
    """The E bit on a conditional branch ends the program after the delay slot."""
    program = generator.start(40)
    generator.straight(program, 8)
    program.add(immediate15(0x08, FIXED, 0, 1 if taken else 0))
    generator.straight(program, 3)
    at = program.add(branch(0x29, 0, FIXED), UPPER_NOP | E_BIT)  # IBNE
    generator.pair(program)
    generator.straight(program, 4)
    program.patch_branch(at, program.next)
    generator.end(program)
    return program


def handed_back(generator):
    """RINIT is not compiled, so the program returns to the interpreter there."""
    program = generator.start(100)
    program.add(immediate15(0x08, 8, 0, 5))
    head = program.next
    generator.straight(program, 12)
    program.add(lower_special(0x42, 1, 0, 0), UPPER_NOP)  # RINIT R, VF1x
    generator.straight(program, 12)
    program.add(immediate15(0x09, 8, 8, 1))
    at = program.add(branch(0x29, 0, 8))
    program.patch_branch(at, head)
    generator.pair(program)
    generator.end(program)
    return program


def image(programs):
    words = [(LOWER_NOP, UPPER_NOP)] * PAIRS
    for program in programs:
        if program.entry + len(program.pairs) > PAIRS:
            raise ValueError(f"program at {program.entry * 8:#x} runs past the end of VU1 code memory")
        for index, pair in enumerate(program.pairs, program.entry):
            words[index] = tuple(pair)
    return b"".join(struct.pack("<II", lower, upper_word) for lower, upper_word in words)


def fnv(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & (2**64 - 1)
    return value


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    images = [[long_straight(Generator(1))],
              [branch_with_e_bit(Generator(2), True), handed_back(Generator(3))],
              [branch_with_e_bit(Generator(4), False)]]
    # Two generated programs per image, at entries the layout leaves free.
    for seed in range(100, 124, 2):
        first = Generator(seed).program(0)
        second = Generator(seed + 1).program(first.next + 16)
        images.append([first, second])
    lines = []
    for programs in images:
        code = image(programs)
        name = f"{fnv(code):016x}"
        (output / f"vu1-{name}.code").write_bytes(code)
        lines += [f"{name} {program.entry * 8:x}" for program in programs]
    (output / "entries.txt").write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
