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

- Download the latest release build zip file (_not SourceCode.zip_)
- Extract to your OBS Directory (usually `C:\Program Files (x86)\obs-studio`)
- check that `win-spout.dll` and `SpoutLibrary.dll` are in the `obs-plugins` folder
- Run OBS

## Contributing / Building

- Clone the [main OBS repository](https://github.com/obsproject/obs-studio)
- Carefully follow their [build instructions](https://obsproject.com/wiki/install-instructions#windows-build-directions)
- Add this repo as a submodule inside the plugins folder: `git submodule add git@github.com/Off-World-Live/obs-spout2-source-plugin.git plugins/win-spout`
- Download the latest [Spout Source](https://github.com/leadedge/Spout2/releases) and extract the contents of [this folder](https://github.com/leadedge/Spout2/tree/master/SpoutSDK/Source/SPOUT_LIBRARY) into a new folder `/deps/spout` inside the OBS Source code directory
- After building, be sure to copy [Spout.dll](https://github.com/leadedge/Spout2/blob/master/SpoutSDK/Source/SPOUT_LIBRARY/Binaries/x64/SpoutLibrary.dll) to your the `obs-plugins` directory inside your `build/runtime/Debug` folder. (I haven't worked out how to get CMake to do this automatically - suggestions / PRS welcome.
- So far I have only built this in 64bit but I see no reason why it should not work with 32bit builds.

Pull Requests welcome!

## Roadmap

- [ ] Improve CMakeLists.txt to copy `Spout.dll` automatically
- [ ] Check DirectX version and disable if not compatible

## License

This plugin authored by Campbell Morgan is copyright Off World Live Ltd, 2019 and [licenced under the GPL V.2](./LICENCE).
