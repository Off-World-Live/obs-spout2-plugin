"""Installed plugin sanity + the OBS launch/readiness/shutdown loop (plan 4.2 / 4.3)."""
import hashlib
import json

import pytest

from harness import obslog, paths
from harness.obs_launcher import sync_plugin_into_portable


def _sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def test_installed_plugin_present():
    bin_dir = paths.PLUGIN_INSTALL_DIR / "bin" / "64bit"
    missing = [n for n in ("win-spout.dll", "Spout.dll", "SpoutDX.dll", "SpoutLibrary.dll") if not (bin_dir / n).exists()]
    assert not missing, f"missing in {bin_dir}: {missing}"
    assert (paths.PLUGIN_INSTALL_DIR / "data" / "locale" / "en-US.ini").exists()


def test_installed_matches_build(artifacts):
    artifacts.value("installed_sha256", _sha256(paths.PLUGIN_DLL))
    if not paths.INSTALLED_JSON.exists():
        pytest.skip("no tests/.stage/installed.json (run.ps1 -Install not used) - testing the plugin that is installed, presumably the release")
    recorded = json.loads(paths.INSTALLED_JSON.read_text(encoding="utf-8"))
    artifacts.value("recorded", recorded)
    assert _sha256(paths.PLUGIN_DLL) == recorded["sha256"], "installed DLL differs from what run.ps1 -Install recorded (stale install?)"
    if paths.BUILD_OUTPUT_DLL.exists():
        assert _sha256(paths.BUILD_OUTPUT_DLL) == recorded["sha256"], "release/ build output is newer than the installed DLL - run run.ps1 -Install"
    else:
        artifacts.note("no build output present; only the install record was checked")


def test_portable_mirror_matches_installed(artifacts):
    hashes = sync_plugin_into_portable()
    mirrored = paths.PORTABLE_OBS / "obs-plugins" / "64bit" / "win-spout.dll"
    artifacts.value("mirrored_sha256", _sha256(mirrored))
    assert _sha256(mirrored) == hashes["win-spout.dll"]
    assert (paths.PORTABLE_OBS / "data" / "obs-plugins" / "win-spout" / "locale" / "en-US.ini").exists()


def test_obs_launches_and_plugin_loads(obs_manager, artifacts):
    client = obs_manager.get(paths.DEFAULT_PROFILE)
    proc = obs_manager.current
    version = client.version()
    artifacts.value("obs_version", version["obsVersion"])
    artifacts.value("websocket_version", version["obsWebSocketVersion"])
    artifacts.value("ready_seconds", proc.ready_seconds)
    kinds = client.call("GetInputKindList", {"unversioned": True})["inputKinds"]
    assert paths.RECEIVER_KIND in kinds, f"spout_capture not registered; kinds={kinds}"
    text = proc.log_text()
    chk = obslog.check(text)
    assert chk.ok, chk
    assert client.current_profile() == paths.DEFAULT_PROFILE
    assert client.current_scene() == paths.SCENE_NAME
    assert client.input_names() == []
    img = client.screenshot()
    artifacts.image("empty-scene", img)
    assert img.shape[:2] == (720, 1280)
    assert any(o["outputName"] == paths.OUTPUT_NAME for o in client.outputs())


def test_clean_restart_no_sentinel_prompt(obs_manager, artifacts):
    """Stop cleanly, relaunch: the previous log must contain the unload line and the new OBS must be ready."""
    first = obs_manager.current
    obs_manager.restart()
    assert first.clean_exit is True, first.notes
    prev = obslog.read_log(first.copied_log)
    chk = obslog.check(prev, expect_clean_exit=True)
    artifacts.value("previous_log", first.copied_log.name)
    assert chk.ok, chk
    assert not paths.OBS_SENTINEL.exists()
    assert obs_manager.client.stats()["activeFps"] > 0
