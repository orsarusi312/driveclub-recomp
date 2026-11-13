"""Independent XML fixtures: no game binaries, no modification to prepared files."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


class MemoryProfileTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.exe = os.environ["MEMORY_PROFILE_TEST"]
        cls.profiles = []
        for node in ET.parse(ROOT / "patches/DriveClub.xml").iter("Metadata"):
            name = node.get("Name", "")
            budgets = {"Memory for up to 1440p": 8056,
                       "Memory for up to 4K": 10056, "Memory for up to 8K": 17056}
            if name in budgets:
                writes = [(int(line.get("Address"), 16) - 0x400000,
                           bytes.fromhex(line.get("Value"))) for line in node.find("PatchList")]
                cls.profiles.append((name, budgets[name], writes))
        assert len(cls.profiles) == 3

    def run_image(self, writes, budget):
        # A sparse disk fixture avoids manufacturing any original guest code.
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "image.bin"
            with path.open("wb") as f:
                for offset, data in writes:
                    f.seek(offset)
                    f.write(data)
            return subprocess.run([self.exe, str(path), str(budget)],
                                  capture_output=True, text=True, check=False)

    def test_known_profiles_reject_retail_budget(self):
        for name, budget, writes in self.profiles:
            with self.subTest(name=name):
                r = self.run_image(writes, 5056)
                self.assertEqual(r.returncode, 2, r.stderr)
                self.assertIn(name, r.stdout)
                self.assertIn(f"BB_DMEM_MB={budget}", r.stdout)

    def test_launcher_budgets_and_larger_pass(self):
        for name, budget, writes in self.profiles:
            for pool in (budget, 20480):
                with self.subTest(name=name, pool=pool):
                    r = self.run_image(writes, pool)
                    self.assertEqual(r.returncode, 0, r.stderr)
                    self.assertIn(name, r.stdout)
                    self.assertNotIn("STOP", r.stdout)
            # Third XML write is MOV RAX,imm64 for VideoPrivate. Check the
            # proven size bound independently of the recommended launcher tier.
            video_mib = int.from_bytes(writes[2][1][2:10], "little") // (1024 * 1024)
            self.assertEqual(self.run_image(writes, video_mib - 1).returncode, 2)
            self.assertEqual(self.run_image(writes, video_mib).returncode, 0)
            self.assertEqual(self.run_image(writes, budget - 1).returncode, 0)

    def test_every_write_is_required(self):
        for name, _, writes in self.profiles:
            for i in range(len(writes)):
                with self.subTest(name=name, missing=i):
                    r = self.run_image(writes[:i] + writes[i+1:], 5056)
                    self.assertEqual(r.returncode, 0, r.stderr)
                    self.assertEqual(r.stdout, "")  # unknown: no invented tier

    def test_corrupted_layouts_are_not_classified(self):
        for name, _, writes in self.profiles:
            for i, (offset, data) in enumerate(writes):
                with self.subTest(name=name, corrupt=i):
                    changed = list(writes)
                    changed[i] = (offset, data[:-1] + bytes([data[-1] ^ 1]))
                    r = self.run_image(changed, 5056)
                    self.assertEqual(r.returncode, 0, r.stderr)
                    self.assertEqual(r.stdout, "")

    def test_truncated_and_unpatched_images(self):
        for writes in ([], [(0, b"\0" * 8)], [(0x1257FFF, b"\0")]):
            r = self.run_image(writes, 5056)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(r.stdout, "")

    def test_final_write_wins(self):
        # Old unused stubs must not make the runtime pick an old tier.
        writes = self.profiles[0][2] + self.profiles[1][2]
        r = self.run_image(writes, 6144)
        self.assertEqual(r.returncode, 2, r.stderr)
        self.assertIn("Memory for up to 4K", r.stdout)
        self.assertNotIn("Memory for up to 1440p", r.stdout)


if __name__ == "__main__":
    unittest.main()
