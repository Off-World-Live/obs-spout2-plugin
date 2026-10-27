"""pytest glue: results folder, report, OBS lifecycle (one instance per profile/variant), fixtures.

Run through tests/run.ps1 -Test; or directly:
    tests\\.venv\\Scripts\\python.exe -m pytest tests --results-dir tests\\results\\<run>
"""
from __future__ import annotations

import hashlib
import json
import os
import sys
import time
from pathlib import Path
from typing import Dict, Optional

import pytest

TESTS_DIR = Path(__file__).resolve().parent
if str(TESTS_DIR) not in sys.path:
    sys.path.insert(0, str(TESTS_DIR))

from harness import obslog, paths, report, spout_tool  # noqa: E402
from harness.obs_client import ObsClient  # noqa: E402
from harness.obs_launcher import ObsLaunchError, ObsProcess, kill_stale_test_obs  # noqa: E402

UNIT_ONLY = {"unit"}


# -- options ---------------------------------------------------------------------------------------
def pytest_addoption(parser):
    parser.addoption("--results-dir", default=None, help="results/<run> folder (default: results/<timestamp>)")
    parser.addoption("--variant", default=paths.DEFAULT_VARIANT, help="default user.ini variant (fixtures/user-ini-variants)")
    parser.addoption("--update-golden", action="store_true", help="rewrite golden/*.json from the current run")
    parser.addoption("--keep-obs", action="store_true", help="leave the test OBS running at the end of the session")


def pytest_configure(config):
    results = config.getoption("--results-dir")
    run_dir = Path(results) if results else paths.RESULTS_ROOT / time.strftime("%Y%m%d-%H%M%S")
    run_dir.mkdir(parents=True, exist_ok=True)
    config._spout_run_dir = run_dir
    meta = {
        "plugin_dll": str(paths.PLUGIN_DLL),
        "plugin_sha256": _sha256(paths.PLUGIN_DLL),
        "installed_json": _read_json(paths.INSTALLED_JSON),
        "portable_obs": str(paths.PORTABLE_OBS),
        "spout_backend": spout_tool.backend(),
        "python": sys.executable,
        "variant": config.getoption("--variant"),
    }
    config._spout_report = report.RunReport(run_dir, meta)
    config._spout_index = 0


def _sha256(path: Path) -> Optional[str]:
    try:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    except OSError:
        return None


