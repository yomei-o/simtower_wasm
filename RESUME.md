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
`shim/src/win32_ne.cpp` parses it in the browser; the build carries nothing
copyrighted. An earlier attempt embedded a 6 MB resource pack built by the
upstream Python tools — that worked, and had to be abandoned for exactly this
reason. Do not put it back.

## State: it is a playable game

`docs/index.html` is the game, and it has been driven end to end in a real
browser, headlessly, by `tools/browser_check.js`:

* Choose a `SIMTOWER.EXE` → 499 resources read → the splash, then SimTower's
  own **New Tower / Load Saved Tower / Quit** chooser.
* **New Tower** gives a tower: the main window with its menu bar and both
  scroll bars, the map window, the info bar (`1st WD/1 Q/1st Year`,
  `Fund $2000000`, `Pop 0`), and the tool palette.
* The menus drop and their items work. The scroll bars scroll, by arrow, by
  page and by dragging the thumb.
* Clicking a facility opens the game's grouped selector; choosing from it and
  clicking in the world **builds**, and the funds go down. The game answers for
  itself when it will not: *"Lobbys are only every 15 floors."*
* The simulation runs — the clock advances and the sky changes with it — at
  23-28 fps at 800x600.
* Sound plays through Web Audio. **Not verified by ear yet**: headless says the
  AudioContext is `running`, which is not the same as hearing it.

All 27 game-core translation units plus `native_main.cpp` compile to wasm; the
shim answers 256 Win32 entry points.

* **Towers save and load.** `win32_files.cpp` draws a chooser out of the same
  control classes as every other dialog, over an IndexedDB-backed `/saves`, so
  what was saved is still there after a reload. Verified in a browser across a
  real reload.

### What is still missing

| | |
|---|---|
| Sound heard | Plays, unheard by anyone so far. |
| The other dialogs | 51 templates; About, Finance and the startup chooser have been opened, the rest have not. |
| Higher ratings | Everything checked so far is a one-star tower. |

## Setting up on another machine

```sh
tools/fetch_upstream.sh upstream          # the pinned fork
tools/build_assets.sh /path/to/SIMTOWER.EXE upstream
emcmake cmake -S . -B build -G Ninja -DUPSTREAM=$PWD/upstream
cmake --build build --target simtower_game --parallel 4
cp docs/simtower_game.html docs/index.html
```

`tools/build_assets.sh` is not optional: it writes
`upstream/port/generated/original_resources.generated.hpp`, without which
nothing configures. It needs Python and the upstream tools.

You need your own `SIMTOWER.EXE` from the 16-bit Windows 3.1 release —
6,566,400 bytes, sha256 starting `2825a3c53f77945c` for the copy used so far.
`SIMTOWER.EX_` off the install disc is KWAJ-compressed and is rejected with a
message saying so; run the installer and use what it writes.

On the machine this was built on, the toolchain is not on `PATH`; `env.sh` (not
committed) supplies it:

```sh
export EMSDK=/c/prog/emsdk/emsdk
export EM_CONFIG=$EMSDK/.emscripten
export PATH="$EMSDK/upstream/emscripten:$EMSDK/node/22.16.0_64bit/bin:$EMSDK/python/3.13.3_64bit:/c/prog/tools:$PATH"
```

Sourcing it puts emsdk's Python first, which has no Pillow — run image
inspection in a shell that has *not* sourced it.

## The three ways to check it

**Natively**, which is the reference. The port builds as an ordinary Windows
program, and that settles any argument about whether a difference is the shim's
fault:

```sh
export PATH=/c/prog/tools/w64devkit/bin:$PATH
cmake -S upstream/port -B build-native -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=/c/prog/tools/w64devkit/bin/g++.exe \
  -DSIMTOWER_WINDRES=/c/prog/tools/w64devkit/bin/windres.exe -DBUILD_TESTING=OFF
cmake --build build-native --target simtower --parallel 4
```

