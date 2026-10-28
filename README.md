# Forza Motorsport Extra Options

**Author:** Victor Alves (L0rdDarkside)

Extra options for **Forza Motorsport 5** (Xbox One, title ID `7B35D917`) and **Forza Motorsport 6** (title ID
`64F9397E`) running on the official Azimuth (Almost There Games): skip the intro and presentation videos, unlock
the DLC cars already included in the game, keep Azimuth's frame rate in menus and transitions, and enable the
Marketplace in FM5. It also fixes the lap times on FM6's
digital dashboards in Brazilian Portuguese.

The mod is a single proxy `winmm.dll` placed next to `azimuth.exe`, plus `motorsport_extraoptions.ini` with a
section per game (`[FM5]`, `[FM6]`) and a `[GLOBAL]` section. All of its changes are made in memory when Azimuth
loads the game.
`azimuth.exe`, the game and the other Azimuth files are **not modified on disk**.

> Versions 1.x of this project (then called Azimuth Motorsport) made FM5 and FM6 run on an older Azimuth. Azimuth
> 1.0.2 supports both games officially, so 2.0 keeps only the extra options. Remove any 1.x `winmm.dll` before
> updating Azimuth.

## Requirements

- **Azimuth 1.0.2 or later.**
- Your own dump of at least one of the games, as supported by Azimuth:
  - **Forza Motorsport 5, package version 1.0.0.20**
  - **Forza Motorsport 6, package version 1.0.0.44**

Each option checks the exact bytes it changes in the game before writing anything. If a game or Azimuth version
does not match, that option is simply left off and the log says why.

## Installation

1. Copy these files into the Azimuth folder, the one that contains `azimuth.exe`:
   - `winmm.dll`
   - `motorsport_extraoptions.ini`

   The result should look like this:

   ```
   <Azimuth folder>\
       azimuth.exe
       winmm.dll                    <- mod
       motorsport_extraoptions.ini  <- mod options (FM5 and FM6)
       data\
   ```

2. (Optional) Choose the options: in Azimuth, open the settings of FM5 or FM6 and pick **Extra Options** (see
   below), or edit `motorsport_extraoptions.ini`.
3. Start `azimuth.exe` as usual and pick FM5 or FM6.

## Options

`1` turns an option on, `0` turns it off. An option missing from `motorsport_extraoptions.ini` (or a missing file) is off.

### In Azimuth's launcher

The settings page of FM5 and FM6 gets an **Extra Options** section, next to Display, Graphics, Audio and Content
packages, with an Off/On switch for each option below. A change is saved to `motorsport_extraoptions.ini` right
away and applies the next time the game starts. The other games in Azimuth are not affected.

The section is added only when the launcher code the mod relies on matches what it expects (Azimuth 1.0.2). With a
version where it does not, the launcher stays as it is (the log says why) and the options still work from the ini.

### Global (section `[GLOBAL]`)

Only in `motorsport_extraoptions.ini` (not in Azimuth's launcher):

| Option | Default | What it does |
|---|---|---|
| `DisableLog` | 0 | `1` stops writing `fm_extra_options.log` (and deletes the one of an earlier run). |

### Forza Motorsport 5 (section `[FM5]`)

| Option | Default | What it does |
|---|---|---|
| `SkipIntro` | 1 | Skips the opening videos (Turn 10 / Microsoft Studios) and goes straight to the title screen. |
| `SkipPresentationVideo` | 0 | Skips the presentation video that plays after the brightness screen and goes straight to the first race. |
| `UnlockDLC` | 1 | The DLC cars and packs already included in the game (Car Pass, car packs, VIP...) count as purchased; the cars show up to buy with in-game credits. |
| `EnableMarketplace` | 1 | Enables the Marketplace option in the menu without Xbox Live (shows the game's DLC packs). |
| `UnlockMenuFrameRate` | 1 | Keeps the frame rate chosen in Azimuth in menus and transitions too (the game itself halves it there; races already run at the chosen rate). |

### Forza Motorsport 6 (section `[FM6]`)

| Option | Default | What it does |
|---|---|---|
| `SkipIntro` | 1 | Skips the opening videos and goes straight to loading. |
| `SkipPresentationVideo` | 1 | Skips the presentation video before the first race of a new profile and the intro videos of the Porsche/NASCAR expansions. |
| `UnlockDLC` | 1 | The DLC cars and packs already included in the game (Car Pass, car packs, VIP, Porsche/NASCAR expansions...) count as purchased; the cars show up to buy with in-game credits. |
| `UnlockMenuFrameRate` | 1 | Keeps the frame rate chosen in Azimuth in menus and transitions too (the game itself halves it there; races already run at the chosen rate). |

Always on, no option: in Brazilian Portuguese, lap times on FM6's digital dashboards showed boxes in place of the
minute and second marks (`00□12,977□□`), because the dashboard fonts have no apostrophe. Those times are shown as in
English (`00:12,977`).

Azimuth 1.0.2 has its own DLC support for the content packages in the game folder. With `UnlockDLC=0` that support
works as usual; with `UnlockDLC=1` every DLC included in the game counts as purchased.

## Logs and troubleshooting

- `fm_extra_options.log` (next to `azimuth.exe`) is the mod's log, recreated on every run. It shows the Azimuth
  version, the game found and, for each option, whether it was applied, off, or ignored because the game's bytes
  did not match. It is not written with `DisableLog=1` in `[GLOBAL]`; set it back to `0` before reporting a
  problem.
- `data\logs\azimuth.log` is Azimuth's own log.

Include both files when you report a problem.

## Uninstall

Delete `winmm.dll` (and `motorsport_extraoptions.ini` if you like) from the Azimuth folder. That restores the
plain Azimuth. Nothing else was changed.

## Building from source

See `ARCHITECTURE.md` for how the mod works. It is C++20, built with CMake + Ninja and the mingw-w64 g++
(`C:\mingw64\bin`):

```
cmake --preset default
cmake --build --preset default
ctest --preset default
```

The output is `build\winmm.dll`.

GitHub Actions (`.github/workflows/ci.yml`) runs the tests on every pull request and, on a `v*` tag push, runs the
tests, builds and publishes a release with `winmm.dll` + `motorsport_extraoptions.ini` + `README.txt`
(this file as plain text, made by `tools/md2txt`).

## License

MIT, see `LICENSE`. It covers only the code in this repository. Azimuth, Forza Motorsport 5 and Forza Motorsport 6
belong to their respective owners and are not included.
