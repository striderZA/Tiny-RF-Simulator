"""Reach the software under test. Three shapes cover almost everything.

    http     a server. Started on a dynamic port, polled until it answers.
    cli      a command. Invoked with args; stdout, stderr and exit code asserted.
    library  no process at all. The E2E imports it and calls it.

Chosen by `driver` in `harness.config.json`. Every one of them prints APP_STARTED,
because that marker means "the thing under test is reachable" -- a different claim
for a server than for a library, and equally load-bearing for both.

WHY THIS IS SPLIT OUT. The obvious first version of this file is HTTP-only: urllib,
a health path, a GET and a POST. That makes a scaffold silently useless for a CLI, a
library, a batch job or a desktop app -- the majority of software -- while the
surrounding documentation claims the plumbing is universal. The process management
genuinely IS universal. The way you talk to the thing is not.

THE REACHABILITY CONSTRAINT, and it is an architecture decision. A rendered window,
a game loop, a canvas, a native UI is NONE of these three. So the rules have to live
behind a headless, scriptable surface an E2E can drive: simulation apart from
rendering, domain apart from view. On a greenfield build, say this before any code
exists -- it is nearly free then and it is a rewrite afterwards.

The universal parts, kept in one place because getting them wrong is subtle:

  * a DYNAMIC port, so two laps cannot collide
  * WAIT for an answer rather than sleeping; a sleep is a race you chose to lose
  * FAIL HARD if it never comes up -- never degrade to "not testable", which is how
    a crashed app becomes a green run
  * TEAR DOWN on every path including failure, or a leaked process holds the port
    and poisons the next lap
"""

from __future__ import annotations

import json
import os
import shlex
import shutil
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class AppDidNotStart(RuntimeError):
    pass


def _free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return int(s.getsockname()[1])


def unquote(token: str) -> str:
    """Take one layer of matching surrounding quotes off a token.

    Commands are split with `posix=False`, which is right on Windows because it
    leaves backslashes in paths alone -- and wrong in that it leaves the QUOTES
    attached to every token it split.

    That was applied to argv[0] only, and the consequence on the rest was severe:
    `python -c "import app"` arrived as the three tokens
    `python`, `-c`, `"import app"`, so Python evaluated the STRING LITERAL
    `"import app"` and exited 0. Measured: `python -c "import
    definitely_not_a_module"` also exits 0. The library driver's import check --
    the entire evidence behind `APP_STARTED driver=library` -- could not fail.
    The Node equivalent this shipped later had exactly the same hole.

    Stripping every token is what posix mode would have done, without giving up
    the backslashes.
    """
    if len(token) > 1 and token[0] == token[-1] and token[0] in "\"'":
        return token[1:-1]
    return token


def _argv(cmd: str) -> list[str]:
    """Split, then resolve argv[0] through PATHEXT.

    Without the resolve, a Windows `.cmd` shim (npm, npx, yarn, pnpm) fails as "the
    system cannot find the file specified" -- which reads as "not installed" for a
    tool that is on PATH and works in any terminal.
    """
    parts = [unquote(t) for t in shlex.split(cmd, posix=False)]
    if parts:
        parts[0] = shutil.which(parts[0]) or parts[0]
    return parts