It needs an empty `upstream/original/extracted/MAXIS/SIMTOWER/SIMTOWER.HLP` to
satisfy the resource script, and it uses the same generated header and pack the
wasm build does — which are byte-identical, so a difference between the two is
never the data. `C:\prog\claude\drive.ps1` (not committed) starts it,
focuses it, resizes it, clicks and screenshots. **The desktop here is scaled**:
a screenshot taken by a DPI-unaware process comes back in logical pixels while
SetCursorPos and MoveWindow speak physical ones, so it calls SetProcessDPIAware
first — without that every click lands somewhere else.



**Without a browser**, which is the fast loop:

```sh
cmake --build build --target simtower_game_node --parallel 4
node tools/node_harness.js build/node/simtower_game_node.js frame.png \
     /path/to/SIMTOWER.EXE 8000 click:400,264 wait:4000 dump shot:after.png
```

Actions are `wait:<ms> click:<x>,<y> move:<x>,<y> drag:<x1>,<y1>,<x2>,<y2>
dbl:<x>,<y> key:<name> shot:<file> dump`. `dump` prints the window tree —
class, id, rectangle, style, visibility — which is how most of the bugs below
were found, and far faster than guessing from a picture.

Input goes in through `simtowerInjectMouse`/`simtowerInjectKey`, which call the
very handlers the browser's own listeners call. A second input path would be a
second thing to get wrong.

**With a browser**, which is what the page actually is:

```sh
node tools/browser_check.js docs/index.html /path/to/SIMTOWER.EXE out.png \
     wait:6000 click:400,264 wait:5000
```

It serves `docs/` over HTTP, drives headless Edge over the DevTools protocol,
hands the page's own file input a real executable, clicks with real mouse
events and screenshots. **Chrome will not work on a managed machine**: this one
answers "DevTools remote debugging is disallowed by the system admin", so Edge
is tried first. `SIMTOWER_BROWSER` overrides.

The two disagree, and the disagreement is real: frame timing differs, and a bug
that only showed in the browser (the tool palette missing) was a genuine one.

## Traps already paid for

### The shim's own model

* **A child window's position is its parent's, not the screen's.** Converted on
  creation and on every move; a window that moves takes its children with it.
* **There is no clipping between windows.** One surface, painted in z-order, so
  a window repainting itself paints over anything standing on top of it.
  `invalidate` queues everything above that overlaps, and **`ReleaseDC` does the
  same** — the port draws a great deal straight through a `GetDC` without
  waiting for `WM_PAINT`.
* **Never publish a half-drawn frame.** The map window is redrawn directly on
  every simulation tick, covering the tool palette, which repaints one message
  later. Publishing in between shows a hole. `publishAndYield` waits for the
  pending paints, with a 100 ms deadline so a window that never paints cannot
  stop the picture.
* **`BeginPaint` erases the update rectangle, not the whole client.** The port
  repaints only what it was asked to; erasing everything left the rest blank —
  half the tool palette went grey when one button was pressed.
* **The frame is not a window.** Clicks on the caption, the menu bar and the
  scroll bars are answered before the window sees them: the port reads
  `WM_LBUTTONDOWN` as an attempt to build, so a click on the File menu built
  nothing at a negative y and said "Cannot place item there".
* **The menu popup is an overlay.** The frame is drawn before `WM_PAINT`, so
  anything drawn with it is painted over by the window's own client.
* **`GWL_USERDATA` is -21**, so it is not an offset into the window's extra
  bytes and needs its own case. Dropping it left the command selector without
  its context: it answered nothing and never closed.
* **`TA_UPDATECP` means `TextOut` ignores the position it is given.** The port
  sets it on the info bar, the palettes and the dialogs, then draws every
  string as `TextOut(dc, 0, 0, ...)` after a `MoveToEx`. Reading the pen only
  after drawing put the funds, the population and the date in the corner.
* **`GetDesktopWindow` has to be a real window.** The port sizes the splash and
  centres every dialog against `GetWindowRect(GetDesktopWindow())`; returning
  null measured a screen of zero and put the startup chooser at (-130,-59).
