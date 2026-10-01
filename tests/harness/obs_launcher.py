"""Launch / readiness / shutdown of the portable test OBS (plan section 4.3).

* always ``--portable --multi`` on port 4466, never the user's %APPDATA% OBS
* ``<config>\\.sentinel`` is deleted before every launch (OBS has no --disable-shutdown-check)
* user.ini, the scene collection and the profiles are re-seeded from tests/fixtures before every
  launch so a test cannot leak state into the next one; ``variant`` overlays
  fixtures/user-ini-variants/<variant>.ini on top of user.ini
* readiness: new log file -> TCP port -> websocket identify -> profile/scene -> plugin loaded in
  the log -> activeFps > 0; fails fast on a module load error
* stop: StopOutput, WM_CLOSE to the main window, 15 s grace, then kill (marked unclean); the
  OBS log is always copied into the results folder
"""
from __future__ import annotations

import configparser
import ctypes
import ctypes.wintypes as wt
import json
import shutil
import socket
import subprocess
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional

from harness import obslog, paths
from harness.obs_client import ObsClient

OBS_ARGS = [
    "--portable",
    "--multi",
    "--disable-updater",
    "--disable-missing-files-check",
    "--unfiltered_log",
    "--verbose",
]


class ObsLaunchError(RuntimeError):
    pass


def find_obs_processes() -> List[Dict[str, str]]:
    """All obs64.exe processes with their executable path (so we never kill the user's OBS)."""
    cmd = [
        "powershell",
        "-NoProfile",
        "-Command",
        "Get-CimInstance Win32_Process -Filter \"Name='obs64.exe'\" | Select-Object ProcessId,ExecutablePath | ConvertTo-Json -Compress",
    ]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=30).stdout.strip()
    except Exception:
        return []
    if not out:
        return []
    data = json.loads(out)
    if isinstance(data, dict):
        data = [data]
    return [{"pid": str(d.get("ProcessId")), "path": str(d.get("ExecutablePath") or "")} for d in data]


def kill_stale_test_obs() -> int:
    killed = 0
    for proc in find_obs_processes():
        if not proc["path"]:
            continue
        if Path(proc["path"]).resolve() == paths.OBS_EXE.resolve():
            subprocess.run(["taskkill", "/PID", proc["pid"], "/F"], capture_output=True)
            killed += 1
    return killed


def sync_plugin_into_portable() -> Dict[str, str]:
    """Mirror the installed plugin into the portable OBS.

    OBS in portable mode only loads plugins from its own ``obs-plugins/64bit`` (it skips the
    %ProgramData% plugin folder), so the DLLs that ``run.ps1 -Install`` put into
    PLUGIN_INSTALL_DIR are copied (only when their hash differs) into the traditional layout:
    ``<portable>/obs-plugins/64bit/*.dll`` and ``<portable>/data/obs-plugins/win-spout/locale``.
    Returns {file: sha256} of what is now in the portable copy.
    """
    import hashlib

    src_bin = paths.PLUGIN_INSTALL_DIR / "bin" / "64bit"
    src_data = paths.PLUGIN_INSTALL_DIR / "data"
    dst_bin = paths.PORTABLE_OBS / "obs-plugins" / "64bit"
    dst_data = paths.PORTABLE_OBS / "data" / "obs-plugins" / "win-spout"
    if not (src_bin / "win-spout.dll").exists():
        raise ObsLaunchError(f"plugin not installed: {src_bin / 'win-spout.dll'} missing")
    hashes: Dict[str, str] = {}
    for dll in sorted(src_bin.glob("*.dll")):
        dest = dst_bin / dll.name
        digest = hashlib.sha256(dll.read_bytes()).hexdigest()
        if not dest.exists() or hashlib.sha256(dest.read_bytes()).hexdigest() != digest:
            try:
                shutil.copy2(dll, dest)
            except PermissionError as exc:  # DLL still mapped by a running OBS
                raise ObsLaunchError(f"cannot update {dest} (is an OBS using it still running?): {exc}")
        hashes[dll.name] = digest
    if src_data.exists():
        shutil.copytree(src_data, dst_data, dirs_exist_ok=True)
    return hashes


def port_open(host: str, port: int, timeout: float = 0.5) -> bool:
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def _ini_read(path: Path) -> configparser.RawConfigParser:
    cp = configparser.RawConfigParser(strict=False, interpolation=None, allow_no_value=True)
    cp.optionxform = str  # OBS keys are case sensitive
    cp.read(path, encoding="utf-8-sig")
    return cp


def _ini_write(cp: configparser.RawConfigParser, path: Path) -> None:
    with path.open("w", encoding="utf-8") as fh:
        cp.write(fh, space_around_delimiters=False)


