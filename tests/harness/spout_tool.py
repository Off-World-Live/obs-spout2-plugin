"""Synthetic Spout sender/receiver used as the oracle outside OBS.

Prefers the native ``tests/spout-tool/build/Release/spout-tool.exe`` (SpoutDX: deterministic
adapter selection, DXGI formats, real frame counter) and falls back to the OpenGL based
``harness/spoutgl_tool.py`` (same CLI / JSON contract) when it is not built.

Both run as subprocesses so a crashing sender never takes the test session down, and so the
sender can be pinned to another GPU with ``harness.gpu_prefs`` (per-executable setting).
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional

import numpy as np

from harness import paths, pattern


def backend() -> str:
    return "spout-tool" if paths.spout_tool_exe() else "spoutgl"


def _base_command(sub: str) -> List[str]:
    exe = paths.spout_tool_exe()
    if exe:
        return [str(exe), sub]
    return [str(paths.REAL_PYTHON), "-m", "harness.spoutgl_tool", sub]


def _env() -> Dict[str, str]:
    env = dict(os.environ)
    extra = [str(paths.TESTS_DIR), str(paths.VENV_SITE_PACKAGES)]
    env["PYTHONPATH"] = os.pathsep.join(extra + ([env["PYTHONPATH"]] if env.get("PYTHONPATH") else []))
    env["PYTHONUNBUFFERED"] = "1"
    return env


def _run(args: List[str], timeout: float) -> Dict[str, Any]:
    proc = subprocess.run(
        args, capture_output=True, text=True, timeout=timeout, env=_env(), cwd=str(paths.TESTS_DIR)
    )
    last_json: Optional[Dict[str, Any]] = None
    for line in proc.stdout.splitlines():
        line = line.strip()
        if line.startswith("{"):
            try:
                last_json = json.loads(line)
            except json.JSONDecodeError:
                pass
    if last_json is None:
        raise RuntimeError(f"{args[0]} {args[1]} produced no JSON (rc={proc.returncode})\n{proc.stdout}\n{proc.stderr}")
    last_json.setdefault("returncode", proc.returncode)
    last_json.setdefault("stderr", proc.stderr[-2000:])
    return last_json


@dataclass
class Sender:
    """A running sender subprocess. Use ``start_sender`` or the ``senders`` fixture."""

    name: str
    width: int
    height: int
    fps: float
    seed: int
    alpha: str
    adapter: Optional[str]
    proc: subprocess.Popen = field(repr=False)
    started: Dict[str, Any] = field(default_factory=dict)
    result: Optional[Dict[str, Any]] = None

    @property
    def size(self) -> tuple[int, int]:
        return self.width, self.height

    @property
    def alive(self) -> bool:
        return self.proc.poll() is None

    def stop(self, timeout: float = 5.0) -> Dict[str, Any]:
        if self.result is not None:
            return self.result
        out = ""
        if self.alive:
            try:
                out, _ = self.proc.communicate("q\n", timeout=timeout)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                out, _ = self.proc.communicate()
        else:
            out, _ = self.proc.communicate()
        result: Dict[str, Any] = {"event": "stopped", "frames_sent": None, "returncode": self.proc.returncode}
        for line in (out or "").splitlines():
            if line.strip().startswith("{"):
                try:
                    obj = json.loads(line)
                    if obj.get("event") == "stopped":
                        result.update(obj)
                except json.JSONDecodeError:
                    pass
        self.result = result
        return result


def start_sender(
    name: str,
    size=(640, 360),
    fps: float = 30.0,
    seed: int = 0,
    alpha: str = "opaque",
    adapter: Optional[str] = None,
    fmt: Optional[str] = None,
    duration: float = 0.0,
    ready_timeout: float = 15.0,
    invert: bool = False,
) -> Sender:
    """Start a sender and block until its first frame is out (the JSON 'started' line)."""
    w, h = size
    args = _base_command("send") + ["--name", name, "--size", f"{w}x{h}", "--fps", str(fps), "--seed", str(seed), "--alpha", alpha]
    if adapter:
        args += ["--adapter", str(adapter)]
    if fmt:
        args += ["--format", fmt]
    if duration:
        args += ["--duration", str(duration)]
    if invert:
        args += ["--invert"]
    proc = subprocess.Popen(
        args,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=_env(),
        cwd=str(paths.TESTS_DIR),
        bufsize=1,
    )
    sender = Sender(name, w, h, fps, seed, alpha, adapter, proc)
    deadline = time.monotonic() + ready_timeout
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            err = proc.stderr.read() if proc.stderr else ""
            raise RuntimeError(f"sender '{name}' exited early (rc={proc.returncode}): {err[-2000:]}")
        line = proc.stdout.readline()
        if not line:
            time.sleep(0.05)
            continue
        if line.strip().startswith("{"):
            obj = json.loads(line)
            if obj.get("event") == "started":
                sender.started = obj
                return sender
            if obj.get("event") == "error":
                raise RuntimeError(f"sender '{name}' failed: {obj}")
    proc.kill()
    raise TimeoutError(f"sender '{name}' did not report 'started' within {ready_timeout}s")


def receive(
    name: str,
    frames: int = 30,
    timeout: float = 5.0,
    out: Optional[Path] = None,
    adapter: Optional[str] = None,
    invert: bool = False,
) -> Dict[str, Any]:
    """Receive from a sender. Returns the tool's JSON plus ``image`` (RGBA ndarray or None)."""
    args = _base_command("recv") + ["--name", name, "--frames", str(frames), "--timeout", str(timeout)]
    if out:
        args += ["--out", str(out)]
    if adapter:
        args += ["--adapter", str(adapter)]
    if invert:
        args += ["--invert"]
    result = _run(args, timeout=timeout + 30)
    result["image"] = None
    if result.get("out"):
        result["image"] = load_image(Path(result["out"]))
    return result


def load_image(path: Path) -> Optional[np.ndarray]:
    if not path.exists():
        return None
    if path.suffix.lower() == ".png":
        from PIL import Image

        return pattern.from_pil(Image.open(path))
    meta = json.loads(path.with_suffix(path.suffix + ".json").read_text())
    return pattern.from_raw(path.read_bytes(), meta["width"], meta["height"], meta.get("order", "bgra"))


def list_senders(timeout: float = 15.0) -> List[Dict[str, Any]]:
    return _run(_base_command("list"), timeout=timeout).get("senders", [])


def sender_names(timeout: float = 15.0) -> List[str]:
    return [s["name"] for s in list_senders(timeout)]


def wait_for_sender(name: str, timeout: float = 10.0, present: bool = True, interval: float = 0.25) -> bool:
    deadline = time.monotonic() + timeout
    while True:
        names = sender_names()
        if (name in names) == present:
            return True
        if time.monotonic() >= deadline:
            return False
        time.sleep(interval)
