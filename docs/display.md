# Stretched, shrunk or shifted game window under CrossOver

Not related to desyncs, but common: on a Mac the game sometimes starts in a small part of
the screen, or stretched, or shifted down by the height of the menu bar. It happens at
resolutions other than the display's native one, for example at 800×600, which the game
uses for a new profile.

## Cause

With *High Resolution Mode* enabled in the bottle (Wine's `RetinaMode`), Wine works in
physical pixels: a 1920×1080-point Retina display is a 3840×2160 desktop for Windows
programs, at 192 DPI.

When the game asks for a different resolution, Wine's Mac driver really switches the
display to that mode, for 800×600 a non-Retina 1x mode. But it keeps scaling windows for
Retina, so the 800×600 game window becomes 400×300 points, a quarter of the screen.
Sometimes it is also placed below the menu bar: the window's Win32 position is (0, 60)
pixels, which is 30 points.

Measured with `tools/display/winrect.c` (Win32 side) and CoreGraphics (macOS side):

| Bottle | Game resolution | Display mode | Game window on screen |
|---|---|---|---|
| High Resolution on | 3840×2160 (native) | unchanged, 1920×1080 @2x | 1920×1080 pt, full screen ✓ |
| High Resolution on | 800×600 | switched to 800×600 @1x | 400×300 pt, sometimes at y = 30 ✗ |
| High Resolution off | 3840×2160 | switched to 3840×2160 @1x | 3840×2160 pt, full screen ✓ |

## Workarounds

Either one is enough:

* Set the game resolution to the Wine desktop size, so no mode switch happens. With
  High Resolution on, that is the display size in pixels (e.g. 3840×2160). In the game's
  options, or in `Options.ini` of the profile
  (`drive_c/users/crossover/AppData/Roaming/Command & Conquer 3 Tiberium Wars/Profiles/<name>/Options.ini`,
  line `Resolution = 3840 2160`; Kane's Wrath has its own folder).
* Turn off *High Resolution Mode* for the bottle. Mode switches then work correctly. At a
  game resolution equal to the display size in points (e.g. 1920×1080) nothing is switched
  and the UI has its usual size.

A new profile starts at 800×600 again, so with High Resolution on set the resolution after
creating a profile.
