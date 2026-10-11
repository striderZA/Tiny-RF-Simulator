"""Checks for bridge_relink.cmake (Windows only).

Run from anywhere: python agent/mcp/cmake/bridge_relink_check.py

Each scenario uses live copies of ping.exe named rf-sim-mcp.exe as running images, and an
in-process exclusive handle (FileShare.None) for the denied-rename case. Scratch files live
in a temp directory that is removed afterwards. Exit 0 only if every check passes.
"""
import ctypes
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from ctypes import wintypes

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "bridge_relink.cmake")
PING = os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), "System32", "PING.EXE")
PAT = re.compile(r"^rf-sim-mcp\.\d{14}\.[0-9a-f]{6}\.old$")
GENERIC_READ, GENERIC_WRITE = 0x80000000, 0x40000000
OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL = 3, 0x80
INVALID = ctypes.c_void_p(-1).value

failures = []
procs = []
K32 = None


def check(name, ok, detail=""):
    print(("PASS " if ok else "FAIL ") + name + ("" if ok or not detail else " -- " + detail))
    if not ok:
        failures.append(name)


def kernel32():
    k = ctypes.WinDLL("kernel32", use_last_error=True)
    k.CreateFileW.restype = wintypes.HANDLE
    k.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                              wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    k.CloseHandle.argtypes = [wintypes.HANDLE]
    return k


def open_exclusive(path):
    h = K32.CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, None, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, None)
    if h is None or h == INVALID:
        raise OSError(ctypes.get_last_error(), "CreateFileW failed for " + path)
    return h


def cmake(mode, bin_dir, exe=None):
    args = ["cmake", f"-DRF_MODE={mode}", f"-DRF_BIN_DIR={bin_dir}"]
    if exe:
        args.append(f"-DRF_EXE={exe}")
    args += ["-P", SCRIPT]
    r = subprocess.run(args, cwd=HERE, capture_output=True, text=True, timeout=120)
    return r.returncode, r.stdout + r.stderr


def standin(bin_dir):
    os.makedirs(bin_dir, exist_ok=True)
    exe = os.path.join(bin_dir, "rf-sim-mcp.exe")
    try:
        shutil.copy(PING, exe)
    except OSError:
        return None
    p = subprocess.Popen([exe, "-n", "300", "127.0.0.1"], stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW)
    procs.append(p)
    time.sleep(0.5)
    return p


def olds(d):
    return sorted(f for f in os.listdir(d) if f.endswith(".old"))


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def scenario_ab(root):
    d = os.path.join(root, "bin")
    p1 = standin(d)
    check("A stand-in started", p1 is not None)
    rc, out = cmake("aside", d, os.path.join(d, "rf-sim-mcp.exe"))
    check("A aside exits 0 with a running stand-in", rc == 0, out[:200])
    check("A original name absent after aside", not os.path.exists(os.path.join(d, "rf-sim-mcp.exe")))
    o = olds(d)
    check("A one .old copy with the expected pattern", len(o) == 1 and bool(PAT.match(o[0])), str(o))
    check("A running stand-in still alive after rename", p1 is not None and p1.poll() is None)
    standin(d)
    rc, out = cmake("aside", d, os.path.join(d, "rf-sim-mcp.exe"))
    o = olds(d)
    check("B second aside exits 0", rc == 0, out[:200])
    check("B two distinct names, both matching the pattern",
          len(o) == 2 and len(set(o)) == 2 and all(PAT.match(x) for x in o), str(o))


def scenario_c(root):
    d = os.path.join(root, "rfsim relink test", "bin")
    standin(d)
    rc, out = cmake("aside", d, os.path.join(d, "rf-sim-mcp.exe"))
    check("C aside exits 0 with a space in the bin path", rc == 0, out[:200])
    check("C one .old copy in the spaced path", len(olds(d)) == 1, str(olds(d)))


def scenario_d(root):
    d = os.path.join(root, "empty")
    os.makedirs(d)
    rc, out = cmake("aside", d, os.path.join(d, "rf-sim-mcp.exe"))
    check("D aside with no exe exits 0", rc == 0, out[:200])
    check("D aside with no exe creates nothing", os.listdir(d) == [])


def scenario_e(root):
    d = os.path.join(root, "cleanup")
    standin(d)
    cmake("aside", d, os.path.join(d, "rf-sim-mcp.exe"))
    running = olds(d)
    check("E setup: aside left one running copy", len(running) == 1, str(running))
    excl = os.path.join(d, "rf-sim-mcp.9.ffffff.old")
    free = os.path.join(d, "rf-sim-mcp.8.eeeeee.old")
    shutil.copy(PING, excl)
    shutil.copy(PING, free)
    h = open_exclusive(excl)
    try:
        rc, out = cmake("cleanup", d)
    finally:
        K32.CloseHandle(h)
    check("E cleanup exits 0 with locked copies present", rc == 0, out[:200])
    check("E running-image copy kept", bool(running) and os.path.exists(os.path.join(d, running[0])))
    check("E exclusive-handle copy kept", os.path.exists(excl))
    check("E free copy removed", not os.path.exists(free))


def scenario_f(root):
    d = os.path.join(root, "denied")
    os.makedirs(d)
    exe = os.path.join(d, "rf-sim-mcp.exe")
    shutil.copy(PING, exe)
    before = sha(exe)
    h = open_exclusive(exe)
    try:
        rc, out = cmake("aside", d, exe)
    finally:
        K32.CloseHandle(h)
    check("F denied rename fails the build (non-zero exit)", rc != 0, out[:200])
    check("F denied rename message names the lock", "locked and could not be moved aside" in out, out[:200])
    check("F exe bytes unchanged after denied rename", sha(exe) == before)
    check("F no .old created on denied rename", olds(d) == [])


def main():
    if os.name != "nt":
        print("SKIP: bridge_relink is Windows only")
        return 0
    global K32
    K32 = kernel32()
    root = tempfile.mkdtemp(prefix="rfsim-relink-check-")
    try:
        for scenario in (scenario_ab, scenario_c, scenario_d, scenario_e, scenario_f):
            try:
                scenario(root)
            except Exception as error:  # keep later scenarios running; report the cause
                check(f"{scenario.__name__} raised {type(error).__name__}", False, str(error)[:200])
    finally:
        for p in procs:
            try:
                p.kill()
            except OSError:
                pass
        time.sleep(0.5)
        shutil.rmtree(root, ignore_errors=True)
    print(f"SUMMARY: {'ALL PASS' if not failures else str(len(failures)) + ' FAILED'}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
