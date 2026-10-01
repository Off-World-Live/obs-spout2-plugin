"""Synthetic test pattern shared by the harness, harness/spoutgl_tool.py and tests/spout-tool/main.cpp.

Layout of a w x h RGBA8 image (top-down rows):

    +---------------------+---------------------+
    |  tl                 |  tr                 |
    |            +--------+--------+            |
    |            | centre (red, a) |            |
    +------------+--------+--------+------------+
    |  bl        |                 |  br (grey) |
    |            +-----------------+            |
    +---------------------+---------------------+
    | 16 B/W cells: frame number, bit i -> cell i (LSB left)     |
    +------------------------------------------------------------+

* Quadrant colours come from a six-entry hue table rotated by ``seed`` so two simultaneous
  senders are distinguishable; the bottom-right quadrant is always 50 % grey (128).
* The centre square is red; ``alpha_mode`` selects opaque / straight (255,0,0,128) /
  premultiplied (128,0,0,128).
* The strip survives scaling because every cell is w/16 wide and h/12 tall and we sample the
  inner 60 % of each cell.

The C++ mirror in tests/spout-tool/main.cpp uses the same integer arithmetic; keep them in sync
and cover both with tests/unit/test_pattern.py.
"""
from __future__ import annotations

from typing import Dict, Tuple

import numpy as np

STRIP_CELLS = 16
FRAME_BITS = 16
CENTRE_RGB = (255, 0, 0)
CENTRE_ALPHA = 128
GREY = (128, 128, 128)

# Hues 0,60,...,300 deg at full saturation/value: exact 0/255 components.
HUE_TABLE = [
    (255, 0, 0),  # red
    (255, 255, 0),  # yellow
    (0, 255, 0),  # green
    (0, 255, 255),  # cyan
    (0, 0, 255),  # blue
    (255, 0, 255),  # magenta
]

ALPHA_MODES = ("opaque", "straight", "premult")

Box = Tuple[int, int, int, int]  # x0, y0, x1, y1 (exclusive)
RGB = Tuple[int, int, int]


