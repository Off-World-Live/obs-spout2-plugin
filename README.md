Spout2 Source Plugin for OBS Studio  (64bit)
=========

This plugin enables the import of shared textuers at high resolution from [SPOUT2](https://github.com/leadedge/Spout2) compatible
programs.

## Why

Previously the only way to import shared textures from SPOUT was via the DirectShow `SpoutCam` interface or by screen-capturing
the full-screen output of the `SpoutReceiver` program.

The `SpoutCam` is limited to standard webcam resolutions and capped at `1920x1080` and capturing the SpoutReceiver is both
inefficient and limited by your current screen resolution

This plugin implements the SPOUT2 SDK and creates Source from the SPOUT shared texture

## Acknowledgements

Thanks to the developer of [OBS-OpenVR-Input-Plugin](https://github.com/baffler/OBS-OpenVR-Input-Plugin) whose source
helped greatly in getting my head around the OBS API.

Thanks to the OBS team and their discord channel

Thanks to the authors of [SPOUT](https://github.com/leadedge/Spout2) for the library and clear documentation

## Installation

**N.B. For now I have only released a x64 build**

- Go to the [Releases Page](https://github.com/Off-World-Live/obs-spout2-source-plugin/releases)
- Download the windows installer: `OBS_Spout2_Plugin_Installer.exe`
- Run the installer (accepting installation from untrusted source)
- Select the `OBS` directory if not the default install location

### Manual Installation

- If you are unable to run the installer, download the release zip file `OBS_SPOUT2_Plugin_Build_xxx.zip`
- Extract the entire contents into your OBS directory
- Verify that `win-spout.dll` is in the dir `<obs-install>\obs-plugins\64bit`

## Contributing / Building

- Clone the [main OBS repository](https://github.com/obsproject/obs-studio)
- Carefully follow their [build instructions](https://obsproject.com/wiki/install-instructions#windows-build-directions)
- Add this repo as a submodule inside the plugins folder: `git submodule add git@github.com:Off-World-Live/obs-spout2-source-plugin.git plugins/win-spout`
- Download the latest [Spout Source](https://github.com/leadedge/Spout2/releases) and extract the contents of [this folder](https://github.com/leadedge/Spout2/tree/master/SpoutSDK/Source/SPOUT_LIBRARY) into a new folder `/deps/spout` inside the OBS Source code directory
- So far I have only built this in 64bit but I see no reason why it should not work with 32bit builds.

### Building the windows installer

- Download the latest version of [NSIS here](https://nsis.sourceforge.io/Download);
- Set the the `APPVERSION` variable in `win-spout-installer.nsi`
- Compile [win-spout-installer.nsi](./win-spout-installer.nsi)

Pull Requests welcome!

## Roadmap

- [x] Improve CMakeLists.txt to copy `Spout.dll` automatically (thanks to [@shugen002](https://github.com/shugen002))
- [ ] Check DirectX version and disable if not compatible

## License

This plugin authored by Campbell Morgan is copyright Off World Live Ltd, 2019 and [licenced under the GPL V.2](./LICENCE).
