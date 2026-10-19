# Azimuth For FM5

## What we are doing
**Azimuth** (Almost There Games) is an Xbox One -> PC translation layer that only runs Forza Horizon 2 and
FH2 Fast & Furious. It **loads the Xbox One exe into its own process** (custom PE loader) and resolves the
imports to internal HLE implementations (kernelx, combase, WinRT, d3d11_x -> D3D12, XAudio2, XMA via FFmpeg).
Goal: make **Forza Motorsport 5** (title id `7B35D917`, same ForzaTech engine, Nov-2012 XDK) run on it,
without modifying `azimuth.exe` on disk: everything is done in memory by a **`winmm.dll` proxy**.

Current state (2026-10-16): launcher resolution (e.g. 1440p) OK; launcher fps (60) kept in the menus; FM5 passes the locks, starts, plays audio, shows intro/menu and loads the race;
3D scene OK (ESRAM aliasing emulated in the proxy, cont. 19) and the 1st race is completed; end of race, "continue" (FMASK 4x) and garage (4x aliasing, cont. 21) OK; save thumbnails (MSAA resolve, cont. 23) OK; ~40 min without a crash. See `docs/contexto/99_proximos_passos_historico.txt`.

## How the solution works
- **Azimuth locks**: games are identified by SHA-256 (manifest, exe, pinned files) and a constant record
  in `.rdata` per game. The proxy builds an **FM5 record in memory** and repoints the allow list span
  (`0x14029a200`) to `{fh2, fh2 f&f, fm5}`; it also relaxes the single template, TLS, the fixed XG layout and `xg.dll`.
- **winmm.dll** is a static import of `azimuth.exe` and is not a KnownDLL -> it loads before `WinMain`. It patches
  `azimuth.exe` (DllMain) and hooks Azimuth functions. Hook on `loader::apply_patches` (`0x14003f5b0`):
  for FM5 it runs FM5's own patches instead of FH2's. The original `azimuth-tls.dll` is kept.
