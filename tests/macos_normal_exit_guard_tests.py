import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tests" / "macos_normal_exit_tests.sh"


class MacosNormalExitGuardTests(unittest.TestCase):
    def _fake_app(self, root: Path) -> tuple[Path, Path]:
        app = root / "osgSol Earth.app"
        binary = app / "Contents" / "MacOS" / "osgSol_Earth"
        binary.parent.mkdir(parents=True)
        marker = root / "binary-ran"
        binary.write_text(f"#!/bin/sh\ntouch '{marker}'\nexit 0\n")
        binary.chmod(0o755)
        return app, marker

    def test_refuses_to_execute_an_app_inside_desktop(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            (home / "Library" / "Logs" / "DiagnosticReports").mkdir(parents=True)
            app, marker = self._fake_app(home / "Desktop")
            env = os.environ.copy()
            env["HOME"] = str(home)

            result = subprocess.run(
                ["bash", str(SCRIPT), str(app)],
                env=env,
                capture_output=True,
                text=True,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(marker.exists())
            self.assertIn("Desktop", result.stderr)

    def test_staging_exit_test_can_request_a_longer_frame_soak(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp) / "home"
            (home / "Library" / "Logs" / "DiagnosticReports").mkdir(parents=True)
            (home / "Desktop").mkdir()
            app, marker = self._fake_app(Path(tmp) / "staging")
            binary = app / "Contents" / "MacOS" / "osgSol_Earth"
            binary.write_text(
                f"#!/bin/sh\nprintf '%s' \"$EARTH_AUTOQUIT_FRAMES\" > '{marker}'\nexit 0\n"
            )
            env = os.environ.copy()
            env["HOME"] = str(home)
            env["OSGSOL_EXIT_TEST_FRAMES"] = "120"

            result = subprocess.run(
                ["bash", str(SCRIPT), str(app)],
                env=env,
                capture_output=True,
                text=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(marker.read_text(), "120")


if __name__ == "__main__":
    unittest.main()
