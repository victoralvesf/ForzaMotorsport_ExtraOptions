# Forza Motorsport Extra Options

## What we are doing
**Azimuth** (Almost There Games) is an Xbox One -> PC translation layer. Since **1.0.2** it officially runs
**Forza Motorsport 5** (title id `7B35D917`, package 1.0.0.20) and **Forza Motorsport 6** (`64F9397E`, 1.0.0.44).
This project is a small **extra options mod** for those two games on top of the official Azimuth: a proxy
`winmm.dll` (C++20) that applies optional, byte-checked patches to the game image in memory. Nothing on disk is
modified; deleting `winmm.dll` restores the plain Azimuth.

The old compatibility layer (allow list, game record, XDK/D3D11.X/XG/SHAPE/WinRT bridges, launcher options...) was
removed in `chore!: remove the compatibility layer`; it lives only in the git history. Migration plan and status:
`MIGRATION.md` (local). See `docs/contexto/99_proximos_passos.txt` for the current step.

## How it works
- `azimuth.exe` imports `winmm.dll` statically and it is not a KnownDLL, so our copy next to the exe loads before
  `WinMain`. `src/proxy/` forwards every winmm export to System32 (`winmm_exports.inc` X-macro + `winmm.def`).
- **Injection point:** IAT hook (found by import **name**, never by address) of `kernel32!FlushInstructionCache` in
  `azimuth.exe`. Azimuth calls it on the whole mapped game image after its own patches, TLS and imports and before
  the game runs. The filter needs a PE64 at the base with `SizeOfImage == dwSize`; the game is identified by
  `TimeDateStamp`/`SizeOfImage` (FM5 `0x53d8d1f2`/`0x279d000`, FM6 `0x58afb1cd`/`0x1fc8000`). Options are applied
  once per process.
- **No fixed `azimuth.exe` addresses and no exe hash:** the mod must work on Azimuth 1.0.2 and later. The log only
  records the VERSIONINFO version and warns below 1.0.2.
