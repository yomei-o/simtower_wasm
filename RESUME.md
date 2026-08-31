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

## Next task: the game's own host

`upstream/port/src/native_main.cpp` (10,071 lines) is the port's Win32 host —
`WinMain`, the window procedure, the dispatcher. Getting it to build is what
turns this from a resource viewer into the game.

Measured, not estimated: **105 errors, about 50 more declarations needed.** The
method that got the core from thousands of errors to zero is mechanical and works
here too:

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