def seed_config(profile: str, variant: str = paths.DEFAULT_VARIANT) -> Path:
    """Re-seed user.ini (+ variant overlay), scene collection and profiles. Returns user.ini path."""
    cfg = paths.OBS_CONFIG_DIR
    src = paths.PORTABLE_FIXTURES / "config" / "obs-studio"
    cfg.mkdir(parents=True, exist_ok=True)

    # profiles + scene collection: copy fresh (OBS rewrites them on exit)
    for sub in ("basic/profiles", "basic/scenes", "plugin_config/obs-websocket"):
        s = src / sub
        d = cfg / sub
        if s.exists():
            d.mkdir(parents=True, exist_ok=True)
            shutil.copytree(s, d, dirs_exist_ok=True)

    # global.ini only when missing (setup-user.ps1 seeds LastVersion from the user's OBS)
    if not (cfg / "global.ini").exists():
        shutil.copy2(src / "global.ini", cfg / "global.ini")

    user = _ini_read(src / "user.ini")
    if variant and variant != paths.DEFAULT_VARIANT:
        overlay_path = paths.VARIANTS_DIR / f"{variant}.ini"
        if not overlay_path.exists():
            raise ObsLaunchError(f"unknown user.ini variant '{variant}' ({overlay_path} missing)")
        overlay = _ini_read(overlay_path)
        for section in overlay.sections():
            if not user.has_section(section):
                user.add_section(section)
            for key, value in overlay.items(section):
                user.set(section, key, value)
    if not user.has_section("Basic"):
        user.add_section("Basic")
    user.set("Basic", "Profile", profile)
    user.set("Basic", "ProfileDir", profile)
    user.set("Basic", "SceneCollection", paths.SCENE_COLLECTION)
    user.set("Basic", "SceneCollectionFile", f"{paths.SCENE_COLLECTION}.json")
    out = cfg / "user.ini"
    _ini_write(user, out)
    return out


# -- window close via WM_CLOSE -----------------------------------------------------------------
_user32 = ctypes.windll.user32 if hasattr(ctypes, "windll") else None
WM_CLOSE = 0x0010
_EnumWindowsProc = ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM) if _user32 else None


def top_level_windows(pid: int) -> List[tuple[int, str]]:
    found: List[tuple[int, str]] = []
    if not _user32:
        return found

    def cb(hwnd, _):
        owner = wt.DWORD()
        _user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and _user32.IsWindowVisible(hwnd):
            length = _user32.GetWindowTextLengthW(hwnd)
            buf = ctypes.create_unicode_buffer(length + 1)
            _user32.GetWindowTextW(hwnd, buf, length + 1)
            found.append((hwnd, buf.value))
        return True

    _user32.EnumWindows(_EnumWindowsProc(cb), 0)
    return found


def close_main_window(pid: int) -> bool:
    windows = top_level_windows(pid)
    main = [w for w in windows if w[1].startswith("OBS")] or windows
    for hwnd, _ in main:
        _user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)
    return bool(main)


