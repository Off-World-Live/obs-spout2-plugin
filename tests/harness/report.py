"""results/<run>/report.md + summary.json + per-test PNGs (plan section 4.7).

Images are saved twice: ``NN-<test>-<label>.png`` downscaled to <= 640 px wide for reading and
``NN-<test>-<label>.full.png`` at native size for pixel assertions / re-checking.
Failed tests come first in report.md, each with its assertion text, recorded values, images and
the ``[win_spout]`` log excerpt.
"""
from __future__ import annotations

import json
import re
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional

import numpy as np

SMALL_WIDTH = 640
ORDER = {"failed": 0, "error": 1, "xpassed": 2, "xfailed": 3, "skipped": 4, "passed": 5}


def checkerboard(pil, cell: int = 16):
    """Composite an RGBA PIL image over a grey checkerboard (returns RGB)."""
    from PIL import Image

    if pil.mode != "RGBA":
        return pil.convert("RGB")
    w, h = pil.size
    yy, xx = np.mgrid[0:h, 0:w]
    light = ((xx // cell + yy // cell) % 2 == 0)
    bg = np.where(light[..., None], 200, 150).astype(np.uint8).repeat(3, axis=2)
    base = Image.fromarray(bg, "RGB")
    base.paste(pil, (0, 0), pil)
    return base


def assertion_summary(longrepr: str, limit: int = 12) -> str:
    """Only the 'E ' assertion lines (and the failing file:line) of a pytest longrepr."""
    lines = [l for l in longrepr.splitlines() if l.startswith("E ") or re.match(r"^[\w./\\-]+\.py:\d+: ", l)]
    return "\n".join(lines[-limit:]) if lines else longrepr[-1500:]


def _safe(text: str) -> str:
    return re.sub(r"[^A-Za-z0-9._-]+", "_", text).strip("_")[:80]


def _jsonable(value):
    if isinstance(value, (np.floating, np.integer)):
        return value.item()
    if isinstance(value, np.ndarray):
        return value.tolist()
    if isinstance(value, (list, tuple)):
        return [_jsonable(v) for v in value]
    if isinstance(value, dict):
        return {str(k): _jsonable(v) for k, v in value.items()}
    if isinstance(value, Path):
        return str(value)
    return value


@dataclass
class Artifacts:
    """Per-test collector handed to tests through the ``artifacts`` fixture."""

    run_dir: Path
    index: int
    test_name: str
    images: List[Dict[str, str]] = field(default_factory=list)
    values: Dict[str, Any] = field(default_factory=dict)
    notes: List[str] = field(default_factory=list)

    def image(self, label: str, img, full: bool = True) -> Path:
        """Save an RGBA ndarray / PIL image. Returns the small PNG path."""
        from PIL import Image

        if isinstance(img, np.ndarray):
            pil = Image.fromarray(np.ascontiguousarray(img), "RGBA" if img.shape[-1] == 4 else "RGB")
        else:
            pil = img
        stem = f"{self.index:02d}-{_safe(self.test_name)}-{_safe(label)}"
        small_path = self.run_dir / f"{stem}.png"
        entry = {"label": label, "small": small_path.name, "size": f"{pil.width}x{pil.height}"}
        if full:
            full_path = self.run_dir / f"{stem}.full.png"
            pil.save(full_path)
            entry["full"] = full_path.name
        small = pil
        if pil.width > SMALL_WIDTH:
            small = pil.resize((SMALL_WIDTH, max(1, int(pil.height * SMALL_WIDTH / pil.width))))
        # The reading copy is flattened onto a checkerboard so a transparent (0,0,0,0) source is
        # visibly different from a black one; the .full.png keeps the alpha channel.
        checkerboard(small).save(small_path)
        self.images.append(entry)
        return small_path

    def value(self, key: str, value: Any) -> Any:
        self.values[key] = _jsonable(value)
        return value

    def note(self, text: str) -> None:
        self.notes.append(text)


@dataclass
class TestResult:
    nodeid: str
    outcome: str
    duration: float
    message: str = ""
    reason: str = ""
    artifacts: Optional[Artifacts] = None
    log_excerpt: List[str] = field(default_factory=list)
    profile: str = ""
    variant: str = ""


class RunReport:
    def __init__(self, run_dir: Path, meta: Optional[Dict[str, Any]] = None):
        self.run_dir = run_dir
        self.meta: Dict[str, Any] = meta or {}
        self.results: List[TestResult] = []
        self.logs: List[Dict[str, Any]] = []
        self.started = time.time()

    def add(self, result: TestResult) -> None:
        self.results.append(result)

    def add_log(self, path: Optional[Path], profile: str, variant: str, clean: Optional[bool], notes: List[str], check=None) -> None:
        self.logs.append(
            {
                "file": path.name if path else None,
                "profile": profile,
                "variant": variant,
                "clean_exit": clean,
                "notes": list(notes),
                "check": _jsonable(check.__dict__) if check is not None else None,
            }
        )

    def counts(self) -> Dict[str, int]:
        out: Dict[str, int] = {}
        for r in self.results:
            out[r.outcome] = out.get(r.outcome, 0) + 1
        return out

    @property
    def report_path(self) -> Path:
        return self.run_dir / "report.md"

    def write(self) -> Path:
        self.run_dir.mkdir(parents=True, exist_ok=True)
        counts = self.counts()
        summary = {
            "run_dir": str(self.run_dir),
            "started": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(self.started)),
            "duration_s": round(time.time() - self.started, 1),
            "counts": counts,
            "meta": _jsonable(self.meta),
            "tests": [
                {
                    "nodeid": r.nodeid,
                    "outcome": r.outcome,
                    "duration_s": round(r.duration, 2),
                    "message": r.message,
                    "reason": r.reason,
                    "profile": r.profile,
                    "variant": r.variant,
                    "values": r.artifacts.values if r.artifacts else {},
                    "images": r.artifacts.images if r.artifacts else [],
                    "notes": r.artifacts.notes if r.artifacts else [],
                }
                for r in self.results
            ],
            "logs": self.logs,
        }
        (self.run_dir / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
        self.report_path.write_text(self._markdown(summary), encoding="utf-8")
        return self.report_path

    def _markdown(self, summary: Dict[str, Any]) -> str:
        lines: List[str] = []
        counts = summary["counts"]
        lines.append(f"# OBS Spout2 plugin test run — {summary['started']}")
        lines.append("")
        lines.append("| outcome | count |")
        lines.append("|---|---|")
        for key in sorted(counts, key=lambda k: ORDER.get(k, 9)):
            lines.append(f"| {key} | {counts[key]} |")
        lines.append("")
        lines.append("Run metadata:")
        for k, v in summary["meta"].items():
            lines.append(f"- **{k}**: `{v}`")
        lines.append("")

        ordered = sorted(self.results, key=lambda r: (ORDER.get(r.outcome, 9), r.nodeid))
        groups = [
            ("Failures", [r for r in ordered if r.outcome in ("failed", "error")]),
            ("Unexpected passes (XPASS — drop the xfail marker)", [r for r in ordered if r.outcome == "xpassed"]),
            ("Expected failures (known issues)", [r for r in ordered if r.outcome == "xfailed"]),
            ("Skipped", [r for r in ordered if r.outcome == "skipped"]),
            ("Passed", [r for r in ordered if r.outcome == "passed"]),
        ]
        for title, items in groups:
            if not items:
                continue
            lines.append(f"## {title} ({len(items)})")
            lines.append("")
            for r in items:
                verbose = r.outcome in ("failed", "error", "xpassed", "xfailed")
                lines.append(f"### {r.outcome.upper()} — `{r.nodeid}` ({r.duration:.1f} s)")
                if r.profile or r.variant:
                    lines.append(f"profile `{r.profile}` · variant `{r.variant}`")
                if r.reason:
                    lines.append(f"reason: {r.reason}")
                if r.message and (verbose or r.outcome == "skipped"):
                    msg = r.message.strip()
                    if r.outcome in ("xfailed", "xpassed"):
                        msg = assertion_summary(msg)
                    if len(msg) > 3000:
                        msg = msg[:3000] + "\n... (truncated)"
                    lines.append("")
                    lines.append("```")
                    lines.append(msg)
                    lines.append("```")
                if r.artifacts and r.artifacts.values:
                    lines.append("")
                    lines.append("| value | |")
                    lines.append("|---|---|")
                    for k, v in r.artifacts.values.items():
                        lines.append(f"| {k} | `{v}` |")
                if r.artifacts and r.artifacts.notes:
                    lines.append("")
                    for n in r.artifacts.notes:
                        lines.append(f"- {n}")
                if r.artifacts and r.artifacts.images:
                    lines.append("")
                    for im in r.artifacts.images:
                        full = f" ([full]({im['full']}))" if im.get("full") else ""
                        lines.append(f"- {im['label']} ({im['size']}): [{im['small']}]({im['small']}){full}")
                if verbose and r.log_excerpt:
                    lines.append("")
                    lines.append("<details><summary>[win_spout] log excerpt</summary>")
                    lines.append("")
                    lines.append("```")
                    lines.extend(r.log_excerpt[-40:])
                    lines.append("```")
                    lines.append("</details>")
                lines.append("")

        if self.logs:
            lines.append("## OBS logs")
            lines.append("")
            lines.append("| file | profile | variant | clean exit | warnings | errors | forbidden | notes |")
            lines.append("|---|---|---|---|---|---|---|---|")
            for lg in self.logs:
                chk = lg.get("check") or {}
                lines.append(
                    f"| [{lg['file']}]({lg['file']}) | {lg['profile']} | {lg['variant']} | {lg['clean_exit']} | "
                    f"{chk.get('warnings', '')} | {chk.get('errors', '')} | {', '.join(chk.get('forbidden_hits', []) or []) or '-'} | "
                    f"{'; '.join(lg['notes']) or '-'} |"
                )
            lines.append("")
        return "\n".join(lines)
