"""Check how compile_vu_programs.py cuts VU1 microcode into routines and blocks."""

import importlib.util
import re
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools" / "compile_vu_programs.py"
FIXTURES = ROOT / "ps2xTest" / "data" / "vu_programs.py"

spec = importlib.util.spec_from_file_location("vu_programs", FIXTURES)
vu = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vu)


def profile(directory, pairs, entries):
    """Write one code image holding pairs from index 0, with the given entry indices."""
    words = [(vu.LOWER_NOP, vu.UPPER_NOP)] * vu.PAIRS
    for index, pair in enumerate(pairs):
        words[index] = pair
    (directory / "vu1-test.code").write_bytes(b"".join(struct.pack("<II", *pair) for pair in words))
    (directory / "entries.txt").write_text("".join(f"test {index * 8:x}\n" for index in entries))


def compile_profile(directory, shards=1, options=()):
    output = directory / "out" / "vu_programs.inc"
    result = subprocess.run([sys.executable, str(SCRIPT), "--output", str(output), "--shards", str(shards),
                             *options, str(directory)], capture_output=True, text=True)
    return result, output


def registered_pcs(output):
    return {int(pc, 16) for pc in re.findall(r"^\{0x([0-9A-F]+)u,", output.read_text(), re.M)}


def routines(output):
    text = "".join(path.read_text() for path in sorted(output.parent.glob("vu_programs_*.cpp")))
    return text, [int(pairs) for pairs in re.findall(r"p\.fits<\d+u, (\d+)u>", text)]


