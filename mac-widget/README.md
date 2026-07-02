# DeskHog Widget (macOS)

The standalone Mac version of the desk ornament: a hedgehog runs inside a
spinning wheel, faster the harder your Mac's CPU is working. No DeskHog
hardware required — it's a small floating window that sits on your desktop.

This is the same idea as the DeskHog `CPU Hog` card, but native to the Mac
and reading the Mac's CPU directly.

## Build & run

Requires macOS 13+ and the Swift toolchain (Xcode or Command Line Tools).

```sh
cd mac-widget
swift run
```

A little green panel with a running hog appears and floats above your other
windows. Drag it anywhere. Quit from the 🦔 menu-bar item.

To build a release binary you can keep around:

```sh
swift build -c release
# binary at .build/release/DeskHogWidget
```

## How it works

- `CPUSampler` reads system CPU via the Mach `host_statistics` API, diffing the
  cumulative tick counters between samples (user + system + nice vs. idle).
- `HogEngine` samples once a second, smooths it, and advances the wheel angle
  every frame — mapping CPU 0-100% to roughly 0.25→4 wheel revolutions/second.
- `HogView` draws the wheel + hog in SwiftUI; `AppDelegate` hosts it in a
  borderless, transparent, always-on-top window and adds a menu-bar quit item.

## Notes

- No entitlements needed — reading CPU stats works from a normal user process.
- It's an accessory app (no Dock icon); it lives in the menu bar.
- Not yet compiled/flashed by the author — build on a Mac and tweak
  `idleRPS`/`maxRPS` in `HogEngine.swift` for how lazy/frantic the run feels.
- The 🦔 emoji stands in for art; drop in a sprite/`Image` if you want Max
  himself on the wheel.
