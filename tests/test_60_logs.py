"""OBS log gate (plan 4.6 'Logs'): runs last so it sees every log copied during the session."""
import time

import pytest

from harness import obslog, paths


def test_live_log_health(obs_manager, artifacts):
    obs_manager.get(paths.DEFAULT_PROFILE)
    text = obs_manager.log_text()
    chk = obslog.check(text)
    artifacts.value("check", chk.__dict__)
    assert chk.ok, chk


def test_all_run_logs_free_of_forbidden_lines(run_dir, artifacts):
    logs = sorted(run_dir.glob("obs-*.log"))
    assert logs, "no OBS logs were copied into the results folder"
    problems = []
    for log in logs:
        chk = obslog.check(obslog.read_log(log))
        artifacts.value(log.name, {"forbidden": chk.forbidden_hits, "warnings": chk.warnings, "errors": chk.errors})
        if chk.missing_required or chk.forbidden_hits:
            problems.append(f"{log.name}: missing={chk.missing_required} forbidden={chk.forbidden_hits}")
    assert not problems, "\n".join(problems)


def test_no_crash_files(request, artifacts):
    started = request.config._spout_report.started
    crashes = [p for p in obslog.crash_files(paths.OBS_CRASH_DIR) if p.stat().st_mtime >= started - 5]
    artifacts.value("crash_files", [p.name for p in crashes])
    assert not crashes, f"OBS wrote crash files during this run: {[p.name for p in crashes]}"


def test_clean_exit_writes_unload_line(obs_manager, artifacts):
    obs_manager.get(paths.DEFAULT_PROFILE)
    previous = obs_manager.current
    obs_manager.restart()
    text = obslog.read_log(previous.copied_log)
    artifacts.value("clean_exit", previous.clean_exit)
    artifacts.value("notes", previous.notes)
    assert previous.clean_exit is True, previous.notes
    chk = obslog.check(text, expect_clean_exit=True)
    assert chk.ok, chk
    assert "Number of memory leaks" in text


def test_warning_baseline(obs_manager, golden, artifacts):
    """Warning/error counts of a fresh OBS start vs tests/golden/log-baseline.json."""
    obs_manager.restart()
    time.sleep(1.0)
    chk = obslog.check(obs_manager.log_text())
    actual = {"warnings": chk.warnings, "errors": chk.errors, "plugin_warnings": chk.plugin_warnings}
    artifacts.value("actual", actual)
    baseline = None if golden.update else golden.load("log-baseline")
    if baseline is None:
        p = golden.save("log-baseline", actual)
        pytest.skip(f"log baseline created at {p}; re-run to compare")
    artifacts.value("baseline", baseline)
    assert actual["plugin_warnings"] <= baseline["plugin_warnings"], f"more [win_spout] warnings/errors than baseline: {actual} vs {baseline}"
    assert actual["errors"] <= baseline["errors"], f"more error lines than baseline: {actual} vs {baseline}"
    assert actual["warnings"] <= baseline["warnings"] + 2, f"warning count grew: {actual} vs {baseline}"