* **`HGDIOBJ` is `void *` in the real SDK**, not a `DECLARE_HANDLE`, which is
  why Windows code writes `DeleteObject(hBrush)` with no cast. `HCURSOR` is a
  typedef of `HICON`, for the same reason.
* **`Sleep` does nothing and `GetMessage` never blocks.** One thread belongs to
  the browser; spinning on it freezes the tab and the reload button with it.

### The game's own expectations

* **The control scope was counted, not guessed.** Across the 51 dialogs: 77
  buttons, every one a plain or default pushbutton; 56 statics, all text; 6
  drop-down lists; 2 list boxes; 2 single-line edits; one vertical scroll bar.
  No check boxes, radio buttons, group boxes or owner-draw anywhere.
* **The port opens one wave device per channel**, and it has two. A shim with a
  single device could only ever play one sound.
* **A wave header must come back when its sound ends**, not when it is
  accepted: the port resets and closes the device on seeing `WHDR_DONE`.
* **`GetAsyncKeyState` is asked whether the mouse button is still down.** The
  grouped command selector only opens while it is, and a drag cannot be told
  from a click without it.
* **SimTower requires four raster capabilities.** `RC_STRETCHBLT` missing from
  `GetDeviceCaps` is what made it warn about the display driver at startup.
* **The port measures a save's DOS basename from the last backslash.** A POSIX
  path has none, so `/saves/TOWER.TDT` measures twelve characters and comes back
  as "That is not a valid filename". It is handed a bare name, and the process
  `chdir`s into the saves directory.
* **ShowWindow must not activate the window it shows.** It is what Windows
  does, and it recurses here: the port answers WM_ACTIVATE by showing the
  auxiliary windows, so activating on show makes the main window and a palette
  hand activation back and forth until the stack runs out. The cost of leaving
  it alone is that the tool palette wears an active caption the real game gives
  to the main window, which is a strip of colour.
* **IDBFS asserts rather than throws where there is no IndexedDB**, and an
  assert aborts the runtime. Ask `typeof indexedDB` first. And `-lidbfs.js` is
  per target: with it missing the runtime keeps a stub that answers "IDBFS is no
  longer included by default" and the mount fails into an ordinary in-memory
  directory, so everything works until the page is reloaded.

### The one change carried against the port

`tools/fetch_upstream.sh` pins the fork's `floor-edges` branch rather than its
main line.  The one commit on it fixes `render_original_floor_edges`, whose
reconstruction of the shared WinG sheet had the fragments' width and height
swapped and took the standard floor edge from BITMAP/1259 - a bank of hotel
rooms - instead of BITMAP/1069, the emergency stairs.  The evidence, in case it
needs revisiting: the fragments are drawn at the floor edge minus 24 and minus
56, which only makes sense as widths; read that way both come out exactly one
story tall; BITMAP/1069 is 48 wide, holding the two 24-wide staircases, and
BITMAP/1001 is 112 wide, holding the two 56-wide canopies; and the port's own
dimension checks on all five source bitmaps pass, so the ids are not shifted.
Reverting is one line: pin 9c2685a again.

### The private resource type names

The port looks resources up by four-character type names — `find("PART", 1000)`
— while a New Executable stores a private type id with the high bit set, and
the public tools call those `TYPE_32513` upwards. SimTower carries its own
Win16 resource-name table (type 15, one entry) holding all eleven private type
names and the five named resources. `tools/name_from_nametable.py` reads it and
fills both into the generated header, so the mapping is read rather than
inferred. `shim/src/win32_ne.cpp` carries the same table for building the pack
— **the two have to agree or the pack comes out empty.**

### Build and deployment

* **The shell is not a source of any target.** `web/shell.html` is baked in at
  link time, so nothing rebuilt when it changed and the page kept its old text
  while the build reported no work to do. It is a `LINK_DEPENDS` now.
* **GitHub Pages defaults to serving the repository root**, then runs Jekyll
  over it and serves a rendered README, which looks exactly like a page whose
  wasm has gone missing. `gh api -X PUT repos/<owner>/<repo>/pages -f
  "source[branch]=main" -f "source[path]=/docs"` — a PUT, not a POST, and check
  the reply instead of discarding it.