def _read_json(path: Path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


# -- reporting hooks -------------------------------------------------------------------------------
def _status_of(rep) -> str:
    if hasattr(rep, "wasxfail"):
        return "xfailed" if rep.outcome == "skipped" else "xpassed"
    if rep.outcome == "failed":
        return "failed" if rep.when == "call" else "error"
    return rep.outcome  # passed / skipped


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    rep = outcome.get_result()
    record = (
        rep.when == "call"
        or (rep.when == "setup" and rep.outcome != "passed")
        or (rep.when == "teardown" and rep.outcome == "failed")
    )
    if not record:
        return
    status = _status_of(rep)
    if rep.when == "teardown":
        status = "error"
    reason = ""
    if hasattr(rep, "wasxfail"):
        reason = str(rep.wasxfail)
    elif status == "skipped" and isinstance(rep.longrepr, tuple):
        reason = str(rep.longrepr[2])
    message = ""
    if rep.longrepr is not None and status != "passed":
        message = str(getattr(rep, "longreprtext", rep.longrepr))
    artifacts = getattr(item, "_spout_artifacts", None)
    manager: Optional[ObsManager] = getattr(item.session.config, "_spout_manager", None)
    excerpt = []
    profile = variant = ""
    if manager and manager.current:
        profile, variant = manager.current.profile, manager.current.variant
        if status in ("failed", "error", "xfailed", "xpassed"):
            excerpt = obslog.excerpt(manager.current.log_text())
    nodeid = item.nodeid + (" (teardown)" if rep.when == "teardown" else "")
    item.session.config._spout_report.add(
        report.TestResult(nodeid, status, rep.duration, message, reason, artifacts, excerpt, profile, variant)
    )


def pytest_sessionfinish(session, exitstatus):
    config = session.config
    manager: Optional[ObsManager] = getattr(config, "_spout_manager", None)
    if manager is not None:
        manager.shutdown(keep=config.getoption("--keep-obs"))
    rep: report.RunReport = config._spout_report
    path = rep.write()
    counts = rep.counts()
    print(f"\nREPORT: {path}")
    print("COUNTS: " + json.dumps(counts))


# -- OBS lifecycle ---------------------------------------------------------------------------------
class ObsManager:
    """Keeps one portable OBS alive and restarts it only when profile/variant change."""

    def __init__(self, run_dir: Path, rep: report.RunReport, default_variant: str):
        self.run_dir = run_dir
        self.report = rep
        self.default_variant = default_variant
        self.current: Optional[ObsProcess] = None
        self.client: Optional[ObsClient] = None
        self.launches = 0
        self.last_error: Optional[str] = None

    def get(self, profile: str = paths.DEFAULT_PROFILE, variant: Optional[str] = None, fresh: bool = False) -> ObsClient:
        variant = variant or self.default_variant
        cur = self.current
        if cur is not None and not fresh and cur.profile == profile and cur.variant == variant and cur.alive() and self.client is not None:
            try:
                self.client.version()
                return self.client
            except Exception:
                pass
        self.stop()
        kill_stale_test_obs()
        self.launches += 1
        proc = ObsProcess(profile, variant, self.run_dir, label=f"{self.launches:02d}")
        try:
            proc.launch()
            self.client = proc.wait_ready()
        except ObsLaunchError as exc:
            self.last_error = str(exc)
            self._record(proc)
            pytest.fail(f"OBS launch failed: {exc}", pytrace=False)
        self.current = proc
        proc.desktop_screenshot(self.run_dir / f"desktop-{proc.label}-{profile}-{variant}.png")
        return self.client

    def restart(self) -> ObsClient:
        cur = self.current
        profile = cur.profile if cur else paths.DEFAULT_PROFILE
        variant = cur.variant if cur else self.default_variant
        return self.get(profile, variant, fresh=True)

    def stop(self) -> Optional[Path]:
        cur = self.current
        if cur is None:
            return None
        if self.client is not None:
            try:
                self.client.clear()
            except Exception:
                pass
        path = cur.stop()
        self._record(cur)
        self.current = None
        self.client = None
        return path

    def _record(self, proc: ObsProcess) -> None:
        text = obslog.read_log(proc.copied_log) if proc.copied_log else proc.log_text()
        chk = obslog.check(text, expect_clean_exit=bool(proc.clean_exit))
        self.report.add_log(proc.copied_log, proc.profile, proc.variant, proc.clean_exit, proc.notes, chk)

    def log_text(self) -> str:
        return self.current.log_text() if self.current else ""

    def shutdown(self, keep: bool = False) -> None:
        if keep:
            return
        self.stop()


@pytest.fixture(scope="session")
def run_dir(request) -> Path:
    return request.config._spout_run_dir


@pytest.fixture(scope="session")
def obs_manager(request) -> ObsManager:
    mgr = ObsManager(request.config._spout_run_dir, request.config._spout_report, request.config.getoption("--variant"))
    request.config._spout_manager = mgr
    yield mgr


@pytest.fixture(scope="module")
def obs(obs_manager) -> ObsClient:
    """A ready OBS on the default profile/variant (restarted only if a previous test changed it)."""
    return obs_manager.get(paths.DEFAULT_PROFILE)


@pytest.fixture
def scene(obs_manager, obs) -> ObsClient:
    """Function-scoped view of a default-profile OBS; everything created is removed afterwards.

    If a previous test switched profile/variant (or stopped OBS), this transparently relaunches.
    """
    client = obs_manager.get(paths.DEFAULT_PROFILE)
    client.clear()
    yield client
    if obs_manager.client is not None and obs_manager.current and obs_manager.current.alive():
        obs_manager.client.clear()


@pytest.fixture
def artifacts(request) -> report.Artifacts:
    cfg = request.config
    cfg._spout_index += 1
    art = report.Artifacts(cfg._spout_run_dir, cfg._spout_index, request.node.name)
    request.node._spout_artifacts = art
    return art


@pytest.fixture
def senders():
    """Factory for synthetic senders; all are stopped at teardown."""
    started = []

    def start(name: str, **kw) -> spout_tool.Sender:
        s = spout_tool.start_sender(name, **kw)
        started.append(s)
        return s

    yield start
    for s in started:
        try:
            s.stop()
        except Exception:
            pass


class Golden:
    def __init__(self, update: bool):
        self.update = update

    def path(self, name: str) -> Path:
        return paths.GOLDEN_DIR / f"{name}.json"

    def load(self, name: str) -> Optional[Dict]:
        p = self.path(name)
        return json.loads(p.read_text(encoding="utf-8")) if p.exists() else None

    def save(self, name: str, data: Dict) -> Path:
        p = self.path(name)
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(json.dumps(report._jsonable(data), indent=2, sort_keys=True), encoding="utf-8")
        return p

    def compare_or_create(self, name: str, actual: Dict[str, tuple], tol: float, artifacts: report.Artifacts) -> None:
        """Compare colour triples per key; create the golden (and skip) when it does not exist yet."""
        expected = None if self.update else self.load(name)
        if expected is None:
            p = self.save(name, actual)
            artifacts.note(f"golden created: {p} — review the PNGs once, then re-run")
            pytest.skip(f"golden '{name}' created at {p}; review the images and re-run")
        bad = []
        for key, exp in expected.items():
            act = actual.get(key)
            if act is None:
                bad.append(f"{key}: missing")
                continue
            if any(abs(float(a) - float(e)) > tol for a, e in zip(act[:3], exp[:3])):
                bad.append(f"{key}: actual {tuple(round(float(a), 1) for a in act[:3])} vs golden {tuple(exp[:3])} (tol {tol})")
        assert not bad, "golden mismatch for " + name + ":\n  " + "\n  ".join(bad)


@pytest.fixture
def golden(request) -> Golden:
    return Golden(request.config.getoption("--update-golden"))


@pytest.fixture(scope="session")
def default_variant(request) -> str:
    return request.config.getoption("--variant")


def pytest_collection_modifyitems(config, items):
    """Mark everything outside tests/unit as needing OBS so hosted CI can deselect with -m 'not obs'."""
    for item in items:
        rel = Path(str(item.fspath)).resolve()
        try:
            top = rel.relative_to(TESTS_DIR).parts[0]
        except ValueError:
            top = ""
        if top not in UNIT_ONLY and top != "":
            item.add_marker(pytest.mark.obs)
        elif rel.parent == TESTS_DIR:
            item.add_marker(pytest.mark.obs)
