"""Dual-GPU: a sender on the other adapter (plan 4.6, issue #73 / #65).

With the native spout-tool the sender is pinned with ``--adapter <substring>``; with the SpoutGL
fallback the Python interpreter is pinned through HKCU UserGpuPreferences (``GpuPreference=1``
= power saving, i.e. the iGPU). Placement is verified empirically: if the receiver renders the
pattern correctly the sender ended up on OBS's adapter and the tests skip with that reason.
"""
import re
import time

import pytest

from harness import gpu_prefs, paths, pattern, spout_tool

pytestmark = [pytest.mark.gpu, pytest.mark.dual_gpu]

NAME = "OBSTEST_iGPU"


def _obs_adapter(log_text: str) -> str:
    m = re.search(r"Adapter 0: (.+)", log_text)
    return m.group(1).strip() if m else ""


@pytest.fixture(scope="module")
def cross_gpu(obs_manager, run_dir):
    adapters = gpu_prefs.adapter_names()
    if len(set(adapters)) < 2:
        pytest.skip(f"needs two display adapters, found {adapters}")
    client = obs_manager.get(paths.DEFAULT_PROFILE)
    client.clear()
    obs_adapter = _obs_adapter(obs_manager.log_text())
    others = [a for a in adapters if a != obs_adapter]
    log_mark = len(obs_manager.log_text())
    info = {"adapters": adapters, "obs_adapter": obs_adapter, "backend": spout_tool.backend(), "placement": ""}
    if spout_tool.backend() == "spout-tool":
        target = others[0].split()[0] if others else "1"
        sender = spout_tool.start_sender(NAME, size=(640, 360), seed=4, adapter=target)
        info["placement"] = f"spout-tool --adapter {target} -> {sender.started.get('adapter')}"
    else:
        with gpu_prefs.gpu_preference(paths.REAL_PYTHON, gpu_prefs.POWER_SAVING):
            sender = spout_tool.start_sender(NAME, size=(640, 360), seed=4)
        info["placement"] = f"UserGpuPreferences[{paths.REAL_PYTHON}]=GpuPreference=1 (power saving)"
    try:
        client.create_receiver("RX", NAME)
        time.sleep(3.0)
        items = [i["itemValue"] for i in client.property_items("RX", "spoutsenders")]
        img = client.screenshot("RX")
        means = pattern.region_means(img)
        rendered = all(pattern.colour_close(means[n], e, 8) for n, e in pattern.expected_quadrants(4).items())
        black = all(max(means[n][:3]) < 12 for n in ("tl", "tr", "bl", "br"))
        new_log = obs_manager.log_text()[log_mark:]
        info.update(
            listed=NAME in items,
            rendered=rendered,
            black=black,
            size=client.source_size("RX"),
            alive=obs_manager.current.alive(),
            fps=client.stats()["activeFps"],
            means={k: pattern.fmt_rgb(v) for k, v in means.items()},
            image=img,
            log_lines=[l for l in new_log.splitlines() if "[win_spout]" in l or "shared" in l.lower() or "80070057" in l],
        )
        yield info
    finally:
        sender.stop()
        client.clear()


def _record(artifacts, info):
    for k in ("adapters", "obs_adapter", "backend", "placement", "listed", "rendered", "black", "size", "alive", "fps", "means"):
        artifacts.value(k, info.get(k))
    artifacts.image("rx-cross-gpu", info["image"])
    for line in info["log_lines"][-15:]:
        artifacts.note(line)
    if info["rendered"]:
        pytest.skip("GPU placement not achieved: the sender rendered correctly, so it ran on the same adapter as OBS "
                    "(UserGpuPreferences had no effect on this process); nothing to test")


def test_cross_gpu_sender_listed_and_no_crash(cross_gpu, artifacts):
    _record(artifacts, cross_gpu)
    assert cross_gpu["listed"], "sender on the other GPU is not listed in the dropdown"
    assert cross_gpu["alive"] and cross_gpu["fps"] > 0, "OBS stopped rendering after a cross-GPU sender"
    assert cross_gpu["black"], f"expected a black source for a cross-GPU sender, got {cross_gpu['means']}"


@pytest.mark.xfail(strict=True, reason="#73: receiver logs nothing actionable when gs_texture_open_shared fails for a sender on another GPU")
def test_cross_gpu_actionable_hint_logged(cross_gpu, artifacts):
    _record(artifacts, cross_gpu)
    hint = [l for l in cross_gpu["log_lines"] if "[win_spout]" in l and ("same GPU" in l or "different GPU" in l)]
    assert hint, "no [win_spout] hint about the sender running on a different GPU"
