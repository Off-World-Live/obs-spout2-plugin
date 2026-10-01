"""Tools > Spout Output (`OBS Spout Output` obs_output) assertions (plan 4.6)."""
import time

import pytest

from harness import obslog, paths, pattern, spout_tool

OUT = paths.OUTPUT_SENDER
A = "OBSTEST_A"


def _start(scene):
    scene.output_set_sender(OUT)
    scene.output_start()
    assert scene.wait(scene.output_active, timeout=3.0), "output did not become active"
    assert spout_tool.wait_for_sender(OUT, timeout=5.0), "output sender not registered"


def _flat(img):
    """Mean RGB over the whole image (for flat colour programs)."""
    h, w = img.shape[:2]
    return pattern.mean_rgba(img, (0, 0, w, h))[:3]


def test_output_start_stop(scene, artifacts, obs_manager, run_dir):
    scene.create_color_source("GREY", (128, 128, 128), width=1280, height=720)
    _start(scene)
    artifacts.value("status", scene.output_status())
    r = spout_tool.receive(OUT, frames=2, timeout=3.0, out=run_dir / "tmp-output.png")
    img = r.pop("image")
    vs = scene.video_settings()
    artifacts.value("recv", {k: v for k, v in r.items() if k != "stderr"})
    assert r["connected"]
    assert (r["width"], r["height"]) == (vs["outputWidth"], vs["outputHeight"])
    artifacts.image("output-grey", img)
    grey = _flat(img)
    artifacts.value("grey", pattern.fmt_rgb(grey))
    assert pattern.colour_close(grey, (128, 128, 128), 3), f"50% grey came out as {pattern.fmt_rgb(grey)}"
    assert f"Creating capture with name: {OUT}, width: {vs['outputWidth']}, height: {vs['outputHeight']}" in obs_manager.log_text()
    scene.output_stop()
    assert scene.wait(lambda: not scene.output_active(), timeout=3.0)
    assert spout_tool.wait_for_sender(OUT, timeout=5.0, present=False), "output sender still registered after StopOutput"


def test_output_follows_colour_change(scene, artifacts, run_dir):
    scene.create_color_source("C", (128, 128, 128), width=1280, height=720)
    _start(scene)
    spout_tool.receive(OUT, frames=1, timeout=3.0)
    t0 = time.monotonic()
    scene.set_input_settings("C", {"color": 0xFFFF0000})  # ABGR -> pure blue
    seen = None
    while time.monotonic() - t0 < 2.0:
        r = spout_tool.receive(OUT, frames=1, timeout=0.5, out=run_dir / "tmp-output-colour.png")
        if r.get("image") is not None and pattern.colour_close(_flat(r["image"]), (0, 0, 255), 6):
            seen = time.monotonic() - t0
            artifacts.image("output-blue", r["image"])
            break
    artifacts.value("seen_after_s", None if seen is None else round(seen, 3))
    assert seen is not None, "colour change never reached the Spout output"
    assert seen < 1.0, f"colour change took {seen:.2f}s to reach the output (expected < 0.5 s)"


def test_output_pattern_passthrough(scene, senders, artifacts, run_dir):
    senders(A, size=(640, 360), seed=0)
    scene.create_receiver("RX", A)
    scene.wait_source_size("RX", (640, 360), timeout=2.0)
    _start(scene)
    r = spout_tool.receive(OUT, frames=6, timeout=3.0, out=run_dir / "tmp-output-pattern.png")
    img = r.pop("image")
    assert img is not None
    crop = img[0:360, 0:640]
    artifacts.image("output-pattern", img)
    means = pattern.region_means(crop)
    for name, expected in pattern.expected_quadrants(0).items():
        assert pattern.colour_close(means[name], expected, 6), f"{name}: {pattern.fmt_rgb(means[name])} != {expected}"
    f1 = pattern.decode_frame(crop)
    time.sleep(0.3)
    r2 = spout_tool.receive(OUT, frames=1, timeout=2.0, out=run_dir / "tmp-output-pattern2.png")
    f2 = pattern.decode_frame(r2["image"][0:360, 0:640]) if r2.get("image") is not None else None
    artifacts.value("frames", (f1, f2))
    assert f1 is not None and f2 is not None and f1 != f2, "pattern not moving through the output"


def test_output_start_stop_cycles(scene, artifacts, obs_manager):
    scene.create_color_source("GREY", (128, 128, 128), width=1280, height=720)
    before = obslog.count_warnings(obs_manager.log_text())
    for i in range(10):
        _start(scene)
        scene.output_stop()
        assert scene.wait(lambda: not scene.output_active(), timeout=3.0), f"cycle {i}: output did not stop"
    assert spout_tool.wait_for_sender(OUT, timeout=5.0, present=False)
    after = obslog.count_warnings(obs_manager.log_text())
    artifacts.value("warnings_errors_delta", (after[0] - before[0], after[1] - before[1]))
    chk = obslog.check(obs_manager.log_text())
    assert chk.ok, chk
    assert after[1] == before[1], "errors were logged during start/stop cycles"


@pytest.mark.xfail(strict=True, reason="#80/#92: since 1.12.0 AutoStart only fires when the Tools dialog is opened, not at OBS start")
def test_autostart_variant_starts_output_at_launch(obs_manager, artifacts):
    client = obs_manager.get(paths.DEFAULT_PROFILE, "autostart")
    t0 = time.monotonic()
    appeared = spout_tool.wait_for_sender(OUT, timeout=8.0)
    artifacts.value("appeared_after_s", round(time.monotonic() - t0, 2))
    artifacts.value("output_active", client.output_active())
    assert appeared, "auto_start=true but the output sender never appeared within 8 s of launch"
    assert client.output_active()


@pytest.mark.xfail(strict=True, reason="#66: OBS refuses obs_reset_video while the raw output is active, so the sender keeps the old size")
def test_profile_switch_while_running_follows_size(scene, obs_manager, artifacts):
    scene.create_color_source("GREY", (128, 128, 128), width=1280, height=720)
    _start(scene)
    try:
        scene.set_profile("portrait")
        deadline = time.monotonic() + 6.0
        size = None
        while time.monotonic() < deadline:
            r = spout_tool.receive(OUT, frames=1, timeout=1.0)
            size = (r["width"], r["height"])
            if size == (720, 1280):
                break
            time.sleep(0.3)
        artifacts.value("sender_size_after_switch", size)
        artifacts.value("video_settings", scene.video_settings())
        assert obs_manager.current.alive()
        assert size == (720, 1280), f"sender size after switching to the 720x1280 profile: {size}"
    finally:
        # the output was active during the switch; give the next test a fresh OBS
        obs_manager.stop()
