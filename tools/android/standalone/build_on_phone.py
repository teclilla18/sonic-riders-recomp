"""Run once per verified ISO and tool bundle, with the APK's own tools."""

from pathlib import Path
import fcntl
import resource
import shutil
import subprocess
import sys
import time


def blocks(paths):
    return sum(p.stat().st_blocks * 512 for path in paths for p in path.rglob("*") if p.is_file())


def build(root, native, iso, output):
    temp = output / "stage"
    shutil.rmtree(temp, ignore_errors=True)
    temp.mkdir()
    shutil.copytree(root / "config", temp / "config")
    (temp / "baserom").mkdir()
    gen = temp / "gen"
    log = output / "build.log"
    build_start = time.monotonic()
    stage = "initializing build"
    try:
        with log.open("wb") as log_stream:
            def run(step, command):
                nonlocal stage
                stage = step
                try:
                    subprocess.run(command, cwd=temp, stdout=log_stream, stderr=log_stream, check=True)
                except subprocess.CalledProcessError as error:
                    raise RuntimeError(f"exit status {error.returncode}") from None

            print("18|Reading game files", flush=True)
            run("extraction", [sys.executable, root / "tools/extract_iso.py", iso, "baserom", "--no-verify-iso"])
            print("23|Analyzing game code", flush=True)
            run("dtk analysis", [native / "libsr_dtk.so", "dol", "split", "config/GXEE8P/config.yml", "analysis"])
            print("29|Translating game code", flush=True)
            run("recompiler", [sys.executable, root / "tools/recomp/gcrecomp.py", "config/GXEE8P", "baserom", gen])
            bin_start = time.monotonic()
            objects = []
            sdk = root / "sdk"
            clang = native / "libsr_clang.so"
            chunks = sorted(gen.glob("code_*.c")) + [gen / "table.c"]
            if len(chunks) < 2:
                raise RuntimeError("the recompiler emitted no game code")
            for index, source in enumerate(chunks, 1):
                print(f"{35 + (index - 1) * 59 // len(chunks)}|Building game code  {index}/{len(chunks)}", flush=True)
                obj = temp / (source.stem + ".o")
                run(f"Clang unit {index}", [
                    clang, "--target=aarch64-linux-android30", "-fintegrated-cc1", "-std=gnu11",
                    "-O2", "-ffp-contract=off", "-fno-strict-aliasing", "-fwrapv", "-fPIC",
                    "-ffunction-sections", "-fdata-sections", "-nostdinc", "-isystem", sdk / "clang",
                    "-isystem", sdk / "include/aarch64-linux-android", "-isystem", sdk / "include",
                    "-I", root / "include", "-I", gen, "-c", source, "-o", obj,
                ])
                objects.append(obj)
            print("95|Finishing installation", flush=True)
            run("LLD link", [
                native / "libsr_lld.so", "-flavor", "gnu", "-shared", "--no-undefined", "--gc-sections",
                "--build-id=sha1", "-z", "max-page-size=16384", "-soname", "libmain.so",
                "-o", temp / "libmain.so", sdk / "link/crtbegin_so.o", root / "runtime.o", *objects,
                native / "libSDL2.so", native / "libc++_shared.so", "-L", sdk / "link",
                "-lvulkan", "-landroid", "-llog", "-ldl", "-lm", "-lc", sdk / "link/libbuiltins.a",
                sdk / "link/crtend_so.o",
            ])
        elapsed = time.monotonic() - build_start
        native_elapsed = time.monotonic() - bin_start
        allocated = blocks({root, output})
        module = temp / "libmain.so"
        size = module.stat().st_size
        module.chmod(0o400)
        report = (
            f"Build complete in {elapsed:.2f} s. Native compile/link: {native_elapsed:.2f} s.\n"
            f"Peak RSS, largest tool: {resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss} KiB; "
            f"build worker: {resource.getrusage(resource.RUSAGE_SELF).ru_maxrss} KiB.\n"
            f"Internal build files before cleanup: {allocated:,} bytes allocated (tools + staging).\n"
            f"Cached module: {size:,} bytes. ISO copy: {iso.stat().st_size:,} bytes."
        )
        (output / "report.txt").write_text(report, encoding="utf-8")
        module.replace(output / "libmain.so")
        print("100|Ready to play", flush=True)
    except (OSError, RuntimeError) as error:
        print(f"FAIL|{stage}: {error}", flush=True)
        return 1
    finally:
        shutil.rmtree(temp, ignore_errors=True)
    return 0


if __name__ == "__main__":
    root, native, iso, output = map(Path, sys.argv[1:])
    output.mkdir(parents=True, exist_ok=True)
    with (output / "build.lock").open("wb") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print("FAIL|A build is already running. Wait a moment, then reopen Riders Rebuilt.", flush=True)
            sys.exit(1)
        sys.exit(build(root, native, iso, output))
