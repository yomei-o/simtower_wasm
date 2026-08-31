# simtower_wasm

Running the **disassembly-backed C++ port of SimTower** in a browser, on
WebAssembly, with no WebGL — the game's GDI drawing is rasterised in software.

### [Live demo](https://yomei-o.github.io/simtower_wasm/) — bring your own `SIMTOWER.EXE`

The page reads the executable you choose and never uploads it.

## What is in this repository

Only this project's own work:

| | |
|---|---|
| `shim/` | a small Win32 for Emscripten — window, message loop, GDI, DIB blitting, palettes, menus, dialogs, `waveOut` |
| `tools/` | fetch the ported source, and turn a `SIMTOWER.EXE` into the resource pack it expects |
| `web/` | the page that hosts the build |

The ported source is **not** vendored. Upstream
([kandowontu/simtower-native-windows-port](https://github.com/kandowontu/simtower-native-windows-port))
states plainly that no licence has been selected for it, so redistributing a
copy from here is not something it permits. Forking on GitHub is permitted, so
`tools/fetch_upstream.sh` pulls a pinned fork at build time and this repository
stays its own.

The original game's resources are not here either, and never will be: they
belong to their rights holders. You supply your own `SIMTOWER.EXE`.

## Why this is worth doing

The port is a function-by-function translation of the real Windows 3.1
executable, so it has the whole game — every facility, the rating progression,
the parking and the cathedral, the original tuning values. Reimplementations
tend to stop before that; the one this project's author previously ported to
WebAssembly ([openskyscraper_wasm](https://github.com/yomei-o/openskyscraper_wasm))
has a fine simulation underneath and a written list of what was never wired up.

The port is also, structurally, a good fit. Its drawing goes through
`SetDIBitsToDevice` and `StretchDIBits` — it hands over a block of pixels and a
palette, which is exactly what a software rasteriser wants, and exactly what
survives having no WebGL. Its resource script turns out to be a single `RCDATA`
blob, so none of Windows' resource compiler is needed.

## Measured scope

Taken from the source rather than guessed at:

- **42,438** lines across the ported translation units.
- **81** Win32 names needed by the game core (`original_*.cpp`) — mostly GDI
  objects, menus and resource lookup.
- The window, the message loop and the dialogs live in the host
  (`native_main.cpp`), which this project replaces rather than shims.
- Dialogs come from memory as `DLGTEMPLATE`s via `DialogBoxIndirectParamW`, 51
  of them, using only the standard control classes.

## Building

    tools/fetch_upstream.sh upstream
    tools/build_assets.sh /path/to/SIMTOWER.EXE upstream

then configure with Emscripten. Verified so far: the asset pipeline produces
499 resources packed into 6,089,216 bytes from the 16-bit Windows 3.1 release.

## Credits

The port, and the disassembly analysis behind it, are the work of
[kandowontu](https://github.com/kandowontu). SimTower is the property of its
rights holders; this project is not affiliated with Maxis or Electronic Arts,
and commercial unavailability does not place anything in the public domain.
