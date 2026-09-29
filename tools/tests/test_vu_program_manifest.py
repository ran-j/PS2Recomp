"""Check that a manifest rebuilds the routines its recordings compile to."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "tools" / "vu_program_manifest.py"
COMPILER = ROOT / "tools" / "compile_vu_programs.py"
FIXTURES = ROOT / "ps2xTest" / "data" / "vu_programs.py"


def run(*arguments):
    return subprocess.run([sys.executable, *map(str, arguments)], check=True, capture_output=True, text=True)


def compiled(profile, output):
    run(COMPILER, "--output", output / "vu_programs.inc", "--shards", "2", profile)
    return [path.read_text() for path in sorted(output.iterdir())]


class VuProgramManifestTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        self.recordings = self.root / "recordings"
        run(FIXTURES, self.recordings)
        # A stand-in for the game's files: the recorded code amid other data.
        self.disc = self.root / "disc"
        self.disc.mkdir()
        images = sorted(self.recordings.glob("vu1-*.code"))
        self.game = self.disc / "GAME.ELF"
        self.game.write_bytes(b"".join(bytes(range(97)) * 3 + path.read_bytes() for path in images))
        self.manifest = self.root / "manifest.txt"
        run(MANIFEST, "make", "--output", self.manifest, "--game", self.game, self.recordings)

    def tearDown(self):
        self.directory.cleanup()

    def test_expanded_manifest_compiles_like_the_recordings(self):
        expanded = self.root / "expanded"
        run(MANIFEST, "expand", "--manifest", self.manifest, "--game-root", self.disc, "--output", expanded)
        self.assertTrue((expanded / "entries.txt").read_text().strip())
        self.assertEqual(compiled(self.recordings, self.root / "from-recordings"),
                         compiled(expanded, self.root / "from-manifest"))

    def test_images_whose_code_changed_are_skipped(self):
        data = bytearray(self.game.read_bytes())
        for index in range(0, len(data), 8):
            data[index] ^= 0xFF
        self.game.write_bytes(bytes(data))
        expanded = self.root / "expanded"
        result = run(MANIFEST, "expand", "--manifest", self.manifest, "--game-root", self.disc,
                     "--output", expanded)
        self.assertIn("0 images expanded", result.stdout)
        self.assertEqual((expanded / "entries.txt").read_text().strip(), "")


if __name__ == "__main__":
    unittest.main()
