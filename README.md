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

## Installation

- Go to the [Releases Page](https://github.com/Off-World-Live/obs-spout2-plugin/releases)
- Download the windows installer: `OBS_Spout2_Plugin_Installer.exe`
- Run the installer (accepting installation from untrusted source)
- Keep the default install location (`C:\ProgramData\obs-studio\plugins\win-spout`) unless you use a portable OBS
  install, in which case choose `<OBS folder>\plugins\win-spout`

This version requires OBS Studio 33.0 or newer. It uses the plugin layout introduced in OBS 33, so it is not flagged as
**Legacy** in the plugin manager:

```
plugins\win-spout\win-spout.dll   (plus Spout.dll, SpoutDX.dll, SpoutLibrary.dll)
plugins\win-spout\data\locale\*.ini
```

The installer removes files left by older versions in `plugins\win-spout\bin\64bit` and
`C:\Program Files\obs-studio\obs-plugins\64bit`. If you installed manually from a zip, delete those old copies yourself.
For a portable OBS install, extract the `-portable` zip into the root of the OBS folder.

> N.B there are no current plans for 32bit builds, although theoretically this should be possible

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
