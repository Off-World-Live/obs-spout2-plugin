Spout2 Plugin for OBS Studio (64bit)
=========

This plugin enables the import and export of shared textures at high resolution to and from [SPOUT2](https://github.com/leadedge/Spout2) compatible
programs.

## Why

Previously the only way to import shared textures from SPOUT was via the DirectShow `SpoutCam` interface or by screen-capturing
the full-screen output of the `SpoutReceiver` program.

The `SpoutCam` is limited to standard webcam resolutions and capped at `1920x1080` and capturing the SpoutReceiver is both
inefficient and limited by your current screen resolution.

Previously, there was no way of outputting Spout video textures from OBS. 

This plugin implements the SPOUT2 SDK, creates an OBS Source from the SPOUT shared texture and a Spout output which sends the content of the OBS canvas to Spout.

Please see installation and usage guide [here](http://docs.offworld.live/#/obs-spout-plugin/README?id=spout-plugin-for-obs-studio)

## Acknowledgements

Thanks to the developer of [OBS-OpenVR-Input-Plugin](https://github.com/baffler/OBS-OpenVR-Input-Plugin) whose source
helped greatly in getting my head around the OBS API.

Thanks to the OBS team and their discord channel

Thanks to the authors of [SPOUT](https://github.com/leadedge/Spout2) for the library and clear documentation

## Requirements

- Windows 10/11, 64-bit, OBS Studio running on its default **Direct3D 11** renderer (the OpenGL renderer, `--allow-opengl`, is not supported — Spout shares D3D11 textures).
- **A matching OBS Studio version.** Each release is built against a specific OBS version and OBS refuses to load plugins
  that were built for a newer OBS than the one running. The release page states the minimum OBS version for each download
  (for example *"requires OBS Studio 32.1 or newer"*). If you are on an older OBS, update OBS or pick an older plugin release.
- There are no plans for 32-bit builds.

## Installation

Every release ships three downloads. Pick the one that matches how your OBS is installed:

### 1. Installer (`OBS_Spout2_Plugin_Install_v<version>.exe`) — normal OBS installs

- Close OBS.
- Download and run the installer (accept the "unknown publisher" prompt).
- Keep the default destination. The plugin is installed into OBS's shared plugin folder, which every OBS installed on the
  machine picks up automatically:
  ```
  C:\ProgramData\obs-studio\plugins\win-spout\
  ├── bin\64bit\win-spout.dll  (+ Spout.dll, SpoutDX.dll, SpoutLibrary.dll)
  └── data\locale\en-US.ini  (…)
  ```
- The installer checks the version of the OBS Studio it finds in the registry and refuses to install if OBS is too old
  for this build.

### 2. Zip (`win-spout-<version>-windows-x64.zip`) — the same layout, done by hand

Extract the archive so that you end up with exactly the folder tree shown above under `C:\ProgramData\obs-studio\plugins\`.
(Create the `plugins` folder if it does not exist yet.)

### 3. Portable zip (`win-spout-<version>-windows-x64-portable.zip`) — portable OBS

OBS in portable mode (a `portable_mode.txt` file in the OBS folder) does **not** read `C:\ProgramData`, so use this archive
instead. Extract it straight onto your OBS folder so that you end up with:
```
<your OBS folder>\
├── obs-plugins\64bit\win-spout.dll  (+ Spout.dll, SpoutDX.dll, SpoutLibrary.dll)
└── data\obs-plugins\win-spout\locale\en-US.ini  (…)
```

### Troubleshooting

- **"The following OBS plugins failed to load: win-spout — Please update or remove these plugins."**
  OBS is older than the version this plugin build was made for. Open `Help → Log Files → View Current Log` and look for a line
  like `Module 'win-spout.dll' compiled with newer libobs 32.1`. Update OBS, or install the plugin release that matches your
  OBS version.
- **"Spout2 Capture" is missing from the Sources list, but the plugin loaded.** Another plugin is registering the same source id
  (`spout_capture`); the OBS log shows `obs_register_source: Source 'spout_capture' already exists!`. Remove the other plugin
  (PRPR Live's `prpr-library.dll` is a known case).
- **Menu entries show up as `toolslabel` / `sourcename`.** The `data\locale` folder was not extracted next to the plugin;
  compare your folder tree with the ones above.
- **The source lists the sender but the preview is black**, and the log says `gs_texture_open_shared (D3D11): Failed to open
  shared 2D texture (80070057)`: the sending application is running on a different GPU than OBS (typical on laptops with an
  integrated + dedicated GPU). In Windows *Settings → System → Display → Graphics*, set both OBS and the sender application to
  the same GPU.
- **Where do the Spout Output settings live?** `Tools → Spout Output Settings`. The AutoStart and sender-name values are stored
  in your OBS user config (`%APPDATA%\obs-studio\user.ini`, section `[win_spout]`) and survive reinstalling OBS or the plugin.

## Contributing / Building

- Clone this repo recursively
```
git clone --recursive git@github.com:off-world-live/obs-spout2-plugin
```
- Install [CMAKE min version 3.28](https://cmake.org/download/)
- Either configure and generate through the CMAKE Gui or through the command line for the `windows-x64` architecture
- Run `Configure`, `Generate` and then `Open Project` in the `CMake Gui`

### Building a release locally

- Open `git bash` or similar bash terminal interpreter
- Run `./scripts/Release.sh <version number>`
- You should find the executable (installer) and zip file in the main `win-spout` directory
### Building the windows installer

- Download the latest version of [NSIS here](https://nsis.sourceforge.io/Download);
- Set the the `APPVERSION` variable in `win-spout-installer.nsi`
- Compile [win-spout-installer.nsi](./win-spout-installer.nsi)

Pull Requests welcome!

## Contributors

Thanks to everybody that submitted bug tickets and in particular the code contributors:

- [@shugen002](https://github.com/shugen002)
- [@mzlt](https://github.com/mzlt)
- [@terids](https://github.com/terids)

## Roadmap

- [x] Improve CMakeLists.txt to copy `Spout.dll` automatically (thanks to [@shugen002](https://github.com/shugen002))
- [x] Spout Output
- [x] Spout Filter Output

## License

This plugin authored by Campbell Morgan is Copyright Off World Live Ltd, 2019-2021 and [licenced under the GPL V.2](./LICENCE).
