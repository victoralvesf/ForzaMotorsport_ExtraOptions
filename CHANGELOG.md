# Changelog

Forza Motorsport Extra Options adds optional extras to Forza Motorsport 5 (1.0.0.20) and Forza Motorsport 6
(1.0.0.44) running on the official Azimuth. It ships as a proxy `winmm.dll` plus `motorsport_extraoptions.ini`
(sections `[GLOBAL]`, `[FM5]` and `[FM6]`). Everything happens in memory: `azimuth.exe` and the games are never
modified on disk.

Requirements: Azimuth 1.0.2 or later and your own dump of FM5 1.0.0.20 and/or FM6 1.0.0.44.

When reporting a bug, please attach `fm_extra_options.log` (next to `azimuth.exe`) and `data\logs\azimuth.log`.

## Current support

### Both games
- `[GLOBAL]`: `DisableLog` (ini only, not in the launcher).

### FM5
- `[FM5]`: `SkipIntro`, `SkipPresentationVideo`, `UnlockDLC`, `EnableMarketplace`, `UnlockMenuFrameRate`.

### FM6
- `[FM6]`: `SkipIntro`, `SkipPresentationVideo`, `UnlockDLC`, `UnlockMenuFrameRate`, `StockWheels`.
- Lap times on the digital dashboards in Brazilian Portuguese (always on).

## Unreleased

### Added
- FM6 `StockWheels` (off by default): a fifth rim style category, "Stock Rims (All Cars)", with the stock rims of
  every car in the game, by make in alphabetical order, free, to fit on any car. Each rim is loaded from its own
  car's package.
- FM5 `StockWheels` (off by default): the same category in FM5. FM5 car packages keep the rim model in a
  few different folders and file names; the mod tries each one, and a few special editions without a rim model of
  their own are listed but do not load.

### Changed
- The StockWheels engine (SQL and rim loader) moved to the shared core; FM6 only describes its sites.

## v1.0.1 — Launcher section on Azimuth 1.0.4

### Fixed
- The Extra Options section in Azimuth's launcher shows up again on Azimuth 1.0.4 (its settings page code changed);
  1.0.2 and 1.0.3 keep working as before.

## v1.0.0 — Extra options for the official Azimuth

First release of Forza Motorsport Extra Options.

### New
- One options file for both games, `motorsport_extraoptions.ini`, with a `[FM5]` and a `[FM6]` section.
- **Extra Options in Azimuth's launcher:** the settings page of FM5 and FM6 has a new Extra Options section (next to
  Display, Graphics, Audio and Content packages) with an Off/On switch per option. Changes are saved to
  `motorsport_extraoptions.ini` at once. If the launcher code does not match (another Azimuth version), the section
  is not added and the ini still works.
- Works with any Azimuth from 1.0.2 on. Every option checks the game's bytes before writing and is left off, with
  the reason in `fm_extra_options.log`, when they don't match.
- **`[GLOBAL]` `DisableLog`:** `1` turns `fm_extra_options.log` off (default `0`, log on). Only in the ini, not in
  the launcher.
- **FM5 and FM6:** `SkipIntro`: skips the opening videos.
- **FM5 and FM6:** `SkipPresentationVideo`: skips the presentation video before the first race (on FM6 also the
  intros of the Porsche and NASCAR expansions, if you have the DLC files).
- **FM5 and FM6:** `UnlockDLC`: every DLC included in the game files counts as purchased. Works together with
  Azimuth's own DLC support; with it off, Azimuth's support works as usual.
- **FM5:** `EnableMarketplace`: enables the Marketplace option in the menu without Xbox Live (shows the game's DLC
  packs).
- **FM5 and FM6:** `UnlockMenuFrameRate`: menus and transitions run at the frame rate chosen in Azimuth instead of
  half of it (the game asks for half there).
- **FM6:** in Brazilian Portuguese, lap times on the digital dashboards showed boxes in place of the minute and
  second marks. The dashboard fonts have no apostrophe, which the Portuguese time format uses; those times are
  now shown as in English (`00:12,977`). Always on.

Downloads: https://github.com/victoralvesf/ForzaMotorsport_ExtraOptions/releases