@dataclass
class ObsProcess:
    profile: str
    variant: str
    run_dir: Path
    label: str = ""
    proc: Optional[subprocess.Popen] = field(default=None, repr=False)
    log_path: Optional[Path] = None
    client: Optional[ObsClient] = None
    clean_exit: Optional[bool] = None
    copied_log: Optional[Path] = None
    started_at: float = 0.0
    ready_seconds: float = 0.0
    notes: List[str] = field(default_factory=list)
    plugin_hashes: Dict[str, str] = field(default_factory=dict)
    _logs_before: List[Path] = field(default_factory=list, repr=False)

    # -- launch -----------------------------------------------------------------------------
    def launch(self) -> "ObsProcess":
        if not paths.OBS_EXE.exists():
            raise ObsLaunchError(
                f"portable OBS not found at {paths.OBS_EXE}; run  pwsh tests\\setup-user.ps1 -MakePortable"
            )
        if port_open(paths.WS_HOST, paths.WS_PORT):
            raise ObsLaunchError(f"port {paths.WS_PORT} already in use (stale test OBS?)")
        self.plugin_hashes = sync_plugin_into_portable()
        seed_config(self.profile, self.variant)
        paths.OBS_SENTINEL.unlink(missing_ok=True)
        paths.OBS_LOG_DIR.mkdir(parents=True, exist_ok=True)
        self._logs_before = list(paths.OBS_LOG_DIR.glob("*.txt"))
        args = [str(paths.OBS_EXE), *OBS_ARGS, "--collection", paths.SCENE_COLLECTION, "--profile", self.profile,
                "--scene", paths.SCENE_NAME, "--websocket_port", str(paths.WS_PORT), "--websocket_password", paths.WS_PASSWORD]
        self.started_at = time.monotonic()
        self.proc = subprocess.Popen(args, cwd=str(paths.OBS_BIN_DIR), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return self

    @property
    def pid(self) -> int:
        return self.proc.pid if self.proc else 0

    def alive(self) -> bool:
        return self.proc is not None and self.proc.poll() is None

    def log_text(self) -> str:
        return obslog.read_log(self.log_path) if self.log_path else ""

    def _fail(self, msg: str) -> None:
        self.stop(reason="readiness failed")
        raise ObsLaunchError(f"{msg}\n--- log tail ---\n" + "\n".join(self.log_text().splitlines()[-40:]))

    # -- readiness --------------------------------------------------------------------------
    def wait_ready(self, timeout: float = 45.0) -> ObsClient:
        deadline = time.monotonic() + timeout

        # 1. new log file
        while self.log_path is None:
            if not self.alive():
                self._fail(f"obs64 exited during startup (rc={self.proc.returncode})")
            self.log_path = obslog.newest_log(paths.OBS_LOG_DIR, after=self._logs_before)
            if self.log_path is None:
                if time.monotonic() > deadline:
                    self._fail("no new OBS log file appeared")
                time.sleep(0.2)

        # 2. websocket port + identify
        client: Optional[ObsClient] = None
        while client is None:
            if not self.alive():
                self._fail(f"obs64 exited during startup (rc={self.proc.returncode})")
            fatal = obslog.fatal_startup_lines(self.log_text())
            if fatal:
                self._fail(f"plugin failed to load: {fatal}")
            if port_open(paths.WS_HOST, paths.WS_PORT):
                try:
                    client = ObsClient()
                except Exception as exc:  # identify can fail while the server is still coming up
                    if time.monotonic() > deadline:
                        self._fail(f"websocket identify failed: {exc}")
                    time.sleep(0.5)
                    continue
            else:
                if time.monotonic() > deadline:
                    self._fail(f"websocket port {paths.WS_PORT} never opened")
                time.sleep(0.25)
        self.client = client

        # 3. profile / scene
        current = client.current_profile()
        if current != self.profile:
            self._fail(f"OBS started with profile '{current}', expected '{self.profile}'")
        if client.current_scene() != paths.SCENE_NAME:
            if paths.SCENE_NAME in client.scenes():
                client.set_current_scene(paths.SCENE_NAME)
            else:
                self._fail(f"scene '{paths.SCENE_NAME}' missing from collection (scenes: {client.scenes()})")

        # 4. plugin loaded
        text = self.log_text()
        if "win-spout loaded!" not in text:
            self._fail("log lacks '[win_spout] win-spout loaded!'")
        if "win-spout.dll" not in obslog.loaded_modules(text):
            self._fail("win-spout.dll not in the 'Loaded Modules' list")

        # 5. rendering
        while True:
            fps = float(client.stats().get("activeFps") or 0)
            if fps > 0:
                break
            if time.monotonic() > deadline:
                self._fail("GetStats().activeFps stayed 0")
            time.sleep(0.25)
        self.ready_seconds = round(time.monotonic() - self.started_at, 2)
        return client

    # -- shutdown ---------------------------------------------------------------------------
    def stop(self, grace: float = 15.0, reason: str = "") -> Optional[Path]:
        if self.proc is None:
            return self.copied_log
        if self.alive():
            if self.client is not None:
                try:
                    if self.client.output_active():
                        self.client.output_stop()
                except Exception:
                    pass
                self.client.close()
                self.client = None
            close_main_window(self.pid)
            try:
                self.proc.wait(timeout=grace)
                self.clean_exit = self.proc.returncode == 0
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=10)
                self.clean_exit = False
                self.notes.append("OBS did not exit within the grace period; killed")
        elif self.clean_exit is None:
            self.clean_exit = self.proc.returncode == 0
            self.notes.append(f"OBS had already exited (rc={self.proc.returncode})")
        if reason:
            self.notes.append(reason)
        time.sleep(0.3)  # let the log flush
        self.copied_log = self._copy_log()
        return self.copied_log

    def _copy_log(self) -> Optional[Path]:
        if self.log_path is None or not self.log_path.exists():
            return None
        self.run_dir.mkdir(parents=True, exist_ok=True)
        base = f"obs-{self.profile}-{self.variant}" + (f"-{self.label}" if self.label else "")
        dest = self.run_dir / f"{base}.log"
        n = 1
        while dest.exists():
            n += 1
            dest = self.run_dir / f"{base}-{n}.log"
        shutil.copy2(self.log_path, dest)
        return dest

    def desktop_screenshot(self, dest: Path) -> Optional[Path]:
        """Whole-desktop grab (catches blocking dialogs); downscaled to <= 1280 px wide."""
        try:
            from PIL import ImageGrab

            img = ImageGrab.grab(all_screens=True)
            if img.width > 1280:
                img = img.resize((1280, int(img.height * 1280 / img.width)))
            dest.parent.mkdir(parents=True, exist_ok=True)
            img.save(dest)
            return dest
        except Exception:
            return None
