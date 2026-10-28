# Changelog

Forza Motorsport Extra Options adds optional extras to Forza Motorsport 5 (1.0.0.20) and Forza Motorsport 6
(1.0.0.44) running on the official Azimuth. It ships as a proxy `winmm.dll` plus `motorsport_extraoptions.ini`
(sections `[FM5]` and `[FM6]`). Everything happens in memory: `azimuth.exe` and the games are never modified on
disk.

Requirements: Azimuth 1.0.2 or later and your own dump of FM5 1.0.0.20 and/or FM6 1.0.0.44.

When reporting a bug, please attach `fm_extra_options.log` (next to `azimuth.exe`) and `data\logs\azimuth.log`.

## Current support

### FM5
- `[FM5]`: `SkipIntro`, `SkipPresentationVideo`, `UnlockDLC`, `EnableMarketplace`, `UnlockMenuFrameRate`.

### FM6
- `[FM6]`: `SkipIntro`, `SkipPresentationVideo`, `UnlockDLC`, `UnlockMenuFrameRate`.
- Lap times on the digital dashboards in Brazilian Portuguese (always on).

## Unreleased

First release of Forza Motorsport Extra Options.

### New
- One options file for both games, `motorsport_extraoptions.ini`, with a `[FM5]` and a `[FM6]` section.
- Works with any Azimuth from 1.0.2 on. Every option checks the game's bytes before writing and is left off, with
  the reason in `fm_extra_options.log`, when they don't match.
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