* **This emscripten's `SINGLE_FILE` embeds the wasm as a raw byte string** in
  `binaryDecode('...')`, not as base64. Searching the page for `base64` finds
  nothing and suggests the wasm is absent; look for the ` asm` magic instead.
* **`upstream/` in the working tree is a copy, not a symlink.** `ln -s` does not
  make one on Windows, so there are two trees and the build uses whichever
  `-DUPSTREAM` named at configure time.
* **`EM_JS`, never `EM_ASM`,** for anything with a comma in it — a comma splits
  the macro argument, and `createImageData(w, h)` has one.
* **Key events must be listened for on the document.** A canvas cannot take
  focus without a `tabindex`, and `preventDefault` on mousedown stops a click
  from focusing it even then.
* **The canvas element's box must equal the backbuffer size.** Pointer
  coordinates come from the element rect, so any other ratio misplaces every
  click as well as resampling the picture.
* **A DIB's source y is measured from its bottom.** Getting this wrong flips
  the picture in a way that is easy to miss on symmetrical art.
* **The font is baked, and it must be one-bit rasterised**, not an antialiased
  image thresholded afterwards. At nine pixels thresholding loses the stem of a
  `1` and the bar of a `$`. Regenerate with `tools/make_font.py`.
* **No `globalThis.window` stub in the node harness** — emscripten decides it is
  in a browser from that alone and refuses a node-only build. Its `document`
  stub also needs `querySelector`, and the harness must `process.exit`
  explicitly or the main loop keeps the event loop alive and nothing flushes.

## Next

1. **Saving and loading a tower.** The port calls `GetSaveFileNameW` and then
   ordinary file writes. A browser has no file system worth the name, so this
   needs a decision: IDBFS behind the same paths, a download for save and the
   existing file input for load, or a name list in `localStorage`. Whatever it
   is, the port's own code should not have to know.
2. **Hear the sound.** It plays; nobody has listened.
3. **The rest of the dialogs.** 51 templates, and only a few have been opened.
   The controls are all there; what has not been checked is each dialog.

## File map

| | |
|---|---|
| `shim/include/` | `windows.h`, `mmsystem.h`, `commdlg.h` — declarations only, driven by the compiler |
| `shim/src/win32_gdi.cpp` | DCs, objects, drawing, DIB blitting, palettes, DrawText |
| `shim/src/win32_font.cpp` | text from the baked table, and the text alignments |
| `shim/src/win32_font_data.h` | generated — do not edit, see `tools/make_font.py` |
| `shim/src/win32_window.cpp` | windows, messages, painting, z-order, the frame, scroll bars |
| `shim/src/win32_control.cpp` | BUTTON, STATIC, EDIT, LISTBOX, COMBOBOX, SCROLLBAR, and dialog navigation |
| `shim/src/win32_dialog.cpp` | dialog templates, the modal loop, MessageBox |
| `shim/src/win32_menu.cpp` | menus and accelerators |
| `shim/src/win32_audio.cpp` | waveOut over Web Audio |
| `shim/src/win32_ne.cpp` | the executable's resource table, parsed in the browser |
| `shim/src/win32_resource.cpp` | the Win32 resource API over that |
| `shim/src/win32_host.cpp` | canvas, input, presentation, and the harness's way in |
| `shim/src/win32_misc.cpp` | strings, rectangles, time, settings |
| `demo/main.cpp` | the resource viewer that proved the pipeline; not the game |
| `game/entry.cpp` | the game's `main`: wait for the executable, then `WinMain` |
| `tools/node_harness.js` | run the same wasm on the command line, script it, dump a PNG |
| `shim/src/win32_files.cpp` | the save chooser, and the IndexedDB-backed directory under it |
| `tools/browser_check.js` | drive the published page in headless Edge; `js:<expr>` looks inside it |
| `tools/paint_sweep.py` | build a tower with every tool and measure what came out |