def strip_height(h: int) -> int:
    return max(8, h // 12)


def regions(w: int, h: int) -> Dict[str, Box]:
    sh = strip_height(h)
    body = h - sh
    half_w = w // 2
    half_h = body // 2
    s = max(8, min(w, body) // 6)
    cx0 = w // 2 - s // 2
    cy0 = body // 2 - s // 2
    return {
        "tl": (0, 0, half_w, half_h),
        "tr": (half_w, 0, w, half_h),
        "bl": (0, half_h, half_w, body),
        "br": (half_w, half_h, w, body),
        "centre": (cx0, cy0, cx0 + s, cy0 + s),
        "strip": (0, body, w, h),
    }


def quadrant_colors(seed: int = 0) -> Tuple[RGB, RGB, RGB, RGB]:
    """(tl, tr, bl, br) for a seed; seed 0 = red/green/blue/grey."""
    seed = int(seed) % len(HUE_TABLE)
    tl = HUE_TABLE[(0 + seed) % 6]
    tr = HUE_TABLE[(2 + seed) % 6]
    bl = HUE_TABLE[(4 + seed) % 6]
    return tl, tr, bl, GREY


def expected_quadrants(seed: int = 0) -> Dict[str, RGB]:
    tl, tr, bl, br = quadrant_colors(seed)
    return {"tl": tl, "tr": tr, "bl": bl, "br": br}


def cell_box(w: int, h: int, i: int) -> Box:
    _, y0, _, y1 = regions(w, h)["strip"]
    return (i * w // STRIP_CELLS, y0, (i + 1) * w // STRIP_CELLS, y1)


def paint_strip(img: np.ndarray, frame: int) -> None:
    """Write the frame counter strip in place (img is h x w x 4 RGBA)."""
    h, w = img.shape[:2]
    frame &= (1 << FRAME_BITS) - 1
    for i in range(STRIP_CELLS):
        x0, y0, x1, y1 = cell_box(w, h, i)
        v = 255 if (frame >> i) & 1 else 0
        img[y0:y1, x0:x1, 0:3] = v
        img[y0:y1, x0:x1, 3] = 255


def make_pattern(w: int, h: int, frame: int = 0, seed: int = 0, alpha_mode: str = "opaque") -> np.ndarray:
    if alpha_mode not in ALPHA_MODES:
        raise ValueError(f"alpha_mode must be one of {ALPHA_MODES}")
    img = np.zeros((h, w, 4), dtype=np.uint8)
    img[..., 3] = 255
    reg = regions(w, h)
    for name, colour in zip(("tl", "tr", "bl", "br"), quadrant_colors(seed)):
        x0, y0, x1, y1 = reg[name]
        img[y0:y1, x0:x1, 0:3] = colour
    x0, y0, x1, y1 = reg["centre"]
    if alpha_mode == "opaque":
        img[y0:y1, x0:x1] = (*CENTRE_RGB, 255)
    elif alpha_mode == "straight":
        img[y0:y1, x0:x1] = (*CENTRE_RGB, CENTRE_ALPHA)
    else:  # premultiplied
        img[y0:y1, x0:x1] = (CENTRE_RGB[0] * CENTRE_ALPHA // 255, 0, 0, CENTRE_ALPHA)
    paint_strip(img, frame)
    return img


def inner(box: Box, frac: float = 0.6) -> Box:
    x0, y0, x1, y1 = box
    mx = int((x1 - x0) * (1 - frac) / 2)
    my = int((y1 - y0) * (1 - frac) / 2)
    return (x0 + mx, y0 + my, max(x0 + mx + 1, x1 - mx), max(y0 + my + 1, y1 - my))


def mean_rgba(img: np.ndarray, box: Box) -> Tuple[float, ...]:
    x0, y0, x1, y1 = box
    patch = img[y0:y1, x0:x1].astype(np.float64)
    if patch.size == 0:
        return tuple([float("nan")] * img.shape[2])
    return tuple(float(v) for v in patch.reshape(-1, img.shape[2]).mean(axis=0))


def region_means(img: np.ndarray, names=("tl", "tr", "bl", "br", "centre")) -> Dict[str, Tuple[float, ...]]:
    """Mean colour of the inner 60 % of each region (works on any image size)."""
    h, w = img.shape[:2]
    reg = regions(w, h)
    return {n: mean_rgba(img, inner(reg[n])) for n in names}


def decode_frame(img: np.ndarray, margin: int = 80) -> int | None:
    """Decode the frame counter strip. Returns None if any cell is not clearly black or white."""
    h, w = img.shape[:2]
    frame = 0
    for i in range(STRIP_CELLS):
        r, g, b = mean_rgba(img, inner(cell_box(w, h, i)))[:3]
        lum = (r + g + b) / 3.0
        if lum > 255 - margin:
            frame |= 1 << i
        elif lum >= margin:
            return None
    return frame


def colour_close(actual, expected, tol: float) -> bool:
    return all(abs(float(a) - float(e)) <= tol for a, e in zip(actual[:3], expected[:3]))


def rgba_to_bgra(img: np.ndarray) -> np.ndarray:
    return img[..., [2, 1, 0, 3]]


def bgra_to_rgba(img: np.ndarray) -> np.ndarray:
    return img[..., [2, 1, 0, 3]]


def from_raw(data: bytes, w: int, h: int, order: str = "rgba") -> np.ndarray:
    arr = np.frombuffer(data, dtype=np.uint8)
    if arr.size != w * h * 4:
        raise ValueError(f"raw buffer is {arr.size} bytes, expected {w * h * 4} for {w}x{h}")
    arr = arr.reshape(h, w, 4).copy()
    return bgra_to_rgba(arr) if order == "bgra" else arr


def to_pil(img: np.ndarray):
    from PIL import Image

    return Image.fromarray(np.ascontiguousarray(img), "RGBA")


def from_pil(pil) -> np.ndarray:
    return np.asarray(pil.convert("RGBA")).copy()


def fmt_rgb(v) -> str:
    return "(" + ", ".join(f"{float(c):.1f}" for c in v[:3]) + ")"
