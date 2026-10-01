"""Well-known locations and constants shared by run.ps1, the fixtures and the tests.

Everything here can be overridden with environment variables so the harness can run on
another box (or a CI runner) without edits.
"""
from __future__ import annotations

import os
import sys
import sysconfig
from pathlib import Path

TESTS_DIR = Path(__file__).resolve().parent.parent
REPO_ROOT = TESTS_DIR.parent

# Python: the venv launcher spawns the base interpreter as a child, so subprocesses and the
# GPU-preference registry entry must use the *real* executable; the venv's site-packages is
# handed to those subprocesses through PYTHONPATH (works from any worktree).
VENV_PYTHON = TESTS_DIR / ".venv" / "Scripts" / "python.exe"
REAL_PYTHON = Path(getattr(sys, "_base_executable", sys.executable))
VENV_SITE_PACKAGES = Path(sysconfig.get_paths()["purelib"])

# Portable OBS copy created by setup-user.ps1 -MakePortable (never the user's own OBS).
PORTABLE_OBS = Path(os.environ.get("OBS_TEST_PORTABLE", r"C:\AgenticWork\obs-test\obs-studio"))
OBS_EXE = PORTABLE_OBS / "bin" / "64bit" / "obs64.exe"
OBS_BIN_DIR = OBS_EXE.parent
OBS_CONFIG_DIR = PORTABLE_OBS / "config" / "obs-studio"
OBS_LOG_DIR = OBS_CONFIG_DIR / "logs"
OBS_CRASH_DIR = OBS_CONFIG_DIR / "crashes"
OBS_SENTINEL = OBS_CONFIG_DIR / ".sentinel"

# Installed plugin (NSIS layout, loaded by both the user's OBS and the portable copy).
PLUGIN_INSTALL_DIR = Path(
    os.environ.get("OBS_SPOUT_PLUGIN_DIR", r"C:\ProgramData\obs-studio\plugins\win-spout")
)
PLUGIN_DLL = PLUGIN_INSTALL_DIR / "bin" / "64bit" / "win-spout.dll"
BUILD_CONFIGURATION = os.environ.get("OBS_SPOUT_BUILD_CONFIG", "RelWithDebInfo")
BUILD_OUTPUT_DIR = REPO_ROOT / "release" / BUILD_CONFIGURATION / "win-spout"
BUILD_OUTPUT_DLL = BUILD_OUTPUT_DIR / "bin" / "64bit" / "win-spout.dll"

STAGE_DIR = TESTS_DIR / ".stage"
INSTALLED_JSON = STAGE_DIR / "installed.json"
RESULTS_ROOT = TESTS_DIR / "results"
GOLDEN_DIR = TESTS_DIR / "golden"
FIXTURES_DIR = TESTS_DIR / "fixtures"
PORTABLE_FIXTURES = FIXTURES_DIR / "portable"
VARIANTS_DIR = FIXTURES_DIR / "user-ini-variants"

# Native sender/receiver tool (preferred when built); SpoutGL fallback otherwise.
SPOUT_TOOL_CANDIDATES = [
    TESTS_DIR / "spout-tool" / "build" / "Release" / "spout-tool.exe",
    TESTS_DIR / "spout-tool" / "build" / "RelWithDebInfo" / "spout-tool.exe",
    TESTS_DIR / "spout-tool" / "build" / "spout-tool.exe",
]

WS_HOST = "127.0.0.1"
WS_PORT = int(os.environ.get("OBS_TEST_WS_PORT", "4466"))
WS_PASSWORD = os.environ.get("OBS_TEST_WS_PASSWORD", "obs-spout-test")

SCENE_COLLECTION = "spout-test"
SCENE_NAME = "Test"
DEFAULT_PROFILE = "bgra"
DEFAULT_VARIANT = "default"

OUTPUT_NAME = "OBS Spout Output"  # obs_output created by win-spout.cpp
OUTPUT_SENDER = "OBSTEST_Output"  # [win_spout] spout_output_name in the seeded user.ini
FILTER_SENDER = "OBSTEST_Filter"
RECEIVER_KIND = "spout_capture"
FILTER_KIND = "win_spout_filter"

# Profiles seeded in fixtures/portable/config/obs-studio/basic/profiles/<name>/basic.ini
PROFILES = {
    "bgra": dict(format="RGB", space="sRGB", range="Full", size=(1280, 720)),
    "nv12-partial": dict(format="NV12", space="709", range="Partial", size=(1280, 720)),
    "nv12-full": dict(format="NV12", space="709", range="Full", size=(1280, 720)),
    "i444": dict(format="I444", space="709", range="Partial", size=(1280, 720)),
    "p010-709": dict(format="P010", space="709", range="Partial", size=(1280, 720)),
    "p010-2100pq": dict(format="P010", space="2100PQ", range="Partial", size=(1280, 720)),
    "i010-709": dict(format="I010", space="709", range="Partial", size=(1280, 720)),
    "portrait": dict(format="RGB", space="sRGB", range="Full", size=(720, 1280)),
}
COLOR_MATRIX_PROFILES = ["bgra", "nv12-partial", "nv12-full", "i444", "p010-709", "p010-2100pq", "i010-709"]


def spout_tool_exe() -> Path | None:
    for candidate in SPOUT_TOOL_CANDIDATES:
        if candidate.exists():
            return candidate
    return None
