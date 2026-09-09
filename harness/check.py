#!/usr/bin/env python3
"""Deterministic static/unit checks for the factory harness (no shell involved).

ci.py runs one argv with shell=False and a 600 s ceiling, so compound commands
(`cmake ... && ctest ...`) and bash scripts are not usable there. This wrapper is
the single executable command; it also works in a fresh candidate worktree where
no build/ directory exists yet.

    python harness/check.py static   # CI-equivalent clang-format-18 check, full file set
    python harness/check.py unit     # configure + build + full ctest

Directory list mirrors scripts/format.sh and the release.yml format job.
"""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

DIRS = [
    "src", "app", "core", "common", "tests", "test_engine",
    "signal_generator", "amplifier", "spectrum_analyzer", "equalizer",
    "node_graph", "splitter", "mixer", "adc", "coax", "pfb_channelizer",
    "iq_plot", "network_analyzer", "ideal_filter", "attenuator", "combiner",
    "power_meter", "touchstone", "help", "layout", "tutorial", "logging",
]


def _run(argv: list[str], timeout: float | None = None) -> subprocess.CompletedProcess:
    print("+ " + " ".join(argv), flush=True)
    return subprocess.run(argv, cwd=ROOT, capture_output=True, text=True,
                          encoding="utf-8", errors="replace", timeout=timeout)


def clang_format() -> str | None:
    """Resolve a clang-format that reports version 18, exactly like format.sh."""
    for candidate in ("clang-format-18", "clang-format"):
        try:
            out = subprocess.run([candidate, "--version"], capture_output=True,
                                 text=True, timeout=30).stdout
        except OSError:
            continue
        if re.search(r"version 18\.", out):
            return candidate
    home = Path.home()
    for pip_bin in (home / ".cache/clang-format-18/clang_format/data/bin/clang-format",
                    home / ".cache/clang-format-18/clang_format/data/bin/clang-format.exe"):
        if pip_bin.is_file():
            return str(pip_bin)
    return None


def sources() -> list[str]:
    files: list[str] = []
    for d in DIRS:
        base = ROOT / d
        if not base.is_dir():
            continue
        for p in sorted(base.rglob("*")):
            if p.is_file() and p.suffix in (".cpp", ".h"):
                files.append(str(p.relative_to(ROOT)).replace("\\", "/"))
    return files


def static() -> int:
    cf = clang_format()
    if cf is None:
        print("check.py: clang-format 18 not found. Run scripts/install-clang-format.sh first.",
              file=sys.stderr)
        return 1
    files = sources()
    if not files:
        print("check.py: no sources found to check", file=sys.stderr)
        return 1
    bad: list[str] = []
    # Windows CreateProcess line limits: chunk the invocation.
    for i in range(0, len(files), 25):
        chunk = files[i:i + 25]
        p = _run([cf, "--dry-run", "--Werror", *chunk])
        if p.returncode != 0:
            bad += chunk
            sys.stdout.write((p.stdout or "") + (p.stderr or ""))
    if bad:
        print(f"check.py: {len(bad)} file(s) need formatting (see diffs above)")
        return 1
    print(f"check.py: {len(files)} file(s) clean.")
    return 0


def unit() -> int:
    cfg = ["cmake", "-B", "build", "-G", "Ninja"]
    if sys.platform == "win32":
        # CONTRIBUTING: MSVC unsupported on Windows; MinGW-w64 g++ is the compiler.
        cfg += ["-DCMAKE_CXX_COMPILER=g++", "-DCMAKE_C_COMPILER=gcc"]
    for step in (cfg, ["cmake", "--build", "build"]):
        p = _run(step)
        tail = (p.stdout or "") + (p.stderr or "")
        if p.returncode != 0:
            sys.stdout.write(tail)
            return p.returncode
    p = _run(["ctest", "--test-dir", "build", "--output-on-failure"])
    out = (p.stdout or "") + (p.stderr or "")
    sys.stdout.write(out)
    return p.returncode


def main() -> int:
    if len(sys.argv) != 2 or sys.argv[1] not in ("static", "unit"):
        print(__doc__)
        return 2
    return static() if sys.argv[1] == "static" else unit()


if __name__ == "__main__":
    raise SystemExit(main())
