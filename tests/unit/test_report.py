"""Report generation tests (no OBS needed)."""
import json

import numpy as np

from harness import report


def test_report_markdown_and_summary(tmp_path):
    rep = report.RunReport(tmp_path, {"plugin_sha256": "abc"})
    art = report.Artifacts(tmp_path, 1, "test_demo")
    img = np.zeros((720, 1280, 4), dtype=np.uint8)
    img[..., 3] = 255
    small = art.image("program", img)
    art.value("grey", (128.0, 128.2, 127.9))
    art.note("a note")
    rep.add(report.TestResult("test_10.py::test_demo", "failed", 1.5, "assert 1 == 2", "", art, ["12:00:00.000: [win_spout] hello"], "bgra", "default"))
    rep.add(report.TestResult("test_10.py::test_ok", "passed", 0.2))
    rep.add(report.TestResult("test_40.py::test_known", "xfailed", 0.3, "", "#75"))
    rep.add(report.TestResult("test_40.py::test_fixed", "xpassed", 0.3, "", "#84"))
    rep.add_log(None, "bgra", "default", True, ["note"], None)
    path = rep.write()

    assert path == tmp_path / "report.md"
    assert small.exists() and (tmp_path / "01-test_demo-program.full.png").exists()
    from PIL import Image

    assert Image.open(small).width == 640
    assert Image.open(tmp_path / "01-test_demo-program.full.png").width == 1280

    md = path.read_text(encoding="utf-8")
    assert md.index("## Failures") < md.index("## Unexpected passes") < md.index("## Expected failures") < md.index("## Passed")
    assert "assert 1 == 2" in md and "[win_spout] hello" in md and "01-test_demo-program.png" in md
    assert "drop the xfail marker" in md

    summary = json.loads((tmp_path / "summary.json").read_text())
    assert summary["counts"] == {"failed": 1, "passed": 1, "xfailed": 1, "xpassed": 1}
    assert summary["tests"][0]["values"]["grey"] == [128.0, 128.2, 127.9]
    assert summary["meta"]["plugin_sha256"] == "abc"
