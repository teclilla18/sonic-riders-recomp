"""Linux setup regressions: python -m unittest discover -s tools -p test_setup_linux.py."""
import os
import json
import sys
from contextlib import ExitStack
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import setup_linux as setup


class ZigTargetTests(unittest.TestCase):
    def test_glibc_host_version_for_both_architectures(self):
        with patch.object(setup.os, "confstr", return_value="glibc 2.39", create=True):
            self.assertEqual(setup.zig_target("x86_64"), "x86_64-linux-gnu.2.39")
            self.assertEqual(setup.zig_target("aarch64"), "aarch64-linux-gnu.2.39")

    def test_unknown_libc_is_rejected(self):
        for value in (None, "", "musl 1.2", "glibc 2.39;false", "glibc 2", "glibc 2.39.0"):
            with self.subTest(value=value), patch.object(setup.os, "confstr", return_value=value, create=True), \
                    patch.object(setup, "log"), self.assertRaises(SystemExit):
                setup.zig_target("x86_64")

    def test_unavailable_glibc_query_is_rejected(self):
        for error in (OSError("not available"), ValueError("unknown confstr")):
            with patch.object(setup.os, "confstr", side_effect=error, create=True), \
                    patch.object(setup, "log"), self.assertRaises(SystemExit):
                setup.zig_target("x86_64")

    def test_unsupported_architecture_is_rejected(self):
        with patch.object(setup, "log"), self.assertRaises(SystemExit):
            setup.zig_target("riscv64")

    @unittest.skipUnless(os.name == "posix", "executes POSIX compiler wrappers")
    def test_both_wrappers_preserve_arguments_and_cache(self):
        with tempfile.TemporaryDirectory(prefix="sr-zig-") as tmp:
            root = Path(tmp)
            # Spaces/metacharacters in the compiler path and arguments must remain literal.
            zig = root / "compiler's $folder" / "fake zig"
            zig.parent.mkdir()
            zig.write_text('#!/bin/sh\nprintf "%s\\n" "$ZIG_GLOBAL_CACHE_DIR" "$@"\n')
            zig.chmod(0o755)
            with patch.object(setup.os, "confstr", return_value="glibc 2.39"), patch.object(setup, "log"):
                cc, cxx = setup.write_zig_wrappers(str(zig), tmp, "x86_64")
            for wrapper, language in ((cc, "cc"), (cxx, "c++")):
                result = subprocess.run([wrapper, "source with spaces.cpp", "-DVALUE=$literal"],
                                        capture_output=True, text=True, check=True)
                self.assertEqual(result.stdout.splitlines(), [str(root / "zig-cache"), language,
                                 "-target", "x86_64-linux-gnu.2.39", "source with spaces.cpp", "-DVALUE=$literal"])

    def test_validation_retains_compiler_error(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(setup.subprocess, "run", return_value=
                subprocess.CompletedProcess([], 1, "", "error: LibCStdLibHeaderNotFound")):
            self.assertFalse(setup.compiler_works(["zig-c++"], tmp))
            self.assertIn("LibCStdLibHeaderNotFound", setup.compiler_works.last_error)

    def test_validation_retains_launch_error(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(setup.subprocess, "run", side_effect=OSError("missing")):
            self.assertFalse(setup.compiler_works(["zig-c++"], tmp))
            self.assertIn("missing", setup.compiler_works.last_error)


class SetupHeaderSelectionTests(unittest.TestCase):
    def configure(self, mode, compiler, system_headers=True, static_runtime=True, portable=None):
        """Run the real setup flow through configure, without downloads or game data."""
        class Configured(Exception):
            pass

        with tempfile.TemporaryDirectory(prefix="sr-sdl-") as tmp, ExitStack() as stack:
            root = Path(tmp)
            for name in ("configure.py", "tools/recomp/gcrecomp.py", "disc.iso"):
                p = root / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text("test fixture\n")
            tools = str(root / "tools-cache")
            specs = {n: {"dir": n} for n in ("dtk", "ninja", "zig", "sdl2_headers", "imgui")}
            tc = {"linux": {"dtk": {"x86_64": specs["dtk"]}, "ninja": {"x86_64": specs["ninja"]},
                            "zig": {"x86_64": specs["zig"]}, "sdl2_headers": specs["sdl2_headers"]},
                  "imgui": specs["imgui"]}
            (root / "setup").mkdir()
            (root / "setup/toolchain.json").write_text(json.dumps(tc))
            mocks = {}
            for name, value in (("ROOT", str(root)), ("LOG", str(root / "setup.log"))):
                stack.enter_context(patch.object(setup, name, value))
            for name, result in (("log", None), ("check_iso", 13), ("convert_image", str(root / "disc.iso")),
                                 ("system_compiler", compiler), ("system_sdl_headers", system_headers),
                                 ("static_cxx_runtime_works", static_runtime), ("compiler_works", True),
                                 ("write_zig_wrappers", ["zig-cc", "zig-c++"])):
                mocks[name] = stack.enter_context(patch.object(setup, name, return_value=result))
            ensure = stack.enter_context(patch.object(setup, "ensure_tool", side_effect=lambda spec, _:
                portable if portable and spec == specs["sdl2_headers"] else str(root / spec["dir"])))
            stack.enter_context(patch.object(setup.platform, "system", return_value="Linux"))
            stack.enter_context(patch.object(setup.platform, "machine", return_value="x86_64"))
            stack.enter_context(patch.object(sys, "argv", ["setup_linux.py", "--iso", str(root / "disc.iso"),
                                "--yes", "--tools", tools, "--compiler", mode, "--desktop-shortcut", "no"]))
            commands = []

            def run(what, cmd, **kwargs):
                if what == "configure":
                    commands.append(cmd)
                    raise Configured()

            stack.enter_context(patch.object(setup, "run", side_effect=run))
            with self.assertRaises(Configured):
                setup.main()
            return commands[0], mocks, ensure

    def test_zig_ignores_installed_debian_multiarch_sdl_headers(self):
        args, mocks, ensure = self.configure("zig", ("clang", "clang++"))
        self.assertIn("--sdl", args)
        self.assertEqual(Path(args[args.index("--sdl") + 1]).name, "sdl2_headers")
        mocks["system_sdl_headers"].assert_not_called()
        mocks["system_compiler"].assert_not_called()
        self.assertEqual(args[args.index("--cc") + 1], "zig-cc")

    def test_auto_fallback_uses_portable_headers_when_system_sdl_exists(self):
        args, mocks, _ = self.configure("auto", None)
        self.assertIn("--sdl", args)
        mocks["system_sdl_headers"].assert_not_called()
        mocks["compiler_works"].assert_called_once_with(["zig-c++"])

    def test_system_compilers_keep_system_headers_in_both_modes(self):
        for mode in ("auto", "system"):
            for pair in (("clang", "clang++"), ("gcc", "g++")):
                with self.subTest(mode=mode, compiler=pair):
                    args, mocks, _ = self.configure(mode, pair)
                    self.assertNotIn("--sdl", args)
                    self.assertEqual(args[args.index("--cc") + 1], pair[0])
                    mocks["write_zig_wrappers"].assert_not_called()

    def test_system_compilers_download_headers_only_when_missing(self):
        for mode in ("auto", "system"):
            with self.subTest(mode=mode):
                args, _, _ = self.configure(mode, ("gcc", "g++"), system_headers=False)
                self.assertIn("--sdl", args)

    def test_system_mode_without_compiler_does_not_fall_back_to_zig(self):
        with self.assertRaises(SystemExit):
            self.configure("system", None)

    def test_system_shared_cxx_runtime_fallback_is_preserved(self):
        args, mocks, _ = self.configure("system", ("gcc", "g++"), static_runtime=False)
        self.assertIn("--shared-cxx-runtime", args)
        self.assertNotIn("--sdl", args)

    @unittest.skipUnless(os.name == "posix" and os.environ.get("SR_TEST_ZIG") and
                         os.environ.get("SR_TEST_SDL_HEADERS"), "optional real Zig + pinned SDL header probe")
    def test_real_zig_with_debian_multiarch_sdl_layout(self):
        config = Path("/usr/include/SDL2/SDL_config.h")
        if not config.is_file() or "_real_SDL_config.h" not in config.read_text():
            self.skipTest("needs Debian/Ubuntu multiarch SDL headers")
        portable = os.environ["SR_TEST_SDL_HEADERS"]
        args, _, _ = self.configure("zig", None, portable=portable)
        selected = Path(args[args.index("--sdl") + 1])
        include = next(p for p in (selected / "include/SDL2", selected / "include") if (p / "SDL.h").is_file())
        with tempfile.TemporaryDirectory(prefix="sr-sdl-probe-") as tmp:
            src = Path(tmp) / "sdl.c"
            src.write_text("#include <SDL.h>\nint main(void){return SDL_Init(0);}\n")
            command = [os.environ["SR_TEST_ZIG"], "cc", "-target", setup.zig_target("x86_64"),
                       "-c", str(src), "-o", str(Path(tmp) / "sdl.o")]
            bad = subprocess.run(command + ["-I/usr/include/SDL2"], capture_output=True, text=True, timeout=120)
            self.assertNotEqual(bad.returncode, 0)
            self.assertIn("_real_SDL_config.h", bad.stderr)
            good = subprocess.run(command + ["-I" + str(include)], capture_output=True, text=True, timeout=120)
            self.assertEqual(good.returncode, 0, good.stderr)


if __name__ == "__main__":
    unittest.main()
