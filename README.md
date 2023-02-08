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
- Select the `OBS` directory if not the default install location

> N.B there are no current plans for 32bit builds, although theoretically this should be possible

## Contributing / Building

- Clone this repo recursively
```
git clone --recursive git@github.com:off-world-live/obs-spout2-plugin
```
- Clone the [main OBS repository](https://github.com/obsproject/obs-studio) recursively.
- Carefully follow their [build instructions](https://obsproject.com/wiki/install-instructions#windows-build-directions) ensuring that your `build` folder is `build64`
- Add this repo as a submodule inside the plugins folder: `git submodule add git@github.com:Off-World-Live/obs-spout2-source-plugin.git plugins/win-spout`
- Edit the `CMakeLists.txt` file in `/plugins` directory and add `add_subdirectory(win-spout)` inside the `if(WIN32)` block.
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
