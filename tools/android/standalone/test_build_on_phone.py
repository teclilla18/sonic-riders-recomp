"""Exercise the installer publication step without an SDK, device, or game data."""

from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest import mock

with mock.patch.dict(sys.modules, {"fcntl": mock.Mock(), "resource": mock.Mock()}):
    import build_on_phone


class BuildPublicationTest(unittest.TestCase):
    def test_finalization_publishes_module_and_report(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            tools, native, output = (root / name for name in ("tools", "native", "prepared"))
            (tools / "config").mkdir(parents=True)
            output.mkdir()
            iso = root / "synthetic.iso"
            iso.write_bytes(b"fixture")

            def fake_tool(command, cwd, **_):
                if "gcrecomp.py" in str(command[1]):
                    generated = Path(command[-1])
                    generated.mkdir()
                    (generated / "code_000.c").touch()
                if Path(command[0]).name in ("libsr_clang.so", "libsr_lld.so"):
                    Path(command[command.index("-o") + 1]).write_bytes(b"synthetic binary")

            metrics = mock.Mock(ru_maxrss=10)
            with mock.patch.object(build_on_phone.subprocess, "run", side_effect=fake_tool), \
                    mock.patch.object(build_on_phone, "blocks", return_value=4096), \
                    mock.patch.object(build_on_phone.resource, "getrusage", return_value=metrics), \
                    redirect_stdout(StringIO()):
                self.assertEqual(build_on_phone.build(tools, native, iso, output), 0)
            module = output / "libmain.so"
            self.assertEqual(module.read_bytes(), b"synthetic binary")
            self.assertIn("Cached module:", (output / "report.txt").read_text(encoding="utf-8"))
            self.assertFalse((output / "stage").exists())
            module.chmod(0o600)  # tempfile cleanup on Windows


if __name__ == "__main__":
    unittest.main()