class CompileVuProgramsTests(unittest.TestCase):
    def test_fixture_programs_all_get_routines_and_the_output_is_stable(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run([sys.executable, str(FIXTURES), str(root / "fixtures")], check=True)
            outputs = []
            for name in ("a", "b"):
                output = root / name / "vu_programs.inc"
                subprocess.run([sys.executable, str(SCRIPT), "--output", str(output), "--shards", "3",
                                str(root / "fixtures")], check=True, capture_output=True, text=True)
                outputs.append([path.read_text() for path in sorted(output.parent.iterdir())])
            self.assertEqual(outputs[0], outputs[1])
            entries = {int(line.split()[1], 16)
                       for line in (root / "fixtures" / "entries.txt").read_text().splitlines()}
            self.assertLessEqual(entries, registered_pcs(root / "a" / "vu_programs.inc"))

    def test_long_straight_runs_are_cut_into_blocks_that_fit_the_flag_queue(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pairs = [(vu.immediate15(0x08, 2, 2, 1), vu.UPPER_NOP)] * 300
            pairs += [(vu.LOWER_NOP, vu.UPPER_NOP | vu.E_BIT), (vu.LOWER_NOP, vu.UPPER_NOP)]
            profile(root, pairs, [0])
            result, output = compile_profile(root)
            self.assertEqual(result.returncode, 0, result.stderr)
            text, blocks = routines(output)
            self.assertEqual(sum(blocks), len(pairs))
            self.assertEqual(max(blocks), 127)
            self.assertIn("goto b_007f;", text)

    def test_calls_and_constant_register_jumps_start_routines_of_their_own(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            nop = (vu.LOWER_NOP, vu.UPPER_NOP)
            pairs = [nop] * 24
            pairs[0] = (vu.branch(0x21, vu.LINK, 0, 10 - 1), vu.UPPER_NOP)  # BAL to 10
            pairs[2] = (vu.immediate15(0x08, vu.TARGET, 0, 20), vu.UPPER_NOP)
            pairs[3] = (vu.branch(0x25, vu.LINK, vu.TARGET), vu.UPPER_NOP)  # JALR to 20
            pairs[5] = (vu.LOWER_NOP, vu.UPPER_NOP | vu.E_BIT)
            pairs[10] = (vu.branch(0x24, 0, vu.LINK), vu.UPPER_NOP)  # JR
            pairs[20] = (vu.branch(0x24, 0, vu.LINK), vu.UPPER_NOP)
            profile(root, pairs, [0])
            result, output = compile_profile(root)
            self.assertEqual(result.returncode, 0, result.stderr)
            # The entry, both callees, and the two places the calls return to.
            self.assertEqual(registered_pcs(output), {0, 10 * 8, 2 * 8, 20 * 8, 5 * 8})
            text, _ = routines(output)
            self.assertEqual(text.count("return p.exit(p.jumpTarget());"), 2)

    def test_an_unsupported_pair_hands_the_program_back_where_it_stands(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            nop = (vu.LOWER_NOP, vu.UPPER_NOP)
            rinit = (vu.lower_special(0x42, 1), vu.UPPER_NOP)
            pairs = [nop, nop, nop, rinit, nop, (vu.LOWER_NOP, vu.UPPER_NOP | vu.E_BIT), nop]
            profile(root, pairs, [0])
            result, output = compile_profile(root)
            self.assertEqual(result.returncode, 0, result.stderr)
            text, blocks = routines(output)
            self.assertEqual(blocks, [3])
            self.assertIn("return p.leave(0x0018u);", text)

    def test_every_assigned_efu_opcode_compiles_and_the_unassigned_ones_hand_back(self):
        nop = (vu.LOWER_NOP, vu.UPPER_NOP)
        end = [(vu.LOWER_NOP, vu.UPPER_NOP | vu.E_BIT), nop]
        assigned = [op for op in range(0x70, 0x80) if op not in (0x77, 0x7F)]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pairs = [(vu.lower_special(op, 1), vu.UPPER_NOP) for op in assigned] + end
            profile(root, pairs, [0])
            result, output = compile_profile(root)
            self.assertEqual(result.returncode, 0, result.stderr)
            _, blocks = routines(output)
            self.assertEqual(blocks, [len(pairs)])
        for op in (0x77, 0x7F):
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                profile(root, [nop, nop, (vu.lower_special(op, 1), vu.UPPER_NOP)] + end, [0])
                result, output = compile_profile(root)
                self.assertEqual(result.returncode, 0, result.stderr)
                text, blocks = routines(output)
                self.assertEqual(blocks, [2])
                self.assertIn("return p.leave(0x0010u);", text)

    def test_entries_must_be_pair_aligned_and_inside_code_memory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            profile(root, [(vu.LOWER_NOP, vu.UPPER_NOP | vu.E_BIT)], [0])
            for line in ("test 4\n", "test 4000\n", "test\n"):
                (root / "entries.txt").write_text(line)
                result, _ = compile_profile(root)
                self.assertNotEqual(result.returncode, 0)
                self.assertRegex(result.stderr, "invalid entry|expected '<hash> <pc>'")

    def test_long_routines_are_compiled_in_chunks_that_continue_each_other(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pairs = [(vu.immediate15(0x08, 2, 2, 1), vu.UPPER_NOP)] * 600
            pairs += [(vu.LOWER_NOP, vu.UPPER_NOP | vu.E_BIT), (vu.LOWER_NOP, vu.UPPER_NOP)]
            profile(root, pairs, [0])
            result, output = compile_profile(root)
            self.assertEqual(result.returncode, 0, result.stderr)
            # Still one routine, entered and checked as a whole.
            self.assertEqual(registered_pcs(output), {0})
            text, blocks = routines(output)
            self.assertEqual(sum(blocks), len(pairs))
            chunks = re.split(r"^static PS2_VU_CHUNK ", text, flags=re.M)[1:]
            self.assertEqual(len(chunks), 3)
            for chunk in chunks:
                self.assertLessEqual(sum(int(n) for n in re.findall(r"p\.fits<\d+u, (\d+)u>", chunk)), 256)
            self.assertIn("return p.exit(0x10100u);", text)
            self.assertIn("return p.exit(0x10200u);", text)

    def test_a_loop_that_fits_in_a_chunk_is_not_cut(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            work = (vu.immediate15(0x08, 2, 2, 1), vu.UPPER_NOP)
            nop = (vu.LOWER_NOP, vu.UPPER_NOP)
            pairs = [work] * 119 + [(vu.immediate15(0x08, 8, 0, 3), vu.UPPER_NOP)]
            head = len(pairs)
            # 150 pairs, which the compiler cuts into blocks of 127 and 23.
            pairs += [work] * 147 + [(vu.immediate15(0x09, 8, 8, 1), vu.UPPER_NOP)]
            pairs += [(vu.branch(0x29, 0, 8, head - (len(pairs) + 1)), vu.UPPER_NOP), nop]
            pairs += [(vu.LOWER_NOP, vu.UPPER_NOP | vu.E_BIT), nop]
            profile(root, pairs, [0])
            result, output = compile_profile(root)
            self.assertEqual(result.returncode, 0, result.stderr)
            text, blocks = routines(output)
            self.assertEqual(sorted(blocks), [2, 23, 120, 127])
            # Filling the first chunk would have cut the loop after its first block.
            self.assertEqual(text.count("static PS2_VU_CHUNK"), 2)
            self.assertIn(f"goto b_{head + 127:04x};", text)
            self.assertIn(f"if (p.taken()) goto b_{head:04x};", text)

    def test_routines_under_the_limit_compile_as_before(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pairs = [(vu.immediate15(0x08, 2, 2, 1), vu.UPPER_NOP)] * 600
            pairs += [(vu.LOWER_NOP, vu.UPPER_NOP | vu.E_BIT), (vu.LOWER_NOP, vu.UPPER_NOP)]
            profile(root, pairs, [0])
            result, output = compile_profile(root, options=("--max-routine-pairs", "1000"))
            self.assertEqual(result.returncode, 0, result.stderr)
            text, _ = routines(output)
            self.assertNotIn("PS2_VU_CHUNK", text)
            for limit in ("127", "x"):
                result, _ = compile_profile(root, options=("--max-routine-pairs", limit))
                self.assertNotEqual(result.returncode, 0)

    def test_a_routine_of_a_thousand_chained_blocks_compiles(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            nop = (vu.LOWER_NOP, vu.UPPER_NOP)
            # Each IBNE continues at the next block either way: all of code
            # memory as one chain of 1,024 blocks.
            pairs = [(vu.branch(0x29, offset=1), vu.UPPER_NOP), nop] * 1023
            pairs += [(vu.LOWER_NOP, vu.UPPER_NOP | vu.E_BIT), nop]
            profile(root, pairs, [0])
            result, output = compile_profile(root)
            self.assertEqual(result.returncode, 0, result.stderr)
            text, blocks = routines(output)
            self.assertEqual(len(blocks), 1024)
            self.assertEqual(text.count("static PS2_VU_CHUNK"), 8)


if __name__ == "__main__":
    unittest.main()
