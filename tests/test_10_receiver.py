"""Spout2 Capture (receiver, `spout_capture`) assertions (plan 4.6)."""
import time

import pytest

from harness import obslog, paths, pattern

COLOUR_TOL = 6
A = "OBSTEST_A"
B = "OBSTEST_B"


def _means(img):
    return {k: tuple(round(x, 1) for x in v[:3]) for k, v in pattern.region_means(img).items()}


def assert_quadrants(img, seed, tol=COLOUR_TOL, where=""):
    means = pattern.region_means(img)
    bad = []
    for name, expected in pattern.expected_quadrants(seed).items():
        if not pattern.colour_close(means[name], expected, tol):
            bad.append(f"{name}: got {pattern.fmt_rgb(means[name])} expected {expected}")
    assert not bad, f"quadrant colours off {where}: " + "; ".join(bad)


def test_sender_listed_in_dropdown(scene, senders):
    senders(A, size=(640, 360))
    scene.create_receiver("RX")
    items = [i["itemValue"] for i in scene.property_items("RX", "spoutsenders")]
    assert items[0] == "usefirstavailablesender"
    assert A in items


def test_size_and_colours(scene, senders, artifacts):
    senders(A, size=(640, 360), seed=0)
    scene.create_receiver("RX", A)
    t0 = time.monotonic()
    size = scene.wait_source_size("RX", (640, 360), timeout=2.0)
    artifacts.value("size_after_s", round(time.monotonic() - t0, 2))
    assert size == (640, 360)
    img = scene.screenshot("RX")
    artifacts.image("rx", img)
    artifacts.value("means", _means(img))
    assert img.shape[:2] == (360, 640)
    assert_quadrants(img, 0)
    frame = pattern.decode_frame(img)
    artifacts.value("frame", frame)
    assert frame is not None, "frame strip not decodable"


def test_frames_advance(scene, senders, artifacts):
    senders(A, size=(640, 360), fps=30)
    scene.create_receiver("RX", A)
    scene.wait_source_size("RX", (640, 360), timeout=2.0)
    f1 = pattern.decode_frame(scene.screenshot("RX"))
    time.sleep(0.5)
    f2 = pattern.decode_frame(scene.screenshot("RX"))
    artifacts.value("frames", (f1, f2))
    assert f1 is not None and f2 is not None
    assert f2 != f1, "receiver appears frozen (same frame 500 ms apart)"
    assert 1 <= ((f2 - f1) & 0xFFFF) <= 60


def test_named_vs_first_available(scene, senders, artifacts):
    senders(A, size=(320, 180), seed=0)
    senders(B, size=(320, 180), seed=1)
    scene.create_receiver("RX_B", B)
    scene.create_receiver("RX_ANY")
    scene.wait_source_size("RX_B", (320, 180), timeout=2.0)
    scene.wait_source_size("RX_ANY", (320, 180), timeout=2.0)
    items = [i["itemValue"] for i in scene.property_items("RX_B", "spoutsenders")]
    assert A in items and B in items
    img_b = scene.screenshot("RX_B")
    artifacts.image("rx-named-B", img_b)
    assert_quadrants(img_b, 1, where="(named sender B)")
    img_any = scene.screenshot("RX_ANY")
    artifacts.image("rx-first-available", img_any)
    means = pattern.region_means(img_any)
    matches = [s for s in (0, 1) if all(pattern.colour_close(means[n], e, COLOUR_TOL) for n, e in pattern.expected_quadrants(s).items())]
    artifacts.value("first_available_matches_seed", matches)
    assert matches, f"first-available receiver shows neither sender: {_means(img_any)}"


def test_sender_resize_follows(scene, senders, artifacts):
    s = senders(A, size=(640, 360))
    scene.create_receiver("RX", A)
    assert scene.wait_source_size("RX", (640, 360), timeout=2.0) == (640, 360)
    s.stop()
    time.sleep(0.5)
    senders(A, size=(800, 450), seed=2)
    size = scene.wait_source_size("RX", (800, 450), timeout=6.0)
    artifacts.value("size_after_restart", size)
    assert size == (800, 450)
    img = scene.screenshot("RX")
    artifacts.image("rx-after-resize", img)
    assert_quadrants(img, 2)


def test_sender_exit_no_crash(scene, senders, obs_manager, artifacts):
    s = senders(A, size=(640, 360))
    scene.create_receiver("RX", A)
    scene.wait_source_size("RX", (640, 360), timeout=2.0)
    s.stop()
    time.sleep(1.5)
    assert obs_manager.current.alive(), "OBS died after the sender exited"
    assert scene.stats()["activeFps"] > 0
    text = obs_manager.log_text()
    assert "has changed / gone away" in text
    img = scene.screenshot("RX")
    artifacts.image("rx-after-sender-exit", img)
    artifacts.value("means", _means(img))
    chk = obslog.check(text)
    assert chk.ok, chk


def test_4k(scene, senders, artifacts):
    senders(A, size=(3840, 2160), fps=10, seed=3)
    scene.create_receiver("RX", A)
    size = scene.wait_source_size("RX", (3840, 2160), timeout=5.0)
    assert size == (3840, 2160)
    img = scene.screenshot("RX", width=960, height=540)
    artifacts.image("rx-4k-scaled", img)
    assert_quadrants(img, 3)
    assert pattern.decode_frame(img) is not None


def test_composite_modes_golden(scene, senders, artifacts, golden):
    """Modes 1-4 over a magenta background, compared with tests/golden/composite-modes.json."""
    senders(A, size=(640, 360), seed=0, alpha="straight")
    scene.create_color_source("BG", (255, 0, 255), width=1280, height=720)
    scene.create_receiver("RX", A, composite_mode=1)
    scene.set_item_index("BG", 0)
    scene.wait_source_size("RX", (640, 360), timeout=2.0)
    actual = {}
    for mode in (1, 2, 3, 4):
        scene.set_input_settings("RX", {"compositemode": mode})
        time.sleep(0.4)
        crop = scene.screenshot()[0:360, 0:640]
        artifacts.image(f"composite-{mode}", crop)
        means = pattern.region_means(crop)
        for region in ("tl", "br", "centre"):
            actual[f"mode{mode}.{region}"] = tuple(round(float(v), 1) for v in means[region][:3])
    artifacts.value("composite", actual)
    golden.compare_or_create("composite-modes", actual, tol=COLOUR_TOL, artifacts=artifacts)
