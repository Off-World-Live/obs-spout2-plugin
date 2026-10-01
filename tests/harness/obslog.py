"""OBS log helpers: find the live log, check required/forbidden lines, count warnings."""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, List

PLUGIN_TAG = "[win_spout]"

REQUIRED = [
    "win-spout loaded!",
    "Opened DX11",
]
REQUIRED_ON_CLEAN_EXIT = [
    "win-spout unloaded!",
]
FORBIDDEN = [
    "Failed to load module file",
    "Duplicate library",
    "compiled with newer libobs",
    "Failed to Open DX11",
    "Failed to retrieve OBS d3d11 device",
    "Failed to init DX11 for spout filter",
    "Unable to begin data capture!",
    "Unable to start capture!",
]
# 1.12.0 logs this once on the first frame after a filter is created (texrender_prev is still
# empty), so one occurrence per "filter ... (win_spout_filter) added" line is tolerated; any
# excess is reported as a forbidden hit.
SEND_TEXTURE_ERROR = "Error calling SendTexture()!"
FILTER_ADDED = "(win_spout_filter) added to source"
# Lines whose presence means OBS could not load the plugin at all; readiness fails fast on them.
FATAL_AT_STARTUP = [
    "Failed to load module file 'win-spout.dll'",
    "Duplicate library",
    "compiled with newer libobs",
]

_LINE_RE = re.compile(r"^(?P<time>\d\d:\d\d:\d\d\.\d{3}): (?P<text>.*)$")
_WARN_RE = re.compile(r"^\d\d:\d\d:\d\d\.\d{3}: warning: ")
_ERR_RE = re.compile(r"^\d\d:\d\d:\d\d\.\d{3}: error: ")


@dataclass
class LogLine:
    time: str
    text: str
    raw: str


@dataclass
class LogCheck:
    missing_required: List[str] = field(default_factory=list)
    forbidden_hits: List[str] = field(default_factory=list)
    warnings: int = 0
    errors: int = 0
    plugin_warnings: int = 0

    @property
    def ok(self) -> bool:
        return not self.missing_required and not self.forbidden_hits


def newest_log(log_dir: Path, after: Iterable[Path] | None = None) -> Path | None:
    """Newest OBS log file (OBS names them 'YYYY-MM-DD HH-MM-SS.txt'); optionally excluding a snapshot."""
    if not log_dir.exists():
        return None
    exclude = {Path(p).name for p in (after or [])}
    logs = sorted((p for p in log_dir.glob("*.txt") if p.name not in exclude), key=lambda p: p.name)
    return logs[-1] if logs else None


def read_log(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except FileNotFoundError:
        return ""


def parse(text: str) -> List[LogLine]:
    out: List[LogLine] = []
    for raw in text.splitlines():
        m = _LINE_RE.match(raw)
        if m:
            out.append(LogLine(m.group("time"), m.group("text"), raw))
        elif out:
            # continuation line (OBS prints multi-line blocks without timestamps)
            out.append(LogLine(out[-1].time, raw.strip(), raw))
    return out


def count_warnings(text: str) -> tuple[int, int]:
    warnings = sum(1 for line in text.splitlines() if _WARN_RE.match(line))
    errors = sum(1 for line in text.splitlines() if _ERR_RE.match(line))
    return warnings, errors


def loaded_modules(text: str) -> List[str]:
    mods: List[str] = []
    lines = text.splitlines()
    for i, line in enumerate(lines):
        if "Loaded Modules:" in line:
            for follow in lines[i + 1 :]:
                m = _LINE_RE.match(follow)
                name = (m.group("text") if m else follow).strip()
                if not name or not name.lower().endswith(".dll"):
                    break
                mods.append(name)
            break
    return mods


def check(text: str, expect_clean_exit: bool = False, extra_forbidden: Iterable[str] = ()) -> LogCheck:
    result = LogCheck()
    required = list(REQUIRED) + (list(REQUIRED_ON_CLEAN_EXIT) if expect_clean_exit else [])
    for needle in required:
        if needle not in text:
            result.missing_required.append(needle)
    for needle in list(FORBIDDEN) + list(extra_forbidden):
        if needle in text:
            result.forbidden_hits.append(needle)
    excess = send_texture_error_excess(text)
    if excess > 0:
        result.forbidden_hits.append(f"{SEND_TEXTURE_ERROR} x{excess} beyond the one-per-filter-creation allowance")
    result.warnings, result.errors = count_warnings(text)
    result.plugin_warnings = sum(
        1 for line in text.splitlines() if PLUGIN_TAG in line and (_WARN_RE.match(line) or _ERR_RE.match(line))
    )
    return result


def send_texture_error_excess(text: str) -> int:
    errors = text.count(SEND_TEXTURE_ERROR)
    allowed = text.count(FILTER_ADDED)
    return errors - allowed


def fatal_startup_lines(text: str) -> List[str]:
    return [n for n in FATAL_AT_STARTUP if n in text]


def excerpt(text: str, needle: str = PLUGIN_TAG, limit: int = 80, tail: bool = True) -> List[str]:
    lines = [line for line in text.splitlines() if needle in line]
    return lines[-limit:] if tail else lines[:limit]


def crash_files(crash_dir: Path) -> List[Path]:
    if not crash_dir.exists():
        return []
    return sorted(p for p in crash_dir.iterdir() if p.is_file())
