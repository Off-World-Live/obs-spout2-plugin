"""Per-executable GPU placement on dual-GPU boxes via

    HKCU\\Software\\Microsoft\\DirectX\\UserGpuPreferences
        <full exe path> = "GpuPreference=1;"   (1 = power saving / iGPU, 2 = high performance)

This is what Windows Settings > Display > Graphics writes. It applies to new processes only, so
set it before launching the sender (or OBS) and always remove it afterwards.
"""
from __future__ import annotations

import contextlib
import sys
from pathlib import Path
from typing import Iterator, List

KEY_PATH = r"Software\Microsoft\DirectX\UserGpuPreferences"
POWER_SAVING = 1
HIGH_PERFORMANCE = 2


def _winreg():
    if sys.platform != "win32":
        raise RuntimeError("UserGpuPreferences only exists on Windows")
    import winreg

    return winreg


def get_preference(exe_path: Path | str) -> str | None:
    winreg = _winreg()
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, KEY_PATH) as key:
            value, _ = winreg.QueryValueEx(key, str(exe_path))
            return value
    except FileNotFoundError:
        return None


def set_preference(exe_path: Path | str, preference: int | None) -> None:
    winreg = _winreg()
    with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, KEY_PATH, 0, winreg.KEY_SET_VALUE) as key:
        if preference is None:
            with contextlib.suppress(FileNotFoundError):
                winreg.DeleteValue(key, str(exe_path))
        else:
            winreg.SetValueEx(key, str(exe_path), 0, winreg.REG_SZ, f"GpuPreference={int(preference)};")


@contextlib.contextmanager
def gpu_preference(exe_path: Path | str, preference: int) -> Iterator[None]:
    """Temporarily pin ``exe_path`` to a GPU class; restores the previous value on exit."""
    exe_path = str(Path(exe_path))
    previous = get_preference(exe_path)
    set_preference(exe_path, preference)
    try:
        yield
    finally:
        winreg = _winreg()
        with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, KEY_PATH, 0, winreg.KEY_SET_VALUE) as key:
            if previous is None:
                with contextlib.suppress(FileNotFoundError):
                    winreg.DeleteValue(key, exe_path)
            else:
                winreg.SetValueEx(key, exe_path, 0, winreg.REG_SZ, previous)


def adapter_names() -> List[str]:
    """Display adapters as Windows reports them (WMI); used to decide whether a dual-GPU test can run."""
    import json
    import subprocess

    cmd = [
        "powershell",
        "-NoProfile",
        "-Command",
        "Get-CimInstance Win32_VideoController | Select-Object -ExpandProperty Name | ConvertTo-Json",
    ]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=20).stdout.strip()
    except Exception:
        return []
    if not out:
        return []
    data = json.loads(out)
    return [data] if isinstance(data, str) else list(data)