- **An option is data:** `core::patch::Option{key, sites}`; each `Site{rva, expect, offset, write}` has the
  expected bytes in hex (`??` = any byte, `|` = alternative forms, e.g. original | Azimuth's own patch). Every site
  of an option is checked before anything is written (all or nothing); a mismatch leaves the option off and logs
  why. Code only exists for hooks (FM6 dashboard font fix, always on, `apply_fixes`).
- **Launcher (`src/launcher/`):** adds section 4 "Extra Options" to the settings page of FM5/FM6 in Azimuth's
  launcher (Off/On row per option with a `label`; a change writes the ini at once). Sites found by byte signatures in
  `azimuth.exe` (`core::scan`, unique match), all checked before writing; mismatch = no section, ini still works.
  Rows/strings/vectors use MSVC layouts on the process heap (Azimuth frees them). Analysis:
  `docs\contexto\09_launcher_extra_options.txt`.
- Always **check the original bytes** before patching; if they don't match, change nothing.

## Project layout
- `src/main.cpp` (DllMain, log, version, installs the loader watch and the launcher section), `src/proxy/` (winmm
  forwarding), `src/core/` (log, pe, version, ini, patch, scan, hook, loader_watch, options, `game.hpp` =
  `game::Module`), `src/launcher/` (Extra Options section of Azimuth's launcher),
  `src/games/fm5/`, `src/games/fm6/` (+ `font.cpp`), `src/games/registry.*` (list of games).
- **Game separation:** each game is its own static library that only links `core`; include paths keep one game
  from seeing the other, and the `include_isolation` test rejects `../` includes. A fix for one game goes in its
  folder only.
- `tests/` (doctest): `expected.hpp` = hand-written bytes per option (never copied from the tables), fake image
  tests, real exe tests (skipped without `game_binaries\`), `check_imports.cmake`, `check_includes.cmake`.
- `motorsport_extraoptions.ini` `[GLOBAL]` (`DisableLog`: 1 = no log; ini only, not in the launcher), `[FM5]` (`SkipIntro`, `SkipPresentationVideo`, `UnlockDLC`, `EnableMarketplace`,
  `UnlockMenuFrameRate`) and `[FM6]` (`SkipIntro`, `SkipPresentationVideo`, `UnlockDLC`,
  `UnlockMenuFrameRate`); a missing key = off.
  Log: `fm_extra_options.log` next to `azimuth.exe` (recreated each run).
- `azimuth.exe` (root, local), `data\` (Azimuth's folder: `data\logs\azimuth.log`).
- `game_binaries\FM5\`, `FM6\`, `AZIMUTH\` — copied exes per game. Game folders: `E:\Xbox One Games\Forza Motorsport 5`,
  FM6 `E:\Xbox One Games\Monument_1.0.0.44_x64__8wekyb3d8bbwe`.
- `tools\dec.ps1` + `tools\ghidra_scripts\` (Ghidra headless, to find new options). `ghidra\` — analyzed projects
  (`gproj_az102` = Azimuth 1.0.2).
- `.github\workflows\ci.yml` — cmake/ctest on pull requests; on a `v*` tag push, tests + build + release
  (`ForzaMotorsport_ExtraOptions-<tag>.zip` with `winmm.dll`, `motorsport_extraoptions.ini`, `README.txt` made from `README.md` by
  `tools\md2txt\`, a small C++ tool built by CMake, never shipped; release text = the `## <tag>` section of
  `CHANGELOG.md`). `.claude\skills\release\`.

## Build, install and test
- Git Bash with `/c/mingw64/bin` in `PATH` (g++ 15.2, cmake, ninja):
  `cmake --preset default && cmake --build --preset default && ctest --preset default` -> `build\winmm.dll`.
  Flags: C++20, `-fno-exceptions -fno-rtti`, fully static; `winmm.dll` may only import kernel32/user32/UCRT
  (`winmm_imports` test).
- Install by copying `build\winmm.dll` to `E:\Azimuth For FM5\winmm.dll`, **overwriting freely**: no backup.
- When changing an option, adjust/add the test with it (bytes written in `tests/expected.hpp`, read from the real
  exe, not copied from the table).
- **Adding an option:** sites + launcher `label` in `src/games/<game>/<game>.cpp` + bytes in `tests/expected.hpp` +
  key and comment in the `.ini` + `README.md` + `CHANGELOG.md` (`## Unreleased`).
- In-game test (**only when the user orders it in the current chat**): `./azimuth.exe "--game=E:\\Xbox One Games\\Forza Motorsport 5" --skip-launcher`.
  Check `fm_extra_options.log` (every site applied/mismatch) and `data\logs\azimuth.log` (`STOPPED ...`).
- Decompile: `tools\dec.ps1 <gproj> <program> <out.c> 0xVA ...` (see `02_ferramentas` in `docs\contexto\legado\`).

## Conventions
- Addresses: `rva`/`game+0x...` = in the game exe (ImageBase 0x140000000).
- Nothing may stay only in temp/scratchpad: scripts in `tools\`, decomps in `docs\decomp\`, Ghidra projects in `ghidra\`.
- Local only (`.gitignore`), never in the public repository: `docs\` (whole folder), `data\`, `car_model_report\`,
  `tools\sessoes_antigas\`, `*.pkl`, `game_binaries\`, `ghidra\`, `azimuth.exe`, `MIGRATION.md`.
- At the end of each step: record the findings in `docs\contexto\` (numbered topic file with RESULTADO and the
  next step), update `99_proximos_passos.txt` (open items only) and `MIGRATION.md`; then **commit** the code
  (docs stay local). Never `git add` internal research or anything derived from the game/Azimuth binaries.
- **Never commit code from Azimuth or Forza** (MIT covers only our code): no decompiled/disassembled snippets, Ghidra
  output or copied data in source, comments, tests or docs. Allowed: addresses/offsets, short byte signatures checked
  before patching, hashes, and our own prose descriptions.
- Commit messages: **English, Conventional Commits** (`type(scope): summary`; scopes like `core`, `patch`, `fm5`,
  `fm6`, `ini`, `tests`, `ci`, `docs`).
- Push only `main`. The local branch `historico-local` has the old history (with decomps) and must never be pushed.
  Once pushed, a commit is never amended/rewritten: fixes go in new commits.
- Don't take a desktop screenshot when the game is not in the foreground.

## Reference files (`docs\contexto\`)
Read only what is relevant to the task:

| File | When to consult |
|---|---|
| `01_azimuth_oficial.txt` | Azimuth 1.0.2: how it loads the game, injection point, its DLC patch, sites that overlap |
| `02_opcoes_extras.txt` | sites/bytes of every option (from the old proxy) |
| `03_esqueleto_cpp.txt` | CMake layout, libraries, isolation, flags |
| `04_implementacao.txt` | core (patch, hook, loader_watch, options), FM5/FM6 tables, font fix |
| `05_testes.txt` | tests per option, fake image, real exes |
| `06_docs_ci.txt` | docs, CI, release zip, ini headers (migration phase 5) |
| `99_proximos_passos.txt` | **the current next step** and open items only |
| `legado\` | the old compatibility layer (steps 1-119); only if needed |
