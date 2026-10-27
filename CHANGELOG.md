# Changelog

Forza Motorsport Extra Options adds optional extras to Forza Motorsport 5 (1.0.0.20) and Forza Motorsport 6
(1.0.0.44) running on the official Azimuth. It ships as a proxy `winmm.dll` plus `azimuth_fm5.ini` /
`azimuth_fm6.ini`. Everything happens in memory: `azimuth.exe` and the games are never modified on disk.

Requirements: Azimuth 1.0.2 or later and your own dump of FM5 1.0.0.20 and/or FM6 1.0.0.44.

When reporting a bug, please attach `fm_extra_options.log` (next to `azimuth.exe`) and `data\logs\azimuth.log`.

## Current support

### FM5
- `azimuth_fm5.ini`: `SkipIntro`, `SkipPresentationVideo`, `UnlockDLC`, `EnableMarketplace`, `UnlockMenuFrameRate`.

### FM6
- `azimuth_fm6.ini`: `SkipIntro`, `SkipPresentationVideo`, `UnlockDLC`, `UnlockMenuFrameRate`.
- Lap times on the digital dashboards in Brazilian Portuguese (always on).

## Unreleased

### Changes
- **Only the extra options; requires the official Azimuth 1.0.2 or later.** Azimuth now runs FM5 and FM6 by itself,
  so the compatibility layer of 1.x (game unlock, Direct3D/XG/audio/WinRT bridges, launcher resolution, frame rate
  and graphics presets, FM6 rendering and stability fixes) is gone. Remove any 1.x `winmm.dll` before updating
  Azimuth.
- The mod no longer requires one exact `azimuth.exe`: it works with any Azimuth from 1.0.2 on. Every option still
  checks the game's bytes before writing and is left off, with the reason in the log, when they don't match.
- The mod's log is now `fm_extra_options.log` (it was `azimuth_motorsport.log`).
- Rewritten in C++, built with CMake.

### New
- **FM5 and FM6:** `UnlockMenuFrameRate`: menus and transitions run at the frame rate chosen in Azimuth instead of
  half of it (the game asks for half there).

### Improvements
- **FM5 and FM6:** `UnlockDLC` works together with Azimuth's own DLC support: with it on, every DLC included in the
  game counts as purchased; with it off, Azimuth's support works as usual.

### Removed
- **FM6:** `AlphaToCoverageNoDither`. Azimuth 1.0.2 draws the edges of liveries, decals, fences and foliage
  correctly by itself.

## v1.5.0 — FM6 anti-aliasing, reflections, terrain and stutter fixes

