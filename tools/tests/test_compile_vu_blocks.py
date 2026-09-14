"""Check that separate native compilation units preserve registry coverage."""

import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


class CompileVuBlocksTests(unittest.TestCase):
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
            for count in (1, 3, 8):
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


if __name__ == "__main__":
    unittest.main()
