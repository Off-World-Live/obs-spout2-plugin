"""Colour format / space / range matrix (plan 4.6): one OBS restart per profile.

Program (1280x720): black | 50 % grey | white bars across the top half (colour sources) and a
Spout2 Capture of the test pattern (red/green/blue/grey quadrants) bottom-left. The Tools output
is received outside OBS and sampled:

* ``test_output_levels``      - the bars must come back as 0 / 128 / 255 (+-3) in every profile
                                (NV12 partial vs full vs BGRA catches #84)
* ``test_receiver_brightness`` - the receiver's grey quadrant must be 128 (+-8) through the
                                output in every profile (catches #75 on 10-bit canvases)

Known failures are strict xfails so a fix flips them to XPASS and the marker gets removed.
"""
from typing import Dict

import pytest

from harness import paths, pattern, spout_tool

A = "OBSTEST_A"
OUT = paths.OUTPUT_SENDER
BAR_W, BAR_H = 426, 360
BARS = {"black": ((0, 0, 0), 0), "grey": ((128, 128, 128), 426), "white": ((255, 255, 255), 852)}

_cache: Dict[str, dict] = {}


def xf(profile: str, reason: str):
    return pytest.param(profile, marks=pytest.mark.xfail(strict=True, reason=reason))


def measure(obs_manager, senders, run_dir, profile: str) -> dict:
    if profile in _cache:
        return _cache[profile]
    client = obs_manager.get(profile)
    client.clear()
    for name, (rgb, x) in BARS.items():
        client.create_color_source(name.upper(), rgb, width=BAR_W, height=BAR_H)
        client.set_transform(name.upper(), positionX=float(x), positionY=0.0)
    senders(A, size=(640, 360), seed=0)
    client.create_receiver("RX", A)
    client.set_transform("RX", positionX=0.0, positionY=360.0)
    assert client.wait_source_size("RX", (640, 360), timeout=3.0) == (640, 360)
    client.output_set_sender(OUT)
    client.output_start()
    assert client.wait(client.output_active, timeout=3.0)
    assert spout_tool.wait_for_sender(OUT, timeout=5.0)
    r = spout_tool.receive(OUT, frames=3, timeout=4.0, out=run_dir / f"tmp-40-{profile}.png")
    program = client.screenshot()
    client.output_stop()
    client.clear()
    img = r.pop("image")
    assert r["connected"] and img is not None, f"no output frame received in profile {profile}: {r}"
    bars = {}
    for name, (_, x) in BARS.items():
        bars[name] = pattern.mean_rgba(img, pattern.inner((x, 0, x + BAR_W, BAR_H)))[:3]
    rx = {k: v[:3] for k, v in pattern.region_means(img[360:720, 0:640]).items()}
    result = {"bars": bars, "rx": rx, "output": img, "program": program, "video": client.video_settings(), "profile": paths.PROFILES[profile]}
    _cache[profile] = result
    return result


def _attach(artifacts, m, profile):
    artifacts.value("profile", m["profile"])
    artifacts.value("bars", {k: pattern.fmt_rgb(v) for k, v in m["bars"].items()})
    artifacts.value("rx", {k: pattern.fmt_rgb(v) for k, v in m["rx"].items()})
    artifacts.image(f"output-{profile}", m["output"])
    artifacts.image(f"program-{profile}", m["program"])


OUTPUT_LEVEL_PARAMS = [
    "bgra",
    "nv12-partial",
    "nv12-full",
    "i444",
    "p010-709",
    "p010-2100pq",
    "i010-709",
]

RECEIVER_PARAMS = [
    "bgra",
    "nv12-partial",
    "nv12-full",
    "i444",
    "p010-709",
    "p010-2100pq",
    "i010-709",
]


@pytest.mark.parametrize("profile", OUTPUT_LEVEL_PARAMS)
def test_output_levels(obs_manager, senders, run_dir, artifacts, profile):
    m = measure(obs_manager, senders, run_dir, profile)
    _attach(artifacts, m, profile)
    bad = []
    for name, (expected, _) in BARS.items():
        if not pattern.colour_close(m["bars"][name], expected, 3):
            bad.append(f"{name}: {pattern.fmt_rgb(m['bars'][name])} != {expected} (+-3)")
    assert not bad, f"[{profile}] output levels: " + "; ".join(bad)


@pytest.mark.parametrize("profile", RECEIVER_PARAMS)
def test_receiver_brightness(obs_manager, senders, run_dir, artifacts, profile):
    m = measure(obs_manager, senders, run_dir, profile)
    _attach(artifacts, m, profile)
    grey = m["rx"]["br"]
    assert pattern.colour_close(grey, (128, 128, 128), 8), f"[{profile}] receiver grey through the output: {pattern.fmt_rgb(grey)} (expected 128 +-8)"
    for name in ("tl", "tr", "bl"):
        expected = pattern.expected_quadrants(0)[name]
        assert pattern.colour_close(m["rx"][name], expected, 12), f"[{profile}] receiver {name}: {pattern.fmt_rgb(m['rx'][name])} != {expected}"