### Improvements
- **FM6:** the launcher's anti-aliasing option now picks the race's MSAA: `msaax4` renders the scene with 4 real
  samples (the console uses 2 samples with 4 coverage samples, EQAA, which PC GPUs can't do), and `2x` keeps 2.
- **FM6:** the launcher's Reflections preset now works, like on FM5: high and ultra raise the size of the
  cars' reflection cubemap (up to 2048), the mirrors (up to 2304x768, always at full frame rate) and the windshield
  reflection, and update the cubemap more often; low lowers them for more performance.
- **FM6:** fewer stutters while racing. The proxy did extra CPU work on the game's own threads: it copied large
  blocks of render state for every nested draw bundle, searched long lists on every draw call, and scanned all
  live textures whenever a new streamed texture was created. That work is now much smaller, roughly halving the
  frame spikes in a race and lowering the CPU time of the game's command recording threads.

### Fixes
- **FM6:** the race had no anti-aliasing at all: the game's EQAA resolve read the emulated FMASK in another layout
  and kept only one sample per pixel, so car edges were jagged, with white dots flickering along them.
- **FM6:** the digital dashboards in the cockpit view were drawn out of place at resolutions above 1080p. The UI
  scaling for the launcher resolution also hit the dashboard screens, which the game draws into their own
  smaller textures; it now applies only to the screen itself.
- **FM6:** in Brazilian Portuguese, lap times on the digital dashboards showed boxes in place of the minute and
  second marks (`00□12,977□□`). The game's dashboard fonts have no apostrophe, which the Portuguese time format
  uses; those times are now shown as in English (`00:12,977`), the format the dashboards were laid out for.
- **FM6:** track scenery looked low poly and flat-colored, most visibly on the rocks and slopes of Bernese Alps
  (grass and other terrain on every track too). The draw bundles the game records on its worker threads use the
  texture samplers their parent command list bound, as on the console, but the proxy did not pass the samplers
  along, so the terrain's tiled detail textures were stretched by the default clamp sampler. The samplers now
  carry over with the rest of the inherited state.

## v1.4.1 — FM6 tuning setup name fix

### Fixes
- **FM6:** the game crashed when saving a tuning setup. Naming the setup opens the console's virtual keyboard,
  which Azimuth didn't provide for FM6. It now opens Azimuth's text input (the same one FH2 uses), and the setup
  is saved with the name you type.

## v1.4.0 — FM5 sun flare fix and FM6 crash fix

### Fixes
- **FM5:** the colored line across the sun lens flare is now really gone. The v1.3.1 fix covered only part of
  the problem: the last row of the smaller mip levels of some compressed textures (such as the lens dirt) was
  still never uploaded to the GPU, so leftover memory kept showing up on screen.
- **FM6:** random driver crash (device removed after a GPU page fault) during races and in the menus.

## v1.3.1 — FM5 tuning chart and sun flare fixes

### Fixes
- **FM5:** the gear ratio chart in the tuning menu was drawn out of place at resolutions above 1080p. It now
  follows the resolution chosen in the launcher, like the rest of the UI.
- **FM5:** a colored line showed up across the sun lens flare, at about the height of the speedometer. The last
  row of some small, compressed textures was never uploaded to the GPU, and that leftover garbage is what
  appeared on screen.

## v1.3.0 — FM6 shadows, smooth edges and stability fixes

### New
- **FM6:** the launcher's Shadows preset is now applied. High and Ultra use shadow maps of up to 4096 and give
  noticeably sharper, more detailed shadows.

### Fixes
- **FM6:** a checkerboard pattern appeared on the edges of car liveries, fences, foliage and the crowd. These
  edges are now smooth. Controlled by `AlphaToCoverageNoDither` in `azimuth_fm6.ini` (on by default).
- **FM6 / FM5:** random crash right after launch on NVIDIA systems where features of the NVIDIA app load CUDA
  into the game.
- **FM6:** driver crash after long sessions (around 20 minutes of racing).
- **FM6:** audible click at the start of sounds such as the menu navigation sound. Every sound was losing its
  first few milliseconds.
- **Linux (Wine/Proton):** GPU page faults on vkd3d-proton with RADV. The workaround for the AMD Windows driver
  is now off by default when running under Wine.

## v1.2.0 — FM6 resolution, frame rate and Car Detail

### New
- **FM6:** the launcher resolution (e.g. 1440p) is now applied. Menus, HUD and charts are scaled to fit the new
  resolution.
- **FM6:** the launcher frame rate is kept in menus, pre-race, post-race and replays, which used to drop to 30 fps.
- **FM6:** 120, 144 and 180 fps, up to your monitor's refresh rate. The game speed stays the same at any frame
  rate.
- **FM6:** the launcher's Car Detail preset is now applied (car level of detail and the distances where it
  changes).

### Fixes
- **FM6:** garage and save thumbnails were black.

## v1.1.0 — Forza Motorsport 6, initial support

### New
- **FM6:** boot, menus, videos, audio and races work at about 60 fps.
- **FM6:** long sessions (an hour or more of racing) without crashes or memory growth.
- **FM6:** options in `azimuth_fm6.ini`:
  - `SkipIntro`: skips the opening videos.
  - `SkipPresentationVideo`: skips the presentation video before the first race and the intros of the Porsche
    and NASCAR expansions (if you have the DLC files).
  - `UnlockDLC`: DLC cars and packs already in the game files (Car Pass, Car Packs, VIP) count as purchased and
    can be bought with in-game credits.
- The proxy log is now named `azimuth_motorsport.log`.

Downloads: https://github.com/victoralvesf/azimuth-motorsport/releases
