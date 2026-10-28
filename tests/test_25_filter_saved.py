"""A Spout Filter loaded from a saved scene collection must send without opening its properties (#36).

``spout-filter-saved`` holds an orange 640x360 colour source in scene ``Test`` with a Spout Filter
named ``OBSTEST_Saved``. It is loaded the two ways #36 describes: switching to the collection in a
running OBS (scene collection import) and launching OBS straight into it (machine restart).
"""
from harness import paths, pattern, spout_tool

SAVED = paths.SAVED_FILTER_SENDER
ORANGE = (255, 128, 0)


def _assert_sending(artifacts, run_dir, label):
    assert spout_tool.wait_for_sender(SAVED, timeout=8.0), f"[{label}] saved filter never registered its sender"
    r = spout_tool.receive(SAVED, frames=2, timeout=3.0, out=run_dir / f"tmp-25-{label}.png")
    img = r.pop("image")
    artifacts.value(f"recv-{label}", {k: v for k, v in r.items() if k != "stderr"})
    assert r["connected"] and img is not None, f"[{label}] no frame from the saved filter: {r}"
    assert (r["width"], r["height"]) == (640, 360)
    artifacts.image(f"saved-filter-{label}", img)
    h, w = img.shape[:2]
    mean = pattern.mean_rgba(img, (0, 0, w, h))[:3]
    assert pattern.colour_close(mean, ORANGE, 6), f"[{label}] saved filter sent {pattern.fmt_rgb(mean)}, expected orange"


def test_saved_filter_sends_after_collection_switch(scene, artifacts, run_dir):
    assert not spout_tool.wait_for_sender(SAVED, timeout=0.5), "stale OBSTEST_Saved sender before the switch"
    scene.call("SetCurrentSceneCollection", {"sceneCollectionName": paths.SAVED_FILTER_COLLECTION})
    try:
        assert scene.wait(lambda: scene.call("GetSceneCollectionList")["currentSceneCollectionName"] == paths.SAVED_FILTER_COLLECTION,
                          timeout=10.0), "OBS did not switch scene collection"
        _assert_sending(artifacts, run_dir, "switch")
    finally:
        scene.call("SetCurrentSceneCollection", {"sceneCollectionName": paths.SCENE_COLLECTION})
        scene.wait(lambda: scene.call("GetSceneCollectionList")["currentSceneCollectionName"] == paths.SCENE_COLLECTION,
                   timeout=10.0)
    assert spout_tool.wait_for_sender(SAVED, timeout=5.0, present=False), "saved filter's sender outlived its scene collection"


def test_saved_filter_sends_at_launch(obs_manager, artifacts, run_dir):
    obs_manager.get(paths.DEFAULT_PROFILE, fresh=True, collection=paths.SAVED_FILTER_COLLECTION)
    try:
        _assert_sending(artifacts, run_dir, "launch")
    finally:
        # back to the default collection for the next test
        obs_manager.stop()
