"""Spout Filter (`win_spout_filter`) assertions (plan 4.6)."""
import time

import pytest

from harness import paths, pattern, spout_tool

A = "OBSTEST_A"
F = paths.FILTER_SENDER
TOL = 6


def _chain(scene, senders, name=F, size=(640, 360), seed=0):
    """sender -> spout_capture 'RX' -> Spout Filter 'name' (so the filter output carries a moving pattern)."""
    senders(A, size=size, seed=seed)
    scene.create_receiver("RX", A)
    scene.wait_source_size("RX", size, timeout=2.0)
    filter_name = scene.add_spout_filter("RX", name)
    assert spout_tool.wait_for_sender(name, timeout=5.0), f"filter sender {name} never appeared"
    return filter_name


def _advancing(name, frames=8, timeout=3.0):
    r = spout_tool.receive(name, frames=frames, timeout=timeout)
    moving = r["distinct_frames"] >= 2 and r["pattern_first"] != r["pattern_last"]
    return moving, r


def test_filter_sends_source_colour_and_size(scene, senders, artifacts, run_dir):
    scene.create_color_source("ORANGE", (255, 128, 0), width=640, height=360)
    scene.add_spout_filter("ORANGE", F)
    assert spout_tool.wait_for_sender(F, timeout=5.0)
    r = spout_tool.receive(F, frames=2, timeout=3.0, out=run_dir / "tmp-filter.png")
    img = r.pop("image")
    artifacts.value("recv", {k: v for k, v in r.items() if k != "stderr"})
    assert r["connected"]
    assert (r["width"], r["height"]) == (640, 360)
    assert img is not None
    artifacts.image("filter-orange", img)
    means = pattern.region_means(img)
    for name, m in means.items():
        assert pattern.colour_close(m, (255, 128, 0), TOL), f"{name}: {pattern.fmt_rgb(m)} != orange"


def test_filter_frames_advance(scene, senders, artifacts, run_dir):
    _chain(scene, senders)
    moving, r = _advancing(F)
    r2 = spout_tool.receive(F, frames=1, timeout=2.0, out=run_dir / "tmp-filter2.png")
    if r2.get("image") is not None:
        artifacts.image("filter-pattern", r2["image"])
        means = pattern.region_means(r2["image"])
        for name, expected in pattern.expected_quadrants(0).items():
            assert pattern.colour_close(means[name], expected, TOL), f"{name}: {pattern.fmt_rgb(means[name])}"
    artifacts.value("frames", (r["pattern_first"], r["pattern_last"], r["distinct_frames"]))
    assert moving, f"filter output not advancing: {r}"


def test_filter_disabled_stops(scene, senders, artifacts):
    filter_name = _chain(scene, senders)
    moving, r = _advancing(F)
    assert moving, r
    scene.set_filter_enabled("RX", filter_name, False)
    time.sleep(0.5)
    moving, r = _advancing(F, frames=4, timeout=1.5)
    artifacts.value("disabled", (r["pattern_first"], r["pattern_last"], r["distinct_frames"]))
    assert not moving, f"filter still broadcasting while disabled: {r}"
    scene.set_filter_enabled("RX", filter_name, True)
    time.sleep(0.3)
    moving, r = _advancing(F)
    artifacts.value("re_enabled", (r["pattern_first"], r["pattern_last"], r["distinct_frames"]))
    assert moving, f"filter did not resume after re-enable: {r}"


@pytest.mark.xfail(
    strict=True,
    reason="unreported (found by this harness, related to #36/#79): once rendered, the filter keeps broadcasting "
    "off-program and while the scene item is hidden because win_spout_offscreen_render renders the parent "
    "through its own filter chain, which re-enters win_spout_filter_videorender and re-sets is_active",
)
def test_filter_stops_when_source_off_program(scene, senders, artifacts):
    """continuous_broadcast=false (default): a source that is not on program should stop broadcasting."""
    _chain(scene, senders)
    scene.create_scene("Other")
    scene.set_current_scene("Other")
    time.sleep(1.0)
    moving, r = _advancing(F, frames=4, timeout=1.5)
    artifacts.value("off_program", (r["pattern_first"], r["pattern_last"], r["distinct_frames"]))
    scene.set_current_scene(paths.SCENE_NAME)
    scene.set_item_enabled("RX", False)
    time.sleep(1.0)
    moving_hidden, r2 = _advancing(F, frames=4, timeout=1.5)
    artifacts.value("item_hidden", (r2["pattern_first"], r2["pattern_last"], r2["distinct_frames"]))
    scene.set_item_enabled("RX", True)
    assert not moving, f"filter kept broadcasting off-program with continuous_broadcast=false: {r}"
    assert not moving_hidden, f"filter kept broadcasting while the scene item was hidden: {r2}"
    time.sleep(0.5)
    moving, r = _advancing(F)
    assert moving, f"filter did not resume when back on program: {r}"


def test_filter_continues_off_program_with_continuous_variant(obs_manager, senders, artifacts):
    """[win_spout] continuous_broadcast=true (PR #104): keeps rendering the source off-screen."""
    client = obs_manager.get(paths.DEFAULT_PROFILE, "continuous")
    client.clear()
    _chain(client, senders)
    client.create_scene("Other")
    client.set_current_scene("Other")
    time.sleep(1.0)
    moving, r = _advancing(F)
    artifacts.value("off_program_continuous", (r["pattern_first"], r["pattern_last"], r["distinct_frames"]))
    client.clear()
    assert moving, f"continuous broadcast did not keep sending off-program: {r}"


def test_filter_rename(scene, senders, artifacts):
    filter_name = _chain(scene, senders)
    scene.set_filter_settings("RX", filter_name, {"spout_filter_name": "OBSTEST_Renamed"})
    assert spout_tool.wait_for_sender("OBSTEST_Renamed", timeout=5.0), "renamed sender did not appear"
    assert spout_tool.wait_for_sender(F, timeout=5.0, present=False), "old sender name still registered"
    artifacts.value("senders", spout_tool.sender_names())
