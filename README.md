# Custom Health Bar

A fully customizable health bar for Twilight Princess on Dusklight.

**Version:** 1.0.1

## Build dependencies

CMake fetches the pinned Dusklight source checkout into `build/dusklight/`. This checkout is used only as a build-time dependency/reference; it is not included in the packaged `health_bar.dusk` mod. Keeping it under `build/` leaves the mod source directory uncluttered.

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

- Replaces the vanilla heart display with a dedicated copy of the game's full Lantern-meter layout.
- Keeps the original Lantern fuel meter working independently when using the item.
- Preserves the normal HUD show/hide and fade behavior.
- X/Y positioning with screen-edge and center anchors.
- Adjustable Length from 50% to 300%, preserving the decorative end caps.
- Adjustable Scale from 50% to 200%.
- Adjustable Health Update Speed from 1% to 100%.
- Custom RGB health-bar color.
- Optional Auto Visibility with a 1-60 second timer.