# --------------------------------------------------------------------------- http
class HttpApp:
    """A server on a dynamic port, polled until healthy."""

    def __init__(self, cfg: dict, env: dict | None = None) -> None:
        self.cfg = cfg.get("http", {})
        self.port = _free_port()
        self.base = f"http://127.0.0.1:{self.port}"
        self.proc: subprocess.Popen | None = None
        self.env_overrides = env or {}

    def __enter__(self) -> "HttpApp":
        cmd = (self.cfg.get("start") or "").replace("{port}", str(self.port))
        if not cmd:
            raise AppDidNotStart("driver=http but http.start is empty in harness.config.json")

        merged = {**(self.cfg.get("env") or {}), **self.env_overrides}
        env = None
        if merged:
            env = {**os.environ, **{k: str(v).replace("{port}", str(self.port))
                                    for k, v in merged.items()}}
        self.proc = subprocess.Popen(
            _argv(cmd), cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding="utf-8", errors="replace", env=env,
        )
        self._await_health()
        print(f"APP_STARTED port={self.port}", flush=True)
        return self

    def __exit__(self, *exc) -> None:
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        self._free_the_port()

    def _free_the_port(self) -> None:
        """Kill whatever is still listening, even if this object did not start it.

        Killing `self.proc` frees the port only when the process on it is the one
        this object spawned. A journey may restart the app, and that replacement is
        untracked: the original dies, the agent's copy keeps the port, `__exit__`
        terminates a corpse, and the next lap gets `[Errno 10048] address already in
        use` from a factory that thinks it tore everything down.

        Measured after a morning of gate runs: four orphaned interpreters holding
        four ports, on the exact class of leak the teardown comment already warned
        about.

        Best effort by design. This runs during teardown, on every path including a
        failure, and a port that cannot be freed must not turn a reported failure
        into a traceback about cleanup.
        """
        if not self.port:
            return
        try:
            if os.name == "nt":
                out = subprocess.run(
                    ["netstat", "-ano"], capture_output=True, text=True,
                    encoding="utf-8", errors="replace", timeout=30,
                ).stdout
                pids = {
                    line.split()[-1]
                    for line in out.splitlines()
                    if f":{self.port} " in line and "LISTENING" in line
                }
                for pid in pids:
                    if pid.isdigit() and pid != "0" and (
                        not self.proc or str(self.proc.pid) != pid
                    ):
                        subprocess.run(["taskkill", "/PID", pid, "/F"],
                                       capture_output=True, timeout=30)
            else:
                out = subprocess.run(
                    ["lsof", "-ti", f"tcp:{self.port}"], capture_output=True,
                    text=True, timeout=30,
                ).stdout
                for pid in out.split():
                    if pid.isdigit() and (not self.proc or str(self.proc.pid) != pid):
                        subprocess.run(["kill", "-9", pid], capture_output=True, timeout=30)
        except (OSError, subprocess.SubprocessError):
            # Named, not swallowed: a port left held is a real cost on the next lap,
            # and silence here is how it becomes a mystery instead of a line in a log.
            print(f"PORT_NOT_FREED {self.port} - something may still be listening",
                  flush=True)

    def _await_health(self) -> None:
        path = self.cfg.get("health_path", "/health")
        want = self.cfg.get("health_contains", "")
        deadline = time.time() + int(self.cfg.get("boot_timeout_s", 45))
        last = "never answered"
        while time.time() < deadline:
            if self.proc and self.proc.poll() is not None:
                out = (self.proc.stdout.read() if self.proc.stdout else "") or ""
                raise AppDidNotStart(
                    f"the app exited with {self.proc.returncode} before answering:\n{out[-2500:]}"
                )
            try:
                status, body, _ = self.get(path)
                if status == 200 and (not want or want in body):
                    return
                # NAME WHICH CONDITION FAILED. The first version reported "never
                # became healthy" and then printed a 200 with a healthy-looking
                # body, because the failing half was the CONTAINS check: the
                # configured string had no space after the colon and the app's JSON
                # did. That reads as a dead app, so the next hour goes on the
                # server, and the actual fix is one character in a config file.
                if status != 200:
                    last = f"answered {status}, wanted 200. body={body[:160]!r}"
                else:
                    last = (
                        f"answered 200 but the body does not contain "
                        f"health_contains={want!r}. Got {body[:160]!r}. The app is "
                        f"UP; it is the string in harness.config.json that does not "
                        f"match"
                    )
            except (urllib.error.URLError, OSError) as e:
                last = f"not accepting connections yet ({e})"
            time.sleep(0.2)
        raise AppDidNotStart(
            f"{path} never became healthy in time. {last}. This is a FAILURE, not "
            f"'not testable'."
        )

    def get(self, path: str, follow: bool = False, headers: dict | None = None):
        """(status, body, headers).

        `follow=False` so a redirect stays VISIBLE -- following them by default is
        how a test of a redirect stops testing it.
        """
        req = urllib.request.Request(self.base + path, headers=headers or {})

        class _NoRedirect(urllib.request.HTTPRedirectHandler):
            def redirect_request(self, *a, **kw):
                return None

        opener = (
            urllib.request.build_opener()
            if follow
            else urllib.request.build_opener(_NoRedirect)
        )
        try:
            with opener.open(req, timeout=30) as r:
                return r.status, r.read().decode("utf-8", "replace"), dict(r.headers)
        except urllib.error.HTTPError as e:
            return e.code, e.read().decode("utf-8", "replace"), dict(e.headers)

    def request(self, method: str, path: str, body: str = "", headers: dict | None = None):
        h = {"Content-Type": "application/json"}
        h.update(headers or {})
        req = urllib.request.Request(
            self.base + path,
            data=body.encode("utf-8") if body else None,
            headers=h,
            method=method.upper(),
        )
        try:
            with urllib.request.urlopen(req, timeout=30) as r:
                return r.status, r.read().decode("utf-8", "replace"), dict(r.headers)
        except urllib.error.HTTPError as e:
            return e.code, e.read().decode("utf-8", "replace"), dict(e.headers)

    def post(self, path: str, body: str, headers: dict | None = None):
        return self.request("POST", path, body, headers)

    def post_json(self, path: str, payload: dict):
        status, body, _ = self.post(path, json.dumps(payload))
        try:
            return status, json.loads(body)
        except ValueError:
            return status, {"_unparseable": body[:400]}

    def get_json(self, path: str):
        status, body, _ = self.get(path)
        try:
            return status, json.loads(body)
        except ValueError:
            return status, {"_unparseable": body[:400]}

    def delete(self, path: str):
        return self.request("DELETE", path)


