# Changelog

Azimuth Motorsport lets Forza Motorsport 5 (1.0.0.20) and Forza Motorsport 6 (1.0.0.44) run on Azimuth, which
officially supports only Forza Horizon 2 and FH2 Fast & Furious. It ships as a proxy `winmm.dll` plus the optional
`azimuth_fm5.ini` / `azimuth_fm6.ini`. Everything happens in memory: `azimuth.exe` is never modified on disk and
FH2 keeps working as before.

Requirements: Azimuth 0.9.9 and your own dump of FM5 1.0.0.20 and/or FM6 1.0.0.44.

When reporting a bug, please attach `azimuth_motorsport.log` (next to `azimuth.exe`) and `data\logs\azimuth.log`.

## Current support

### FM6
- Boot, menus, videos, audio and races, with sessions of an hour or more without crashes.
- Launcher resolution, frame rate (up to 180 fps), anti-aliasing (2x / 4x MSAA), Car Detail, Shadows and
  Reflections presets.
- `azimuth_fm6.ini`: `SkipIntro`, `SkipPresentationVideo`, `UnlockDLC`, `AlphaToCoverageNoDither`.

### FM5
- MSAA and 16x anisotropic filtering.
- Launcher resolution, with the UI scaled correctly.
- 60, 120 and 180 fps, with the launcher frame rate kept in the menus as well.
- Car Detail, Shadows and Reflections presets.
- `azimuth_fm5.ini`: `SkipIntro`, `SkipPresentationVideo`, `UnlockDLC`, `EnableMarketplace`.

### Known limitations
- **FM6:** the Particles and Motion Blur presets have no effect.
- **FM6:** very heavy scenes can drop below the chosen frame rate.
- **FM6:** the RivaTuner (RTSS) overlay can crash the game.
- **FM5:** the Particles and Motion Blur presets have no effect.
- No Xbox Live features (Rivals, online play, leaderboards).

## Unreleased

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
