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


def compile_profile(directory, shards=1):
    output = directory / "out" / "vu_programs.inc"
    result = subprocess.run([sys.executable, str(SCRIPT), "--output", str(output), "--shards", str(shards),
                             str(directory)], capture_output=True, text=True)
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

    def test_entries_must_be_pair_aligned_and_inside_code_memory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            profile(root, [(vu.LOWER_NOP, vu.UPPER_NOP | vu.E_BIT)], [0])
            for line in ("test 4\n", "test 4000\n", "test\n"):
                (root / "entries.txt").write_text(line)
                result, _ = compile_profile(root)
                self.assertNotEqual(result.returncode, 0)
                self.assertRegex(result.stderr, "invalid entry|expected '<hash> <pc>'")


if __name__ == "__main__":
    unittest.main()
