"""Pure-Python tests of the pattern codec (no OBS, no GPU)."""
import numpy as np
import pytest

from harness import pattern


@pytest.mark.parametrize("size", [(640, 360), (1280, 720), (3840, 2160), (100, 100), (720, 1280)])
@pytest.mark.parametrize("frame", [0, 1, 0xFFFF, 12345, 0b1010101010101010])
def test_frame_round_trip(size, frame):
    img = pattern.make_pattern(size[0], size[1], frame)
    assert pattern.decode_frame(img) == (frame & 0xFFFF)


def test_frame_survives_scaling():
    from PIL import Image

    img = pattern.make_pattern(1280, 720, 0xBEEF)
    small = pattern.from_pil(pattern.to_pil(img).resize((320, 180), Image.BILINEAR))
    assert pattern.decode_frame(small) == 0xBEEF


def test_quadrant_colours_per_seed():
    for seed in range(6):
        img = pattern.make_pattern(640, 360, 7, seed=seed)
        means = pattern.region_means(img)
        for name, expected in pattern.expected_quadrants(seed).items():
            assert tuple(round(v) for v in means[name][:3]) == expected, (seed, name)
    assert pattern.expected_quadrants(0)["tl"] == (255, 0, 0)
    assert pattern.expected_quadrants(1)["tl"] == (255, 255, 0)
    assert pattern.expected_quadrants(0) != pattern.expected_quadrants(1)


@pytest.mark.parametrize("mode,expected", [("opaque", (255, 0, 0, 255)), ("straight", (255, 0, 0, 128)), ("premult", (128, 0, 0, 128))])
def test_centre_alpha_modes(mode, expected):
    img = pattern.make_pattern(640, 360, 0, alpha_mode=mode)
    assert tuple(round(v) for v in pattern.region_means(img)["centre"]) == expected


def test_regions_do_not_overlap_centre():
    for w, h in [(640, 360), (640, 640), (1280, 720), (100, 100)]:
        reg = pattern.regions(w, h)
        cx0, cy0, cx1, cy1 = reg["centre"]
        for q in ("tl", "tr", "bl", "br"):
            x0, y0, x1, y1 = pattern.inner(reg[q])
            overlap = x0 < cx1 and cx0 < x1 and y0 < cy1 and cy0 < y1
            assert not overlap, (w, h, q)
        assert reg["strip"][1] >= cy1


def test_decode_rejects_garbage():
    img = np.full((360, 640, 4), 128, dtype=np.uint8)
    assert pattern.decode_frame(img) is None


def test_raw_round_trip():
    img = pattern.make_pattern(64, 32, 5, seed=2, alpha_mode="straight")
    raw = pattern.rgba_to_bgra(img).tobytes()
    back = pattern.from_raw(raw, 64, 32, "bgra")
    assert np.array_equal(back, img)
