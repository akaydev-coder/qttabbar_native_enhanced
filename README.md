# QTTabBar Native Enhanced

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE.txt)
[![.NET Framework 4.8](https://img.shields.io/badge/.NET%20Framework-4.8-512BD4.svg)](https://dotnet.microsoft.com/download/dotnet-framework/net48)
[![Release](https://img.shields.io/github/v/release/akaydev-coder/qttabbar_native_enhanced)](https://github.com/akaydev-coder/qttabbar_native_enhanced/releases/latest)

An enhanced native/managed QTTabBar fork for the classic Windows Explorer interface. This project consolidates work from several QTTabBar code lines and adds a stability-focused native integration for Windows 10.

## Stable release

Current version: **1.6.5.0 Stable**

Download the installer from [GitHub Releases](https://github.com/akaydev-coder/qttabbar_native_enhanced/releases/latest).

## Highlights

- Native/managed hybrid Explorer integration on .NET Framework 4.8
- Fluent Glass rendering for the tab bar and vertical command bar
- Custom tab skin and custom new-tab button images
- Restored and stabilized **QT Command Bar (vertical)** / Versatile Bar
- Fixed-width vertical bar with application shortcuts and separators
- Per-tab taskbar thumbnails and full-size DWM live previews
- Taskbar thumbnail activation and close-button support
- Explorer background images configured through `C:\ProgramData\QTTabBar\config.ini`
- Background renderer owned by the stable native bridge; the legacy hook DLL remains unloaded by default
- Stable options dialog lifecycle and runtime settings refresh
- Explorer registration, toolbar persistence, focus and Win+E capture fixes

## Supported environment

- Windows 10 x64 with the classic Explorer command-bar interface
- .NET Framework 4.8

Windows 11 may require a classic Explorer restoration solution. Test changes in a VM before installing them on a primary workstation.

## Installation

1. Download `QTTabBar Setup - 1.6.5.0 Stable.exe` from the latest release.
2. Run the installer as an administrator.
3. Restart Windows when requested.
4. In Explorer, enable **QTTabBar** under **View > Toolbars**.
5. Optionally enable **QT Command Bar (vertical)** under **View > Explorer Bar**.

Error logs are written to `%APPDATA%\QTTabBar\QTTabBarException.log`.

## Explorer background

The installer creates `C:\ProgramData\QTTabBar\config.ini` and a default `libai.png` image. The background renderer runs directly inside `QTTabBarNative.dll`; it does not load the legacy `QTHookLib` in background-only mode. Images are decoded through Windows Imaging Component into premultiplied `32bppPBGRA` surfaces before only the required `dui70.dll` import slots are patched.

```ini
[hook]
enabled=false

[image]
enabled=true
random=false
folder=Image
custom=false
posType=3
imgAlpha=255
imgPath=%ProgramData%\QTTabBar\libai.png
```

`posType` values: `0` top-left, `1` top-right, `2` bottom-left, `3` bottom-right, `4` centered, `5` stretch, `6` zoom-fill.

Place additional PNG/BMP/JPG/JPEG files in the configured `folder`. With `custom=true`, a section named after an Explorer path can select one of those files:

```ini
[C:\Users\Public\Pictures]
img=example.png
```

Hold Escape while Explorer starts to bypass background initialization for recovery.

## Building

Requirements:

- Visual Studio 2022 Build Tools 17.14.37411.7
- MSVC/ATL 14.44.35207
- Windows SDK 10.0.19041.0
- .NET Framework 4.8 SDK and targeting pack
- WiX Toolset 3.14.1.8722

The pinned versions and VC++ redistributable hashes live in `Build\Toolchain.psd1`. `Build\Test-BuildEnvironment.ps1` fails early when the machine differs from that baseline. The bundle stages the verified x86 and x64 redistributables from Visual Studio without committing Microsoft binaries to the repository.

```powershell
.\Build\Test-BuildEnvironment.ps1
.\build_release.ps1
```

## Project lineage and credits

QTTabBar Native Enhanced exists because of the work of QTTabBar's original creator and later open-source maintainers. See [CREDITS.md](CREDITS.md) for the current attribution list and upstream links.

Major integration, debugging and implementation assistance for this enhanced edition was provided by **OpenAI Codex**, directed and extensively tested by **Akay Devel Coder**.

## License

This project is distributed under the [GNU General Public License v3.0](LICENSE.txt). Existing copyright and attribution notices in inherited source files remain in effect.
