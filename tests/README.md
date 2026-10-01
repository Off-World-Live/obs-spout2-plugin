# OBS-in-the-loop test harness

The harness builds the plugin, installs it where OBS loads it from, launches a **separate portable
copy of OBS** (`C:\AgenticWork\obs-test\obs-studio`, never your `%APPDATA%\obs-studio`), drives it
over obs-websocket (port 4466, `--multi`, so your own OBS can stay open) and checks what the
receiver, the Spout filter and the Tools output actually do with a synthetic Spout sender/receiver
outside OBS (`tests/spout-tool`, or the `SpoutGL` fallback when the tool is not built). Every run
leaves `tests/results/<run>/report.md` with failures first, measured values, downscaled PNGs for
reading (`*.png`, <= 640 px wide) plus full-size copies for pixel checks (`*.full.png`), the OBS
log per profile/variant and `summary.json` / `junit.xml`. Known plugin bugs are encoded as
`xfail(strict=True, reason="#NN")` so a fix shows up as **XPASS** and the report tells you to drop
the marker. An agent iterating on the plugin runs the loop below, reads `report.md`, looks at the
PNGs it links, fixes, and re-runs with `-Filter` to keep the cycle short (an OBS restart costs
about 2-8 s; one instance is reused per profile/variant).

## One-time setup

```powershell
# elevated (UAC): VS 2022 Build Tools + Windows SDK 22621, CMake, NSIS, write access to the plugin dir
pwsh -File tests\setup-admin.ps1
# normal shell: Python 3.12 (user scope) + tests\.venv + a portable OBS copy with seeded config
pwsh -File tests\setup-user.ps1 -MakePortable
```

## The loop

```powershell
pwsh -File tests\run.ps1 -Build -Install -Test          # full loop (OBS must be closed for -Install)
pwsh -File tests\run.ps1 -Test                          # test whatever plugin is installed right now
pwsh -File tests\run.ps1 -Test -Filter "receiver"       # pytest -k expression
pwsh -File tests\run.ps1 -Test -Variant continuous      # default user.ini variant (fixtures/user-ini-variants)
pwsh -File tests\run.ps1 -Test -UpdateGolden            # regenerate tests/golden/*.json
pwsh -File tests\run.ps1 -Restore                       # put the backed-up (released) plugin back
```

Exit codes: `0` green (pass / xfail / skip), `1` test failures, `2` build failed, `3` OBS running
(install refused), `4` tooling missing (the message says which setup script to run).
`-Build` without a compiler exits 4 and tells you to run `tests\setup-admin.ps1` elevated; `-Install`
without a fresh `release\RelWithDebInfo\win-spout` says so and leaves the installed plugin in place.
`-Install` records the installed DLL's SHA-256 in `tests\.stage\installed.json`; `test_00` fails
loudly if the installed DLL is stale relative to the build output.

Running pytest directly (same thing `run.ps1 -Test` does):

```powershell
tests\.venv\Scripts\python.exe -m pytest tests --results-dir tests\results\dev -k "filter and not continuous"
```

## Reading `results/<run>/report.md`

1. The table at the top gives the counts; `run.ps1 -Test` prints the same counts and the report
   path as its last lines.
2. **Failures** come first: assertion text, a `value | ...` table with what was measured (region
   means, sizes, timings), image links, and the `[win_spout]` log excerpt of the OBS instance that
   was running. Open the linked `NN-<test>-<label>.png` (small) or `.full.png` (native size).
3. **Unexpected passes (XPASS)** means a known-issue test now passes: remove its `xfail` marker.
4. **Expected failures** lists the known issues (#75 brightness on 10-bit canvases, #84 output
   levels on partial-range / PQ canvases, #73 dual-GPU hint, #80 AutoStart at launch, #66 profile
   switch while running, and one finding of this harness: the filter keeps broadcasting off-program
   once it has rendered, see `test_20_filter.py`).
5. The **OBS logs** table at the end links every `obs-<profile>-<variant>-NN.log` with warning /
   error counts and forbidden-line hits; `desktop-*.png` is a desktop grab per launch (catches a
   blocking dialog).

## Layout

```
tests/
  run.ps1 / setup-admin.ps1 / setup-user.ps1 / requirements.txt / pytest.ini / conftest.py
  harness/      paths, obs_launcher (launch/readiness/stop), obs_client (websocket), spout_tool
                (backend selection), spoutgl_tool (fallback CLI), pattern (test image codec),
                obslog, gpu_prefs (HKCU UserGpuPreferences), report
  fixtures/portable/      portable_mode.txt + seeded config (websocket 4466, profiles, scene collection)
  fixtures/user-ini-variants/   continuous.ini, autostart.ini  ([win_spout] overrides)
  golden/       composite-modes.json, log-baseline.json (created on first run, then compared)
  spout-tool/   C++ SpoutDX sender/receiver/list (built by run.ps1 -Build; same JSON contract as spoutgl_tool)
  unit/         pure-Python tests (pattern codec, log parser, report) - run on hosted CI
  test_00_build_install.py  test_10_receiver.py  test_20_filter.py  test_30_output.py
  test_40_color_formats.py  test_50_dual_gpu.py  test_60_logs.py
  results/<run>/  (gitignored)
```

The pattern (`harness/pattern.py`, mirrored in `spout-tool/main.cpp`): R/G/B/50 %-grey quadrants
(hue-rotated by `--seed`), a red centre square with alpha 0.5 (opaque / straight / premultiplied),
and a bottom strip of 16 black/white cells carrying the frame number, which survives scaling and
lets tests prove that frames are actually moving through OBS.

## CI

`.github/workflows/gpu-tests.yaml` runs `tests/unit` on hosted Windows and the full harness on a
self-hosted runner labelled `[self-hosted, windows, gpu, spout]` (workflow_dispatch, nightly, or a
PR labelled `gpu-tests`). Register that runner on the GPU box as a **logon scheduled task in the
console session**, never as a service (session 0 has no GPU/desktop and `GetSourceScreenshot`
returns black).
