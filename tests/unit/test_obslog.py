"""Log parser tests on a fixture log (no OBS needed)."""
from pathlib import Path

from harness import obslog

FIXTURE = Path(__file__).parent / "fixtures" / "obs-sample.log"


def test_check_required_and_counts():
    text = obslog.read_log(FIXTURE)
    chk = obslog.check(text, expect_clean_exit=True)
    assert chk.ok, chk
    assert chk.missing_required == []
    assert chk.forbidden_hits == []
    assert (chk.warnings, chk.errors) == (1, 1)
    assert chk.plugin_warnings == 1


def test_forbidden_detected():
    text = obslog.read_log(FIXTURE) + "\n13:11:40.000: Failed to load module file 'win-spout.dll'\n"
    chk = obslog.check(text)
    assert "Failed to load module file" in chk.forbidden_hits
    assert obslog.fatal_startup_lines(text) == ["Failed to load module file 'win-spout.dll'"]


def test_send_texture_allowance():
    base = obslog.read_log(FIXTURE)
    one_filter = base + "\n13:11:21.000: - filter 'Spout Filter' (win_spout_filter) added to source 'X'\n13:11:21.050: Error calling SendTexture()!\n"
    assert obslog.check(one_filter).ok
    assert not obslog.check(one_filter + "13:11:22.000: Error calling SendTexture()!\n").ok


def test_missing_unload_when_expected():
    text = obslog.read_log(FIXTURE).replace("win-spout unloaded!", "")
    assert obslog.check(text, expect_clean_exit=True).missing_required == ["win-spout unloaded!"]
    assert obslog.check(text, expect_clean_exit=False).missing_required == []


def test_loaded_modules_and_excerpt():
    text = obslog.read_log(FIXTURE)
    assert obslog.loaded_modules(text) == ["win-spout.dll", "win-wasapi.dll", "obs-websocket.dll"]
    lines = obslog.excerpt(text)
    assert lines and all("[win_spout]" in l for l in lines)
    assert any("has changed / gone away" in l for l in lines)


def test_parse_lines():
    lines = obslog.parse(obslog.read_log(FIXTURE))
    assert lines[0].time == "13:11:11.551"
    assert lines[0].text.startswith("CPU Name")


def test_newest_log(tmp_path):
    (tmp_path / "2026-01-01 10-00-00.txt").write_text("a")
    (tmp_path / "2026-01-02 10-00-00.txt").write_text("b")
    assert obslog.newest_log(tmp_path).name == "2026-01-02 10-00-00.txt"
    assert obslog.newest_log(tmp_path, after=[tmp_path / "2026-01-02 10-00-00.txt"]).name == "2026-01-01 10-00-00.txt"
    assert obslog.newest_log(tmp_path / "missing") is None
