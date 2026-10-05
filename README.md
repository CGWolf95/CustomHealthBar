# Custom Health Bar

A fully customizable health bar for Twilight Princess on Dusklight.

## Settings

### CUSTOMIZE

- Color: `#FF4D4D`
- X Position: `0`
- Y Position: `0`
- Length: `200%`
- Scale: `100%`
- Health Update Speed: `5%`

### FILL ALIGNMENT

Left / Center / Right

### HORIZONTAL ANCHOR

Left / Center / Right

### VERTICAL ANCHOR

Top / Center / Bottom

### AUTO VISIBILITY

- Auto Visibility: Off
- Visibility Timer: `10 seconds`

## Features

- Replaces the vanilla heart display with the game's full Lantern-use meter.
- Preserves the normal HUD show/hide and fade behavior.
- X/Y positioning with screen-edge and center anchors.
- Adjustable Length from 50% to 300%, preserving the decorative end caps.
- Adjustable Scale from 50% to 200%.
- Adjustable Health Update Speed from 1% to 100%.
- Custom RGB health-bar color.
- Optional Auto Visibility with a 1-60 second timer.

## Release metadata

- Mod ID: `com.cgwolf.health_bar`
- Author: `CGWolf`
- Version: `v1.0`

## Build

This project follows the official Dusklight mod-template structure and fetches Dusklight v2.0.2 automatically.

```powershell
cd <path-to-this-source-folder>
cmake -S . -B build -DGIT_EXECUTABLE="E:\Software\PortableGit\bin\git.exe"
cmake --build build --config Release
```

The resulting bundle is `build/mods/health_bar.dusk`.


## GitHub release builds

The repository includes the official Dusklight-style GitHub Actions workflow. Each tagged release is built for Linux x86_64, Linux AArch64, macOS x86_64, macOS ARM64, iOS ARM64, Windows x64, Windows ARM64, and Android AArch64.

GitHub Actions first produces one `.dusk` bundle per platform, then `tools/merge_mod.py` combines them into a single multi-platform `health_bar.dusk`. A tag such as `v1.0` creates a GitHub release and attaches the combined bundle automatically.

The release manifest remains **v1.0**. The project is intentionally not versioned as a later development release.