- **XDK differences** (FM5 = old XDK, Azimuth = FH2's XDK) are resolved in the proxy: device/context
  vtables (context proxy with generated thunks), XG ABI, Draw* with packed arguments,
  DXGIXPresentArray, SHAPE, XAudio2, WinRT stubs, etc.
- Always **check the original bytes** before patching; if they don't match, change nothing.

## Project layout
- `azimuth.exe` (in the root), `data\` (Azimuth DLLs, `data\logs\azimuth.log` + `.dmp`, `data\azimuth.ini`)
- `winmm_proxy\` — proxy source: `fm5_patch.c` (patches/record/hooks/ini), `d3d_fm5.c` (D3D11 bridge),
  `winrt_fm5.c`, `shape_fm5.c`, `debug12_fm5.c`, `gfx_fm5.c`, `shader_fm6.c` (FM6 console DXBC opcodes); `tests\` = proxy tests; `d3d_thunks.S` and `winmm_stubs.S` are **generated**
  (`tools\gen_d3d_thunks.py`, `tools\gen_winmm_stubs.py`).
- `winmm.dll` (root) = installed proxy (delete it = original Azimuth).
- `azimuth_fm5.ini` section `[FM5]` (`SkipIntro`, `SkipPresentationVideo`, `UnlockDLC`, `EnableMarketplace`), `azimuth_fm6.ini` `[FM6]`, `debug.ini` `[Debug]` (`D3D12Debug`, `DrawSkip`, `DrawSkipHashes`;
  dev only, in git but not in the release zip) and `azimuth_fm5.log` (proxy log).
- `game_binaries\FM5\`, `FM6\`, `FH2\` — copied exes/manifest/xg_x.dll per game. Game folders: `E:\Xbox One Games\Forza Motorsport 5`,
  FM6 `E:\Xbox One Games\Monument_1.0.0.44_x64__8wekyb3d8bbwe`.
- `tools\` — Python RE scripts (capstone) and `dec.ps1` (Ghidra headless). `ghidra\` — already analyzed Ghidra projects.
- `docs\contexto\` — accumulated knowledge (see below). `docs\decomp\` — saved decompilations.
- `.github\workflows\ci.yml` — tests on pull requests; on a `v*` tag push, tests + build + release (zip with `winmm.dll`, `azimuth_fm5.ini`, `README.txt`).

## Build, install and test
- Build: `sh winmm_proxy/build.sh` (mingw gcc in `C:\mingw64\bin`; it already runs the generators) -> `winmm_proxy\build\winmm.dll`;
  install by copying to `E:\Azimuth For FM5\winmm.dll`, **overwriting freely**: do NOT create a backup
  (`winmm.prev.dll` or similar); the source is in git and the build is reproducible.
- Proxy tests: `sh winmm_proxy/tests/run_tests.sh [filter]` (`build.sh` already runs them; `SKIP_TESTS=1` skips). When changing the
  proxy, adjust/add the test along with it (expected bytes written in the test, not copied from the table). See `02_ferramentas.txt`.
- In-game test: `./azimuth.exe "--game=E:\\Xbox One Games\\Forza Motorsport 5" --skip-launcher` from Bash (closes by itself on
  error) or via `System.Diagnostics.Process` in PowerShell to get the full exit code (`Start-Process` hung).
- Errors show up as `STOPPED ...` in `data\logs\azimuth.log`; see also `azimuth_fm5.log`.
- Decompile: `tools\dec.ps1 <gproj|gproj_fh2|gproj_fm5|gproj_xg> <azimuth|fh2|fm5|xg> <program> <out.c> 0xVA ... [x:0xVA]`.
- Python scripts: `pip install --user capstone` (and `minidump` for `dmp_*.py`); run without `-I`.

## Conventions
- Addresses: `0x14xxxxxxx` = VA in azimuth.exe; "RVA"/`game+0x...` = in the FM5 exe (ImageBase 0x140000000).
  azimuth.exe: `.text` file off = RVA - 0xc00; `.rdata` VA = file off + 0x140001400.
- `azimuth.pipelines` is just the shader cache — **not a lock**, ignore it.
- Nothing may stay only in temp/scratchpad: scripts in `tools\`, decomps in `docs\decomp\`, Ghidra projects in `ghidra\`.
- Local only (`.gitignore`), never in the public repository: `docs\` (whole folder), `data\` (Azimuth's folder),
  `car_model_report\` (internal research), `tools\sessoes_antigas\`, `*.pkl`, `game_binaries\`, `ghidra\`, `azimuth.exe`.
- At the end of each step: record the findings in the topic's `docs\contexto\` file (section "PASSO 8 (cont. N)"
  with RESULTADO and the next error) and update `99_proximos_passos_historico.txt`; then **commit** the code
  (docs stay local). Large/generated files go in `.gitignore`, and so does any new
  file/folder of internal research, derived from the game/Azimuth binaries, or belonging to Azimuth: never `git add` those.
- **Never commit code from Azimuth or Forza** (MIT covers only our code): no decompiled/disassembled snippets, Ghidra
  output or copied data in source, comments, tests or docs. Allowed: addresses/offsets, short byte signatures checked
  before patching, hashes, and our own prose descriptions.
- Commit messages: **English, Conventional Commits** (`type(scope): summary`, e.g. `fix(d3d): ...`, `feat(ini): ...`,
  `ci: ...`, `docs: ...`, `test: ...`, `chore: ...`; scopes like `d3d`, `xg`, `audio`, `winrt`, `patch`, `ini`, `tests`).
- Push only `main`. The local branch `historico-local` has the old history (with decomps) and must never be pushed.
  Once pushed, a commit is never amended/rewritten: fixes go in new commits.
- Don't take a desktop screenshot when the game is not in the foreground.

## Reference files (`docs\contexto\`)
Read only what is relevant to the task:

| File | When to consult |
|---|---|
| `01_visao_geral_arquivos_arquitetura.txt` | goal, title ids, project files, azimuth.exe sections, Azimuth architecture |
| `02_ferramentas.txt` | Ghidra/JDK/REA, usage of the `tools\` scripts, proxy tests, CI, where decomps and old sessions are |
| `03_trava_allowlist_manifesto_pasta.txt` | allow list, manifest hash lookup, field-by-field validation, folder validation |
| `04_fm5_dados_e_imports.txt` | FM5 hashes/manifest, FM5 x Azimuth import comparison |
| `05_loader_iat_injecao_apply_patches.txt` | STEPS 2/3/3b: import resolver, IAT count, TLS, injection point, ported apply_patches sites |
| `06_registro_do_jogo.txt` | STEP 4: full record layout (0x350) and FM5 values |
| `07_idiomas_e_fala.txt` | STEP 5: language table and speech language patch |
| `08_xg.txt` | STEP 6 + cont. 2: xg_x.dll loader, descriptor ABI, XGTextureAddressComputer |
| `09_proxy_winmm.txt` | STEP 7: proxy patches in azimuth.exe, apply_patches hook, Live table |
| `10_d3d_device_contexto_vtables.txt` | STEP 8 / cont. 4: D3D11CreateDevice, FM5 device/context/resource vtables, context proxy, SetFast*, DMA, private data |
| `11_d3d_msaa_fmask_recursos.txt` | cont. 3: MSAA 2x, FMASK, Azimuth CreateTexture2D/SRV, resource object |
| `12_audio_xaudio2_apu_shape.txt` | cont. 5/8/10: XAudio2Create, risk of MSVC statics on game threads, APU heap, SHAPE opcodes |
| `13_winrt_voz_templates.txt` | cont. 6/7: WinRT activation (SpeechRecognizer stub), GetTemplateByName |
| `14_present_draws_telapreta_resolve.txt` | cont. 9/11/13/14: DXGIXPresentArray, IASetIndexBuffer, black screen (packed Draw*), D3D12 debug layer, ResolveSubresource |
| `15_opcoes_ini_e_videos.txt` | cont. 12/15 + STEP 11: azimuth_fm5.ini, SkipIntro, SkipPresentationVideo, UnlockDLC (DLC ownership/ContentOffers), EnableMarketplace, FM5 command line options |
| `16_opcoes_graficas.txt` | STEP 9: launcher graphics options (resolution, AA, fps, presets) in FH2 x FM5, porting plan |
| `17_fm6.txt` | FM6 (`64F9397E`, 1.0.0.44, OS "era"): manifest, record, section-count lock, missing SOFTWARE hive |
| `99_proximos_passos_historico.txt` | what has been done and **the current next step** |
