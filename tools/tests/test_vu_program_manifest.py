"""Check that a manifest rebuilds the routines its recordings compile to."""

import struct
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


LOWER_NOP = 0x8000033C
UPPER_NOP = 0x000002FF
E_BIT = 1 << 30


def program(destination):
    """ADD.xyzw into a given VF register, then the end of the program."""
    add = (0xF << 21) | (3 << 16) | (2 << 11) | (destination << 6) | 0x28
    return struct.pack("<6I", LOWER_NOP, add, LOWER_NOP, UPPER_NOP | E_BIT, LOWER_NOP, UPPER_NOP)


def upload(code, first):
    """A DMA ret tag whose VIF codes, NOP and MPG, load code at a pair."""
    code += bytes(-len(code) % 16)
    mpg = (0x4A << 24) | (len(code) // 8 << 16) | first
    return struct.pack("<4I", (6 << 28) | len(code) // 16, 0, 0, mpg) + code


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

    def test_uploads_add_programs_no_recording_holds(self):
        # Two programs the game loads at pair 16; only the first one ran while recording.
        recorded, unrecorded = program(1), program(4)
        recording = self.root / "one-program"
        recording.mkdir()
        image = bytearray(16384)
        image[16 * 8:16 * 8 + len(recorded)] = recorded
        (recording / "vu1-a0.code").write_bytes(image)
        (recording / "entries.txt").write_text("a0 80\n")
        game = self.disc / "UPLOADS.BIN"
        game.write_bytes(bytes(48) + upload(recorded, 16) + bytes(range(64)) + upload(unrecorded, 16))

        def routines(*options):
            manifest = self.root / "uploads.txt"
            run(MANIFEST, "make", "--output", manifest, "--game", game, *options, recording)
            expanded = self.root / f"expanded{len(options)}"
            run(MANIFEST, "expand", "--manifest", manifest, "--game-root", self.disc, "--output", expanded)
            output = self.root / f"compiled{len(options)}"
            compiled(expanded, output)
            return [line for line in (output / "vu_programs.inc").read_text().splitlines()
                    if line.startswith("{0x0080u")]

        self.assertEqual(len(routines()), 1)
        self.assertEqual(len(routines("--uploads")), 2)


if __name__ == "__main__":
    unittest.main()
