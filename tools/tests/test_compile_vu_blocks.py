"""Check that separate native compilation units preserve registry coverage."""

import re
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


class CompileVuBlocksTests(unittest.TestCase):
    def test_straight_backedges_do_not_require_a_particular_counter(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            script = Path(__file__).resolve().parents[1] / "compile_vu_blocks.py"
            words = [0x000002ff8000033c] * 9
            words[5] = (0x000002ff << 32) | (8 << 25) | (2 << 16) | (2 << 11) | 5
            words[7] = (0x000002ff << 32) | (0x29 << 25) | (2 << 16) | (3 << 11) | (-8 & 0x7ff)
            words[8] = (0x000002ff << 32) | (1 << 25) | (15 << 21) | (2 << 16) | (4 << 11) | (-7 & 0x7ff)
            for mode in ("valid", "absent", "duplicate", "different-source"):
                variant = words.copy()
                if mode == "absent":
                    variant[5] = 0x000002ff8000033c
                elif mode == "duplicate":
                    variant[4] = variant[5]
                elif mode == "different-source":
                    variant[5] ^= 1 << 11
                data = bytearray(16384)
                struct.pack_into("<9Q", data, 0, *variant)
                image, output = root / "code.bin", root / "out.inc"
                image.write_bytes(data)
                result = subprocess.run([sys.executable, str(script), "--output", str(output),
                                         "--loop-images", str(image)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                entries = re.findall(r"&runCompiledBlock<Unit::VU1, ([^>]+)>", output.read_text())
                self.assertEqual(sum(entry.count("ull") == 9 for entry in entries), 1)

    def test_counted_loop_discovery_matches_explicit_profiles_for_both_units(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            script = Path(__file__).resolve().parents[1] / "compile_vu_blocks.py"
            for pairs in (4, 7, 16, 17, 31, 32):
                words = [0x000002ff8000033c] * pairs  # Authored NOP pairs.
                words[-2] = (0x000002ff << 32) | (0x29 << 25) | (2 << 16) | (3 << 11) | ((1 - pairs) & 0x7ff)
                words[-1] = (0x000002ff << 32) | 0x8000037d | (8 << 21) | (2 << 16) | (4 << 11)
                for unit, size in ((0, 4096), (1, 16384)):
                    image, profile = root / "code.bin", root / "loops.profile"
                    data = bytearray(size)
                    struct.pack_into("<" + "Q" * pairs, data, 24, *words)
                    image.write_bytes(data)
                    profile.write_text("VU-BLOCKS 3\n" + str(unit) + " " + " ".join(f"{word:016x}" for word in words) + "\n")
                    outputs = []
                    for name, inputs in (("image", ["--loop-images", str(image)]), ("profile", [str(profile)])):
                        output = root / f"{name}.inc"
                        subprocess.run([sys.executable, str(script), "--output", str(output), *inputs],
                                       check=True, capture_output=True, text=True)
                        outputs.append(output.read_text())
                    self.assertEqual(outputs[0], outputs[1])

    def test_straight_loop_discovery_rejects_invalid_edges_and_delay_branches(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            script = Path(__file__).resolve().parents[1] / "compile_vu_blocks.py"
            branch = (0x000002ff << 32) | (0x29 << 25) | (2 << 16) | (3 << 11) | (-6 & 0x7ff)
            delay = (0x000002ff << 32) | 0x8000037d | (8 << 21) | (2 << 16) | (4 << 11)
            for bad_branch, bad_delay in (
                (branch | (1 << 63), delay),  # LOI cannot encode a branch.
                ((branch & ~0x7ff) | (1 & 0x7ff), delay),  # Forward edge.
                ((branch & ~0x7ff) | (-16 & 0x7ff), delay),  # Head before this image.
                (branch, (0x000002ff << 32) | (0x28 << 25) | 1),
            ):
                data = bytearray(16384)
                struct.pack_into("<QQ", data, 40, bad_branch, bad_delay)
                image = root / "code.bin"
                image.write_bytes(data)
                result = subprocess.run([sys.executable, str(script), "--output", str(root / "out.inc"),
                                         "--loop-images", str(image)], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("no nonempty microcode blocks", result.stderr)

    def test_straight_loop_discovery_handles_loi_and_equal_operands(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            script = Path(__file__).resolve().parents[1] / "compile_vu_blocks.py"
            words = [0x81e0106240000000, 0x81e010de40400000,
                     0x000002ff500007fd, 0x000002ff8000033c]
            data = bytearray(16384)
            struct.pack_into("<4Q", data, 0, *words)
            image, output = root / "code.bin", root / "out.inc"
            image.write_bytes(data)
            subprocess.run([sys.executable, str(script), "--output", str(output),
                            "--loop-images", str(image)], check=True, capture_output=True, text=True)
            entries = re.findall(r"&runCompiledBlock<Unit::VU1, ([^>]+)>", output.read_text())
            self.assertEqual(sum(entry.count("ull") == 4 for entry in entries), 1)

    def test_shards_preserve_registry_and_instantiate_every_entry_once(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first, second = root / "first.profile", root / "second.profile"
            first.write_text("VU-BLOCKS 1\n1 1 2 3 4\n0 1 1 1 1\n")
            second.write_text("VU-BLOCKS 1\n1 1 2 3 4\n1 5 6 7 8\n")
            script = Path(__file__).resolve().parents[1] / "compile_vu_blocks.py"

            def generate(name, shards, inputs):
                output = root / name / "blocks.inc"
                subprocess.run([sys.executable, str(script), "--output", str(output),
                                "--shards", str(shards), "--", *map(str, inputs)],
                               check=True, capture_output=True, text=True)
                return output

            original = generate("original", 0, [first, second])
            registry = original.read_text()
            expected = {(unit, words) for unit, words in re.findall(
                r"&runCompiledBlock<Unit::VU([01]), ([^>]+)>", registry)}
            self.assertEqual(len(expected), 12)  # Three blocks and nine unit-specific pairs.
            for count in (1, 3, 8, 32):
                output = generate(f"shards-{count}", count, [second, first, second])
                self.assertEqual(output.read_text(), registry)
                sources = sorted(output.parent.glob("blocks_*.cpp"))
                self.assertEqual(len(sources), count)
                instances = []
                for source in sources:
                    text = source.read_text()
                    self.assertIn('#include "ps2_vu1_exec.inl"', text)
                    instances += re.findall(
                        r"template bool VU1Interpreter::runCompiledBlock<VU1Interpreter::Unit::VU([01]), ([^>]+)>", text)
                self.assertEqual(len(instances), len(expected))
                self.assertEqual(set(instances), expected)
                externs = re.findall(
                    r"extern template bool VU1Interpreter::runCompiledBlock<VU1Interpreter::Unit::VU([01]), ([^>]+)>",
                    (output.parent / "blocks_extern.inc").read_text())
                self.assertEqual(len(externs), len(expected))
                self.assertEqual(set(externs), expected)
                repeated = generate(f"repeat-{count}", count, [first, second])
                for source in output.parent.iterdir():
                    self.assertEqual(source.read_bytes(), (repeated.parent / source.name).read_bytes())

    def test_extended_profiles_preserve_lengths_and_unit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            profile, output = root / "regions.profile", root / "blocks.inc"
            records = [f"{unit} " + " ".join(f"{word:x}" for word in range(1, length + 1))
                       for unit in (0, 1) for length in (4, 8, 12, 16)]
            profile.write_text("VU-BLOCKS 2\n" + "\n".join(records) + "\n")
            script = Path(__file__).resolve().parents[1] / "compile_vu_blocks.py"
            subprocess.run([sys.executable, str(script), "--output", str(output),
                            "--shards", "32", "--", str(profile)],
                           check=True, capture_output=True, text=True)
            entries = re.findall(r"&runCompiledBlock<Unit::VU([01]), ([^>]+)>, (\d+)u", output.read_text())
            self.assertEqual(len(entries), 40)  # Eight regions plus sixteen pairs per unit.
            for unit in ("0", "1"):
                self.assertEqual(sorted(int(size) for entry_unit, _, size in entries if entry_unit == unit),
                                 [8] * 16 + [32, 64, 96, 128])
            for _, words, size in entries:
                self.assertEqual(len(words.split(", ")) * 8, int(size))

    def test_profile_length_and_word_bounds_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            profile, output = root / "invalid.profile", root / "blocks.inc"
            script = Path(__file__).resolve().parents[1] / "compile_vu_blocks.py"
            for header, words in [(1, "1 2 3 4 5 6 7 8"), (2, "1 2 3 4 5"),
                                  (2, "1 2 3 10000000000000000"), (2, "1 2 3 -1"),
                                  (3, "1 2 3"), (3, " ".join(["1"] * 33)),
                                  (3, "1 2 3 10000000000000000"), (3, "1 2 3 -1")]:
                profile.write_text(f"VU-BLOCKS {header}\n1 {words}\n")
                result = subprocess.run([sys.executable, str(script), "--output", str(output),
                                         "--", str(profile)], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("invalid", result.stderr)


if __name__ == "__main__":
    unittest.main()
