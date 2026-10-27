# Architecture

This document explains how the Extra Options mod works. It describes the design in our own words and contains no
code from Azimuth or from the games.

## Background

Azimuth (Almost There Games) is an Xbox One -> PC translation layer. It loads the original Xbox One executable into
its own process with its own PE loader and resolves the game's imports to its own implementations of the console
libraries. Since version 1.0.2 it officially runs Forza Motorsport 5 (package 1.0.0.20) and Forza Motorsport 6
(package 1.0.0.44).

Versions 1.x of this project made those games run on an older Azimuth that only supported Forza Horizon 2. That
compatibility layer is gone: the official Azimuth does all of it. What is left are a few quality of life options
per game (skip the intro videos, unlock the DLC already in the game, ...) and a fix for a bug in the game itself.

## Why a `winmm.dll` proxy

`azimuth.exe` imports `winmm.dll` statically, and `winmm.dll` is not a Windows "KnownDLL". Windows therefore loads
the copy that sits next to the executable first, before `WinMain` runs. That gives the mod a way to run inside the
Azimuth process without modifying any file on disk.

The proxy forwards every `winmm.dll` export to the system copy in `System32` (`src/proxy/`: one X-macro list of the
exports, committed together with the matching `.def`), so Azimuth's own use of `winmm` keeps working. Deleting
`winmm.dll` restores the plain Azimuth.

## Injection point

The options have to be written after Azimuth has mapped the game and applied its own patches, and before the game's
code runs. Azimuth flushes the instruction cache over the whole mapped image exactly at that moment: after mapping
the sections, patching, setting up TLS and resolving imports, right before it starts the game.

At startup the proxy replaces the `kernel32!FlushInstructionCache` entry in `azimuth.exe`'s import address table
with its own function. The entry is found by name in the import directory, never by address, so the mod does not
depend on a specific build of Azimuth. On each call the hook checks whether the flushed range is a whole PE image:

- a 64-bit PE header at the base address, and
- `SizeOfImage` equal to the flushed size.

The image is then identified by its `TimeDateStamp` and `SizeOfImage`. If it is one of the supported games, its
options are applied once, and the original `FlushInstructionCache` runs afterwards. Anything else is passed through
untouched.

The mod does not check a hash of `azimuth.exe`. It logs Azimuth's version (from the exe's version resource) and warns
when it is older than 1.0.2. What protects against a wrong game or Azimuth version is the byte check of every site,
below.

## Verified patches

An option is data, not code: a key in the game's `.ini` and a list of sites. Each site has:

- the RVA in the game exe;
- the bytes expected there, in hex, with `??` for a byte that may vary and `|` between alternative forms (for
  example, the game's original code or the same code after Azimuth's own patch);
- the bytes to write, at an offset inside the checked range (or nothing, for a site that is only checked).

An option is applied all or nothing. Every site is checked first (inside the image and readable); only when all of
them match are the new bytes written (`VirtualProtect`, copy, restore the protection, flush). If one site does not
match, nothing of that option is written and the log says which site and what it found. A game update or a new
Azimuth that touches the same code therefore turns the option off instead of corrupting it.

## Options

| Game | Option | What it does |
|---|---|---|
| FM5 | `SkipIntro` | the game sees its "disable splash intro" switch as on (3 checks) |
| FM5 | `SkipPresentationVideo` | the welcome screen jumps straight to the callback that runs after the presentation video |
| FM5 | `UnlockDLC` | the entitlement check returns "owned" for every offer, and the purchase check of the content takes "owned" |
| FM5 | `EnableMarketplace` | the default of the Marketplace switch (normally set by the server config) becomes on |
| FM6 | `SkipIntro` | the "disable splash intro" checks fall through as on, and the logo parade is skipped |
| FM6 | `SkipPresentationVideo` | the career intro video name stays empty (presentation video and expansion intros) |
| FM6 | `UnlockDLC` | same design as FM5 |

**DLC and Azimuth 1.0.2.** Azimuth 1.0.2 has its own DLC support based on the content packages present in the game
folder. It replaces the entitlement call inside the purchase check with a call to its own function. `UnlockDLC`
accepts both forms of that site (the original call, or Azimuth's replacement) and turns it into "owned" in either
case; the entitlement function itself is patched too, for its other callers (Marketplace UI, FM6 expansions).

## FM6 dashboard font fix

In Brazilian Portuguese, FM6 formats lap times with apostrophes (`00'12,977''`), but the fonts of the digital
dashboards have no apostrophe glyph, so the times show boxes. This is a bug of the game, not of Azimuth, and the fix
is always on.

The fix hooks the game's text measure function (its first 15 bytes are checked and moved to a trampoline). Before
the original runs, if the font has no apostrophe glyph, the text is rewritten in place: `''` is removed and `'`
becomes `:`, so the time shows as in English (`00:12,977`), the format the dashboards were laid out for. The draw
that follows reads the same buffer.

## Source layout

```
src/main.cpp            DllMain: log, real winmm, Azimuth version, installs the loader watch
src/proxy/              winmm forwarding (export list + .def)
src/core/               log, pe, version, ini, patch (checked sites), hook (trampoline),
                        loader_watch (FlushInstructionCache), options, game.hpp (game::Module)
src/games/fm5/          FM5 option table
src/games/fm6/          FM6 option table + dashboard font fix
src/games/registry.*    list of supported games, lookup by PE header
tests/                  doctest unit tests and CMake checks
```

Each game is its own static library that only links `core`. A game's include path does not contain the other
game's folder, and a test rejects relative includes, so one game's code cannot reach the other's.

## Build and tests

CMake + Ninja with the mingw-w64 g++ (C++20, no exceptions or RTTI, statically linked):

```
cmake --preset default
cmake --build --preset default
ctest --preset default
```

The tests cover the patch engine on fake buffers (match, mismatch, all or nothing, alternative forms, sites at the
end of a section), the hook and trampoline, the ini parser, the IAT lookup, the font fix, and every option against
bytes written by hand in `tests/expected.hpp`. When the real game exes are present locally they are mapped like
Azimuth does and every option is applied to them, in the original form and after Azimuth's DLC patch. Two CMake
checks make sure `winmm.dll` imports only system DLLs and that no source uses a relative include.

GitHub Actions runs the tests on every pull request and, on a `v*` tag, builds and publishes the release zip.

## What is not in this repository

- Azimuth and the games, or anything taken from them: no decompiled or disassembled code. The source only has
  addresses, short byte signatures checked before patching, and our own descriptions.
- The reverse engineering notes and tools output (local only).
