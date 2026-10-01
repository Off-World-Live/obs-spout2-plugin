"""Fallback Spout sender/receiver on top of the ``SpoutGL`` pip package (OpenGL based).

Same CLI and JSON contract as tests/spout-tool (SpoutDX):

    python -m harness.spoutgl_tool send --name N --size WxH [--fps 30] [--seed K]
                                        [--alpha opaque|straight|premult] [--duration S]
    python -m harness.spoutgl_tool recv --name N [--frames 30] [--timeout 5] [--out file.png]
    python -m harness.spoutgl_tool list

``send`` prints one JSON line ``{"event": "started", ...}`` once the first frame is out and keeps
sending until ``--duration`` elapses or a line is read on stdin (``q``) / stdin closes; it then
prints ``{"event": "stopped", "frames_sent": N, ...}``.

``recv`` prints a single JSON object::

    {"connected": true, "width": W, "height": H, "format": 87, "frame_first": 0, "frame_last": 0,
     "fps": 0.0, "new_frames": reads, "distinct_frames": D, "pattern_first": f0, "pattern_last": f1,
     "elapsed": s, "out": "file.png", "backend": "spoutgl"}

Limitations vs the native tool: no ``--adapter`` (OpenGL picks the GPU; use harness.gpu_prefs),
no ``--format`` (always BGRA8), and Spout's frame counter is not available (``frame_*`` stay 0),
so tests must use the pattern strip (``pattern_*``) / ``distinct_frames`` to prove motion.
"""
from __future__ import annotations

import argparse
import json
import sys
import threading
import time
import zlib
from pathlib import Path

import numpy as np

from harness import pattern

GL_RGBA = 0x1908


def _emit(obj: dict) -> None:
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()


def parse_size(text: str) -> tuple[int, int]:
    w, h = text.lower().split("x")
    return int(w), int(h)


def _stdin_watcher(stop: threading.Event) -> None:
    try:
        for line in sys.stdin:
            if line.strip().lower() in ("q", "quit", "stop", ""):
                break
    except Exception:
        pass
    stop.set()


def cmd_send(a: argparse.Namespace) -> int:
    import SpoutGL

    w, h = parse_size(a.size)
    stop = threading.Event()
    threading.Thread(target=_stdin_watcher, args=(stop,), daemon=True).start()

    img = pattern.make_pattern(w, h, 0, a.seed, a.alpha)
    frame = 0
    sent = 0
    period = 1.0 / a.fps if a.fps > 0 else 0.0
    t_end = time.monotonic() + a.duration if a.duration > 0 else None
    started = False
    with SpoutGL.SpoutSender() as sender:
        sender.setSenderName(a.name)
        next_t = time.monotonic()
        while not stop.is_set():
            if t_end is not None and time.monotonic() >= t_end:
                break
            pattern.paint_strip(img, frame)
            ok = sender.sendImage(img.tobytes(), w, h, GL_RGBA, a.invert, 0)
            if not ok:
                _emit({"event": "error", "message": "sendImage returned false", "frame": frame})
                return 2
            sent += 1
            frame = (frame + 1) & 0xFFFF
            if not started:
                started = True
                _emit(
                    {
                        "event": "started",
                        "backend": "spoutgl",
                        "name": a.name,
                        "width": w,
                        "height": h,
                        "fps": a.fps,
                        "seed": a.seed,
                        "alpha": a.alpha,
                        "adapter": None,
                        "pid": __import__("os").getpid(),
                    }
                )
            next_t += period
            delay = next_t - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            else:
                next_t = time.monotonic()
        sender.releaseSender()
    _emit({"event": "stopped", "backend": "spoutgl", "name": a.name, "frames_sent": sent, "last_frame": frame})
    return 0