# --------------------------------------------------------------------------- cli
class CliApp:
    """A command-line program. `app.run("--flag value")` -> (rc, stdout, stderr).

    The equivalent of a health check is a smoke invocation: if the binary cannot
    even print its own version, nothing below is worth running.
    """

    def __init__(self, cfg: dict) -> None:
        self.cfg = cfg.get("cli", {})

    def __enter__(self) -> "CliApp":
        if not self.cfg.get("invoke"):
            raise AppDidNotStart("driver=cli but cli.invoke is empty in harness.config.json")
        rc, out, err = self.run(self.cfg.get("smoke_args", "--help"))
        want = self.cfg.get("smoke_contains", "")
        if rc not in (0, 1) or (want and want not in (out + err)):
            raise AppDidNotStart(f"the smoke invocation failed: rc={rc}\n{(out + err)[-1500:]}")
        print("APP_STARTED driver=cli", flush=True)
        return self

    def __exit__(self, *exc) -> None:
        return None

    def run(self, args: str = "", stdin: str = "", timeout: int = 120):
        cmd = self.cfg.get("invoke", "").replace("{args}", args)
        p = subprocess.run(
            _argv(cmd), cwd=ROOT, input=stdin or None, capture_output=True, text=True,
            encoding="utf-8", errors="replace", timeout=timeout,
        )
        return p.returncode, p.stdout or "", p.stderr or ""


# --------------------------------------------------------------------------- library
class LibraryApp:
    """No process. The E2E imports the thing and calls it.

    APP_STARTED here means it imports at all -- which is the same claim as a server
    answering: whatever follows is being asserted against something that loaded.
    """

    def __init__(self, cfg: dict) -> None:
        self.cfg = cfg.get("library", {})

    def __enter__(self) -> "LibraryApp":
        check = self.cfg.get("import_check", "")
        if check:
            p = subprocess.run(
                _argv(check), cwd=ROOT, capture_output=True, text=True,
                encoding="utf-8", errors="replace", timeout=120,
            )
            if p.returncode != 0:
                raise AppDidNotStart(
                    f"the library does not import:\n{(p.stdout + p.stderr)[-1500:]}"
                )
        sys.path.insert(0, str(ROOT))
        print("APP_STARTED driver=library", flush=True)
        return self

    def __exit__(self, *exc) -> None:
        return None


DRIVERS = {"http": HttpApp, "cli": CliApp, "library": LibraryApp}


def make_driver(cfg: dict, env: dict | None = None):
    """Start the thing under test.

    `env` overrides the config's own env block for THIS driver only. It exists for
    one job, and it is a job every holdout eventually needs: PROVING A RESTART.

    "Does the data survive a process boundary?" cannot be answered inside one
    process, and it is the question a dropped persistence write hides behind --
    everything works perfectly until a restart, and no single-process test can see the
    difference because the in-memory answer is correct either way. Answering it means
    starting a SECOND process against the SAME state, which means pinning whatever
    names that state:

        DB = ".factory/runs/holdout.db"
        with make_driver(CONFIG, env={"APP_DB": DB}) as app:
            ...                                    # record something
            with make_driver(CONFIG, env={"APP_DB": DB}) as restarted:
                ...                                # is it still there?

    Without this the second driver binds a new port, the config's `{port}`
    substitution hands it a different state file, and the scenario silently proves
    nothing at all -- it starts an empty app and finds it empty.
    """
    name = (cfg.get("driver") or "http").strip().lower()
    if name not in DRIVERS:
        raise AppDidNotStart(
            f"unknown driver {name!r} in harness.config.json - expected one of "
            f"{', '.join(sorted(DRIVERS))}"
        )
    if name == "http":
        return HttpApp(cfg, env)
    return DRIVERS[name](cfg)
