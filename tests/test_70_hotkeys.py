"""Hotkeys for the Tools output and the Spout Filter (#54), driven through obs-websocket's TriggerHotkeyByName."""
import time

from harness import paths, spout_tool

OUT = paths.OUTPUT_SENDER
A = "OBSTEST_A"
F = paths.FILTER_SENDER


def _hotkey_names(scene):
    return {h for h in scene.call("GetHotkeyList")["hotkeys"]}


def _trigger(scene, name, context=None):
    data = {"hotkeyName": name}
    if context:
        data["contextName"] = context
    scene.call("TriggerHotkeyByName", data)


def _filter_enabled(scene, source, filter_name):
    return bool(scene.call("GetSourceFilter", {"sourceName": source, "filterName": filter_name})["filterEnabled"])


def _advancing(name, frames=8, timeout=3.0):
    r = spout_tool.receive(name, frames=frames, timeout=timeout)
    return r["distinct_frames"] >= 2 and r["pattern_first"] != r["pattern_last"], r


def test_hotkeys_registered(scene, senders, artifacts):
    senders(A, size=(640, 360), seed=0)
    scene.create_receiver("RX", A)
    scene.add_spout_filter("RX", F)
    names = _hotkey_names(scene)
    artifacts.value("spout_hotkeys", sorted(n for n in names if n.startswith("Spout")))
    for name in ("SpoutOutput.Start", "SpoutOutput.Stop", "SpoutFilter.Enable", "SpoutFilter.Disable"):
        assert name in names, f"hotkey {name} not registered"


def test_output_hotkeys_start_stop(scene, artifacts):
    scene.create_color_source("GREY", (128, 128, 128), width=1280, height=720)
    assert not scene.output_active()
    _trigger(scene, "SpoutOutput.Start")
    assert scene.wait(scene.output_active, timeout=3.0), "Start hotkey did not start the output"
    assert spout_tool.wait_for_sender(OUT, timeout=5.0), "output sender not registered after the Start hotkey"
    # Start while running is a no-op.
    _trigger(scene, "SpoutOutput.Start")
    time.sleep(0.3)
    assert scene.output_active()
    _trigger(scene, "SpoutOutput.Stop")
    assert scene.wait(lambda: not scene.output_active(), timeout=3.0), "Stop hotkey did not stop the output"
    assert spout_tool.wait_for_sender(OUT, timeout=5.0, present=False), "output sender still registered after Stop"
    # And it can be started again.
    _trigger(scene, "SpoutOutput.Start")
    assert scene.wait(scene.output_active, timeout=3.0), "Start hotkey did not restart the output"
    _trigger(scene, "SpoutOutput.Stop")
    assert scene.wait(lambda: not scene.output_active(), timeout=3.0)


def test_filter_hotkeys_toggle(scene, senders, artifacts):
    senders(A, size=(640, 360), seed=0)
    scene.create_receiver("RX", A)
    scene.wait_source_size("RX", (640, 360), timeout=2.0)
    filter_name = scene.add_spout_filter("RX", F)
    assert spout_tool.wait_for_sender(F, timeout=5.0), "filter sender never appeared"
    moving, r = _advancing(F)
    assert moving, r

    _trigger(scene, "SpoutFilter.Disable", filter_name)
    assert scene.wait(lambda: not _filter_enabled(scene, "RX", filter_name), timeout=2.0), "Disable hotkey did not disable the filter"
    time.sleep(0.5)
    moving, r = _advancing(F, frames=4, timeout=1.5)
    artifacts.value("disabled", (r["pattern_first"], r["pattern_last"], r["distinct_frames"]))
    assert not moving, f"filter still broadcasting after the Disable hotkey: {r}"

    _trigger(scene, "SpoutFilter.Enable", filter_name)
    assert scene.wait(lambda: _filter_enabled(scene, "RX", filter_name), timeout=2.0), "Enable hotkey did not enable the filter"
    time.sleep(0.3)
    moving, r = _advancing(F)
    artifacts.value("re_enabled", (r["pattern_first"], r["pattern_last"], r["distinct_frames"]))
    assert moving, f"filter did not resume after the Enable hotkey: {r}"
