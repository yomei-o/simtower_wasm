# RESUME

Where this is, what to do next, and the things that cost time to find out.

## What this project is

Running **kandowontu/simtower-native-windows-port** — a disassembly-backed C++
port of the real Windows 3.1 SimTower — in a browser on WebAssembly, with no
WebGL. This repository holds a small Win32 for Emscripten and nothing else.

### The constraint that shapes everything

**Do not vendor the upstream source into this repository.** Upstream states in
`DISTRIBUTION.md` that no licence has been selected for it, so the default
applies and there is no permission to redistribute a copy. Forking on GitHub is
permitted, so `tools/fetch_upstream.sh` pulls a pinned fork
(`yomei-o/simtower-native-windows-port`, commit `9c2685a`) at build time.

**Do not commit or serve the original game's resources either.** They belong to
their rights holders. The page asks the player for their own `SIMTOWER.EXE` and
`shim/src/win32_ne.cpp` parses it in the browser; the build output is 387 KB with
nothing copyrighted in it. An earlier attempt embedded a 6 MB resource pack built
by the upstream Python tools — that worked, and had to be abandoned for exactly
this reason. Do not put it back.

## State

Working and verified:

* All **27 game-core translation units, 42,438 lines**, compile clean to wasm.
* The shim covers windows, messages, painting, GDI, DIB blitting, palettes,
  menus, resources, strings, time, and a baked bitmap font.
* **The whole path renders.** `tools/node_harness.js` runs the same wasm on the
  command line with a stubbed canvas and writes the presented frame to a PNG.
  It produces SimTower's own title screen at 640x480 with the right palette,
  inside Windows 3.1 chrome, with a working menu bar and legible text.
* 499 resources parsed out of the executable, 250 bitmaps decoded — the same
  count the upstream Python tools report, which is the check that reading the NE
  directly agrees with building a pack.

Not yet confirmed: **nobody has seen the browser page work.** The node harness
proves the renderer; the page itself was last reported as showing text and no
picture, and two causes for that were fixed and deployed without confirmation —
a canvas with no size in the markup (invisible, and indistinguishable from a
failure) and `printErr` overwriting the status line (an error read as a status).
First thing next session: open <https://yomei-o.github.io/simtower_wasm/>, pick
`SIMTOWER.EXE`, and read the log area under the canvas.

## Setting up on another machine

```sh
tools/fetch_upstream.sh upstream          # the pinned fork
emcmake cmake -S . -B build -G Ninja -DUPSTREAM=$PWD/upstream
cmake --build build --parallel 3          # docs/simtower.html
cp docs/simtower.html docs/index.html
```

You need your own `SIMTOWER.EXE` from the 16-bit Windows 3.1 release —
6,566,400 bytes, sha256 starting `2825a3c53f77945c` for the copy used so far.
`SIMTOWER.EX_` off the install disc is KWAJ-compressed and is rejected with a
message saying so; run the installer and use what it writes.

Checking the renderer without a browser, which is the fastest loop by a long way:

```sh
cmake --build build --target simtower_node --parallel 3
node tools/node_harness.js build/node/simtower_node.js frame.png /path/to/SIMTOWER.EXE
```

## The game's own host: building, linking, and running as far as the UI resources

`native_main.cpp` compiles and links, and `WinMain` runs. It gets through
startup capability checks, the resource pack, the PART table, the three YEN
tables and all six TABL/TABM rating tables, and stops in
`original_ui.cpp`'s reader with "Truncated original Win16 UI resource" - which is
that reader running off the end of a resource, most likely an ICON or CURSOR
reached through a GROUP directory whose ids are being read wrongly. That is the
next thing to chase; `tools/node_harness.js` shows it in one run.

Three things made this possible and are worth not undoing:

* **Asyncify.** The port's dispatcher is `PeekMessage`, and simulate when there
  is nothing to dispatch. `shim/src/win32_window.cpp` makes `PeekMessage` the
  yield point: it publishes the frame and hands the thread back, rate-limited to
  once every 16 ms so there is still time to simulate in. Upstream is explicit
  that the recovered dispatcher stays unchanged, and this needs no patch against
  it.
* **WM_PAINT is generated, not queued** - the same as Windows - so the port's own
  `DispatchMessage` delivers it instead of the shim painting behind its back.
* **The resource pack is synthesised in memory** from the player's executable,
  laid out exactly as `build_resource_pack.py` would. 499 of 499 descriptors,
  6,089,216 bytes, matching the Python tool byte for byte. Nothing copyrighted is
  committed or served, and `OriginalResources::from_current_module` works
  unchanged.

### The private resource type names

The one real piece of reverse engineering so far, and the reason the game got
past its first table. The port looks resources up by four-character type names -
`find("PART", 1000)`, `find("WAVE", id)` - while the generated table calls those
types `TYPE_32513` upwards, so every one of those lookups came back empty.

Neither side is wrong: in a New Executable a type id with the high bit set is an
integer, and SimTower's private types genuinely are 0xFF01..0xFF0B. The names
live in the original's own source and the port carries them; the public tools
have no mapping between the two. `tools/name_private_types.py` is that mapping,
applied to the generated header by `tools/build_assets.sh`, and
`shim/src/win32_ne.cpp` carries the same table for building the pack - **the two
have to agree or the pack comes out empty.**

It was derived from the data, and three entries were wrong when they were
derived from id ranges instead. ALRT was pinned by reading exactly as
`parse_original_alert` expects, TABL and TABM by which types parse as big-endian
word tables at the ids the port asks for, WAVE by every entry beginning
"RIFF....WAVE". Do not adjust an entry without that kind of evidence.

## Next after that

`upstream/port/src/native_main.cpp` (10,071 lines) is the port's Win32 host —
`WinMain`, the window procedure, the dispatcher. Getting it to build is what
turns this from a resource viewer into the game.

The harvest loop below is what got the core, and then the host, from thousands
of errors to zero. Keep using it:

```sh
for f in upstream/port/src/native_main.cpp; do
  em++ -std=c++20 -fsyntax-only -ferror-limit=0 -DUNICODE -D_UNICODE \
       -DWIN32_LEAN_AND_MEAN -DNOMINMAX -Ishim/include \
       -Iupstream/port/src -Iupstream/port/generated "$f" 2>&1
done | grep -oE "(unknown type name|use of undeclared identifier|no member named) '[A-Za-z_0-9]+'" \
     | sed "s/.*'\(.*\)'/\1/" | sort -u
```

The names outstanding as of this handoff: `AdjustWindowRectEx` `AnimatePalette`
`BITSPIXEL` `ClipCursor` `DT_END_ELLIPSIS` `DT_NOPREFIX` `EnumChildWindows`
`FILE_ATTRIBUTE_DIRECTORY` `FreeLibrary` `GetFileAttributesW`
`GetModuleFileNameA/W` `GetProcAddress` `GetProfileStringA` `GetWindowsDirectoryW`
`HELP_CONTENTS` `IDC_SIZENS` `INVALID_FILE_ATTRIBUTES` `LoadLibraryW`
`MEMORYSTATUSEX` `MulDiv` `OFN_ENABLEHOOK` `RASTERCAPS` `RASTERIZER_STATUS`
`SM_CXDLGFRAME` `SM_CYDLGFRAME` `SM_SWAPBUTTON` `UpdateColors` `WAVEOUTCAPSW`
`WA_INACTIVE` `WM_APP` `WM_NCDESTROY` `lstrcmpiW`, plus five
`GetPrivateProfileIntW` calls that pass a `wchar_t[128]` where a `const char *`
is expected — check whether those want the A form.

After it compiles, the pieces still missing to actually run it:

1. **The dispatcher's own loop.** It polls with `PeekMessage`. Under Emscripten a
   loop inside `WinMain` never yields, so either drive it from
   `emscripten_set_main_loop` or build with Asyncify. Decide this deliberately;
   it is the difference between a game and a locked tab.
2. **A dialog manager.** 51 dialogs arrive as `DLGTEMPLATE`s in memory via
   `DialogBoxIndirectParamW`, using only the standard control classes.
   `shim/src/win32_menu.cpp` currently answers their API without drawing them,
   and `MessageBox` reports to stderr and returns the default button.
3. **`waveOut`.** Declared in `shim/include/mmsystem.h`, not implemented.

## Traps already paid for

* **`upstream/` in the working tree is a copy, not a symlink.** `ln -s` does not
  make one on Windows, so there are two trees and the build uses whichever
  `-DUPSTREAM` named at configure time. A rename applied to the wrong one looks
  like a build that ignores your change. Check `readlink upstream` before
  believing "ninja: no work to do".

* **`HGDIOBJ` is `void *` in the real SDK**, not a `DECLARE_HANDLE`. That is why
  Windows code writes `DeleteObject(hBrush)` with no cast, and it accounted for
  eleven errors on its own. `HCURSOR` is a typedef of `HICON`, for the same
  reason.
* **`EM_JS`, never `EM_ASM`,** for anything with a comma in it — a comma splits
  the macro argument, and `createImageData(w, h)` has one.
* **Key events must be listened for on the document.** A canvas cannot take
  focus without a `tabindex`, and `preventDefault` on mousedown stops a click
  from focusing it even then. The OpenSkyscraper port lost a session to this.
* **`Sleep` does nothing and `GetMessage` never blocks.** One thread belongs to
  the browser; spinning on it freezes the tab and the reload button with it.
* **The canvas element's box must equal the backbuffer size.** Pointer
  coordinates come from the element rect, so any other ratio misplaces every
  click as well as resampling the picture.
* **A DIB's source y is measured from its bottom.** Getting this wrong flips the
  picture in a way that is easy to miss on symmetrical art.
* **The font is baked, and it must be one-bit rasterised**, not an antialiased
  image thresholded afterwards. At nine pixels thresholding loses the stem of a
  `1` and the bar of a `$`. Regenerate with `tools/make_font.py`; proof-read by
  drawing the table offline, not by looking at a browser.
* **`build_resource_pack.py` wants the raw extraction as its asset directory,**
  not the catalogued one, because the catalog names resources by raw filename.
  Only relevant if you go back to packs, which you should not.
* **No `globalThis.window` stub in the node harness** — emscripten decides it is
  in a browser from that alone and refuses a node-only build. Its `document`
  stub also needs `querySelector`, and the harness must `process.exit`
  explicitly or the main loop keeps the event loop alive and nothing flushes.

## File map

| | |
|---|---|
| `shim/include/` | `windows.h`, `mmsystem.h`, `commdlg.h` — declarations only, driven by the compiler |
| `shim/src/win32_gdi.cpp` | DCs, objects, drawing, DIB blitting, palettes |
| `shim/src/win32_font.cpp` | text from the baked table |
| `shim/src/win32_font_data.h` | generated — do not edit, see `tools/make_font.py` |
| `shim/src/win32_window.cpp` | windows, messages, painting, the Windows 3.1 frame |
| `shim/src/win32_menu.cpp` | menus, and the dialog API without a dialog manager |
| `shim/src/win32_ne.cpp` | the executable's resource table, parsed in the browser |
| `shim/src/win32_resource.cpp` | the Win32 resource API over that |
| `shim/src/win32_host.cpp` | canvas, input, presentation |
| `shim/src/win32_misc.cpp` | strings, rectangles, time, settings |
| `demo/main.cpp` | the resource viewer that proves the pipeline; not the game |
| `tools/node_harness.js` | run the same wasm on the command line, dump a PNG |