def cmd_recv(a: argparse.Namespace) -> int:
    import SpoutGL

    deadline = time.monotonic() + a.timeout
    t0 = time.monotonic()
    buf = None
    w = h = 0
    fmt = None
    reads = 0
    distinct = 0
    last_digest = None
    first_img = last_img = None
    frame_first = frame_last = None
    with SpoutGL.SpoutReceiver() as receiver:
        receiver.setReceiverName(a.name)
        while time.monotonic() < deadline and distinct < a.frames:
            res = receiver.receiveImage(buf, GL_RGBA, a.invert, 0)
            if receiver.isUpdated():
                w, h = receiver.getSenderWidth(), receiver.getSenderHeight()
                fmt = receiver.getSenderFormat()
                buf = bytearray(w * h * 4)
                continue
            if res and buf is not None and w > 0:
                reads += 1
                digest = zlib.adler32(buf)
                if digest != last_digest:
                    last_digest = digest
                    distinct += 1
                    arr = np.frombuffer(bytes(buf), dtype=np.uint8).reshape(h, w, 4)
                    last_img = arr
                    if first_img is None:
                        first_img = arr
                    f = receiver.getSenderFrame()
                    frame_last = f
                    if frame_first is None:
                        frame_first = f
            time.sleep(0.004)
        connected = bool(receiver.isConnected()) and w > 0 and reads > 0
        receiver.releaseReceiver()

    result = {
        "backend": "spoutgl",
        "name": a.name,
        "sender": a.name if connected else None,
        "connected": connected,
        "width": w,
        "height": h,
        "format": fmt,
        "frame_first": frame_first or 0,
        "frame_last": frame_last or 0,
        "fps": 0.0,
        "new_frames": reads,
        "distinct_frames": distinct,
        "pattern_first": pattern.decode_frame(first_img) if first_img is not None else None,
        "pattern_last": pattern.decode_frame(last_img) if last_img is not None else None,
        "elapsed": round(time.monotonic() - t0, 3),
        "out": None,
    }
    if a.out and last_img is not None:
        out = Path(a.out)
        out.parent.mkdir(parents=True, exist_ok=True)
        if out.suffix.lower() == ".png":
            pattern.to_pil(last_img).save(out)
        else:
            out.write_bytes(np.ascontiguousarray(last_img).tobytes())
            out.with_suffix(out.suffix + ".json").write_text(json.dumps({"width": w, "height": h, "order": "rgba"}))
        result["out"] = str(out)
    _emit(result)
    return 0 if connected else 1


def cmd_list(a: argparse.Namespace) -> int:
    import SpoutGL

    senders = []
    with SpoutGL.SpoutReceiver() as receiver:
        for name in receiver.getSenderList():
            info = receiver.getSenderInfo(name)
            senders.append({"name": name, "width": getattr(info, "width", 0), "height": getattr(info, "height", 0)})
    _emit({"backend": "spoutgl", "senders": senders})
    return 0


def main(argv=None) -> int:
    p = argparse.ArgumentParser(prog="spoutgl_tool")
    sub = p.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("send")
    s.add_argument("--name", required=True)
    s.add_argument("--size", default="640x360")
    s.add_argument("--fps", type=float, default=30.0)
    s.add_argument("--seed", type=int, default=0)
    s.add_argument("--alpha", choices=pattern.ALPHA_MODES, default="opaque")
    s.add_argument("--duration", type=float, default=0.0, help="seconds; 0 = until stdin closes")
    s.add_argument("--adapter", default=None, help="ignored (OpenGL backend); see harness.gpu_prefs")
    s.add_argument("--format", default="BGRA", help="ignored (OpenGL backend)")
    s.add_argument("--invert", action="store_true", help="flip rows when uploading")
    s.set_defaults(func=cmd_send)

    r = sub.add_parser("recv")
    r.add_argument("--name", required=True)
    r.add_argument("--frames", type=int, default=30, help="stop after this many distinct frames")
    r.add_argument("--timeout", type=float, default=5.0)
    r.add_argument("--out", default=None, help=".png (or raw .rgba + .json sidecar)")
    r.add_argument("--adapter", default=None, help="ignored (OpenGL backend)")
    r.add_argument("--invert", action="store_true", help="flip rows when reading back")
    r.set_defaults(func=cmd_recv)

    l = sub.add_parser("list")
    l.set_defaults(func=cmd_list)

    a = p.parse_args(argv)
    return a.func(a)


if __name__ == "__main__":
    sys.exit(main())
