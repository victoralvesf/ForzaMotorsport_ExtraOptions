# Azimuth Motorsport + Extra Options mod

**Author:** Victor Alves (L0rdDarkside)

A mod that lets **Forza Motorsport 5** (Xbox One, title ID `7B35D917`) and
**Forza Motorsport 6** (title ID `64F9397E`) run on Azimuth (Almost There Games), which
officially supports only Forza Horizon 2 and FH2 Fast & Furious. It also adds some extra
options for each game.

The mod is a single proxy `winmm.dll` placed next to `azimuth.exe`. All of its changes are
made in memory when Azimuth starts. `azimuth.exe` and the other Azimuth files are **not
modified on disk**, and FH2 keeps working as before.

## Requirements

- A clean Azimuth install. The mod expects this exact `azimuth.exe`:
  `SHA-256 579ed8d8b0b16cbf7890380cb7b925c972b837631ede367eada55eae4afc39bc`.
  If your `azimuth.exe` is different, the mod changes nothing and shows a warning
  ("azimuth.exe is not the expected version..."). Details go to `azimuth_motorsport.log`.
- Your own dump of at least one of the games:
  - **Forza Motorsport 5, package version 1.0.0.20**
  - **Forza Motorsport 6, package version 1.0.0.44**

  Each one is a game folder containing `Mount\` (with `Mount\AppxManifest.xml`) and
  `EmbeddedXvd\`, the same layout Azimuth uses for FH2. The folder name does not matter
  (an FM6 dump is often named `Monument_1.0.0.44_x64__8wekyb3d8bbwe`).

## Installation

1. Copy these files into the Azimuth folder, the one that contains `azimuth.exe`:
   - `winmm.dll`
   - `azimuth_fm5.ini`
   - `azimuth_fm6.ini`

   The result should look like this:

   ```
   <Azimuth folder>\
       azimuth.exe
       winmm.dll          <- mod
       azimuth_fm5.ini    <- mod options (FM5)
       azimuth_fm6.ini    <- mod options (FM6)
       data\
   ```

2. (Optional) Edit `azimuth_fm5.ini` / `azimuth_fm6.ini` to suit your preferences.
3. Start `azimuth.exe` as usual and select the FM5 or FM6 game folder in the launcher. To
   skip the launcher, run this from a command prompt (with your own game folder):

   ```
   azimuth.exe "--game=C:\YourGamesFolder\Forza Motorsport 5" --skip-launcher
   azimuth.exe "--game=C:\YourGamesFolder\Forza Motorsport 6" --skip-launcher
   ```

The launcher's graphics settings (resolution, frame rate, etc.) apply to FM5. On FM6 only the
**frame rate** is applied for now: menus, pre-race, post-race and replays run at the chosen
rate instead of dropping to 30, and rates above 60 (120, 144, 180...) work up to your
monitor's refresh rate, with the game speed unchanged.

## Status and known issues

- **Forza Motorsport 5:** menus, races, garage, DLC and Marketplace work.
- **Forza Motorsport 6 (initial support):** boots, menus, video, audio and races work, at the
  launcher's frame rate (60 or higher). Known issues:
  - the launcher's graphics settings other than frame rate are not applied (see above);
  - very heavy scenes (e.g. some pre-race shots) can still drop below the chosen frame rate;
  - turning on the RivaTuner (RTSS) overlay can crash the game ("device removed").
- Rivals and online modes need Xbox Live and do not work in either game.

## Logs and troubleshooting

- `azimuth_motorsport.log` (next to `azimuth.exe`) is the mod's log. It is recreated on every run.
- `data\logs\azimuth.log` is Azimuth's own log. Errors show up as `STOPPED ...` lines.

Include both files when you report a problem.

## Uninstall

Delete `winmm.dll` (and `azimuth_fm5.ini` / `azimuth_fm6.ini` if you like) from the Azimuth folder. That restores
the original Azimuth. Nothing else was changed.

## Building from source

See `ARCHITECTURE.md` for how the mod works.

The source is in `winmm_proxy\`. Build it with mingw-w64 gcc:

```
sh winmm_proxy/build.sh
```

The output is `winmm_proxy\build\winmm.dll`.

GitHub Actions (`.github/workflows/ci.yml`) runs the tests on every pull request and, on a
`v*` tag push, runs the tests, builds and publishes a release with `winmm.dll` + `azimuth_fm5.ini` + `azimuth_fm6.ini` + `README.txt`
(this file as plain text, made by `tools\md2txt.py`).

## License

MIT, see `LICENSE`. It covers only the code in this repository. Azimuth, Forza Motorsport 5,
Forza Motorsport 6 and Forza Horizon 2 belong to their respective owners and are not included.
