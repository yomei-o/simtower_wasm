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
(`yomei-o/simtower-native-windows-port`, branch `floor-edges`) at build time.
The pin moves every time that fork gains a commit - it is one line in
`tools/fetch_upstream.sh` - and a build that forgets to move it silently keeps
the old behaviour.

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

* **Measured, not eyeballed.** `tools/paint_sweep.py` builds a tower with every
  tool at five columns across three floors and looks at it scrolled up and
  down; `tools/menu_sweep.py` walks the menus. The world region reports
  0.0-0.3% white at every step, and the only white left in it is the
  construction preview's own outline. Every Windows-menu toggle works, and the
  Finance window and About both draw in full.
* **The page remembers the executable.** The player's own SIMTOWER.EXE is kept
  in the same browser-private directory the towers are, so a second visit
  starts the game with no file to find. There is a button to forget it.
* **60 fps at 800x600**, and the page is 2.0 MB. Both came from setting a build
  type: nothing ever did, so every build until now was -O0, for a program that
  composes 276,000 pixels of world in C++ and blits them every frame.
* **Two and a half minutes of play in a browser**: 36 fps at 800x600, the clock
  hand turning, nothing white, nothing lost.

### What is still missing

| | |
|---|---|
| ~~Sound heard~~ | **Heard, player-confirmed** (build ca1d245). Three separate faults stacked: the suspended-context autoplay policy (resume on next gesture), a source never connected to ctx.destination (started, counted, inaudible), and - the deep one - the shim almost never sent WM_ACTIVATE, so after the first dialog closed the game's idle loop read a false activation latch and deactivated its own mixer for good: exactly one buffer was ever submitted. Activation now announces itself Windows-style; submissions went to one per construction sound. |
| The other dialogs | 51 templates; the startup chooser, Finance, About, the message boxes and the command selector have been opened. The rest arrive with events during play. |
| Long status messages overflow | The info bar's status field is 262 pixels and the port does not clip to it, so a message wider than that runs under the Fund panel. The original fits because its font is narrower than the baked one; there is nothing to clip without clipping the port's own drawing. |
| A tower with people in it | Everything driven so far has a population of zero. Offices and hotel rooms build and stay empty; tenants move in over game days, and only where transport reaches them. Nothing has yet watched a person walk, queue, or ride. The digit debug keys remove the rating wall, so this is now a matter of playing long enough rather than of reaching two stars. |

## Setting up on another machine

```sh
tools/fetch_upstream.sh upstream          # the pinned fork
tools/build_assets.sh /path/to/SIMTOWER.EXE upstream
emcmake cmake -S . -B build -G Ninja -DUPSTREAM=$PWD/upstream
cmake --build build --target simtower_game --parallel 4
cp docs/simtower_game.html docs/index.html
```

This has been run from a fresh clone: those five lines produce a page that is
**byte-identical** to the one deployed, so the repository is self-contained and
the pinned fork is the fork the build actually gets.

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

**Natively**, which is the reference.  Confirmed working on the second
machine too (w64devkit at `C:\prog\w64devkit`): builds and starts to the
New/Load/Quit chooser. The port builds as an ordinary Windows
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

Actions are `wait:<ms> click:<x>,<y> move:<x>,<y> press:<x>,<y>
release:<x>,<y> drag:<x1>,<y1>,<x2>,<y2> dbl:<x>,<y> key:<name>
mods:ctrl+shift shot:<file> dump putfile:<from>|<to> getfile:<from>|<to>`.
`mods:` holds Shift and Control across the actions after it, which is the only
way to reach the two- and three-story lobby or a Shift replacement. `dump` prints the window tree —
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

**A DevTools connection left silent stops answering.** A long `wait:` used to
end the run: the next screenshot waited for a reply that could not come, and
nothing rejected it, so it read exactly like the game hanging. Long waits go in
ten-second slices with a ping between them, and a closed socket now rejects
what was outstanding. The command-line harness is the control here - it ran the
same session to completion while the browser one looked stuck.

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
* **IDBFS restores the directory asynchronously, and the restore replaces what
  is in it.** Anything written before it finishes is thrown away. The page waits
  on `Module.simtowerSavesReady`; `mountSaves()` waits for the same thing, and
  has to run before anything looks in that directory at all.
* **IDBFS asserts rather than throws where there is no IndexedDB**, and an
  assert aborts the runtime. Ask `typeof indexedDB` first. And `-lidbfs.js` is
  per target: with it missing the runtime keeps a stub that answers "IDBFS is no
  longer included by default" and the mount fails into an ordinary in-memory
  directory, so everything works until the page is reloaded.

### Debug keys

Held in the fork, in `apply_original_debug_key` in `native_main.cpp`, taken in
the message pump rather than in a window procedure - the focus normally sits in
one of the auxiliary windows, and a debug key that depends on which one is no
use.

| key | |
|---|---|
| `1`..`6` | set the star rating and refill the bank. Most of the game is behind its rating and reaching one takes a real game, which left the renderer above one star close to untestable. |
| `0` | refill the bank only. |
| `E` | write every live shaft to stderr: position, settle counter, door state, passengers, direction, target, and each floor's waiting counts. |
| `T` | write the tower to stderr: rating, lobby height, population, funds, and every floor's tenants as `type@x`. |

Nothing in SimTower binds a digit, so this takes nothing away. `E` and `T` are
what turn "it looks stuck" into a number that did or did not change;
`tools/elevator_scenario.sh` plays a tower out and prints them.

### The changes carried against the port

`tools/fetch_upstream.sh` pins the fork's `floor-edges` branch rather than its
main line. On it, oldest first:

* **`beef8e9` floor edges.** `render_original_floor_edges`, whose
reconstruction of the shared WinG sheet had the fragments' width and height
swapped and took the standard floor edge from BITMAP/1259 - a bank of hotel
rooms - instead of BITMAP/1069, the emergency stairs.  The evidence, in case it
needs revisiting: the fragments are drawn at the floor edge minus 24 and minus
56, which only makes sense as widths; read that way both come out exactly one
story tall; BITMAP/1069 is 48 wide, holding the two 24-wide staircases, and
BITMAP/1001 is 112 wide, holding the two 56-wide canopies; and the port's own
dimension checks on all five source bitmaps pass, so the ids are not shifted.
  Reverting is one line: pin 9c2685a again.
* **`fc35789` the ground below the ground line is earth**, from BITMAP/849.
  Without it everything below `world_y >= 3960` was white.
* **`785471b`, `e3a0869` a facility carries its own ceiling, and index zero is
  transparent.** A 24-row facility is bottom-aligned in a 36-row band; nothing
  drew the twelve rows above it, so there was a transparent gap between every
  pair of floors. And `merge_original_nonzero_channels` tested the *resolved
  colour's* channels, which makes index zero opaque because CLUT/1000 resolves
  it to white - every person sprite arrived in a solid white box. The test is
  on the source byte.
* **`ad5dd48`, `588f1c1` the debug keys** above.
* **`60ad32b` the rating key left the palette unable to build.** It passed
  argument one to `refresh_original_rating_command`, which forces command mode
  two. New and Open pass zero.
* **`4cd55f4` the elevator car overlay.** `render_original_elevator_cars` drew
  cars only for `word_3c == 0`, a state no shaft the game builds is ever in, so
  a car's only trace was the floor-number bank turning red - one fixed graphic
  per floor, which is why the elevator looked frozen. The moving sprite is the
  BITMAP/1064..1069 car, positioned by `original_elevator_car_visual`'s
  interpolated y. **`word_3c` is not a view toggle**: it marks a live shaft.
  `extend_original_elevator_shaft` and both shrinks refuse without it and
  `original_elevator_service_floor_gate` calls a shaft without it inactive, so
  clearing it - which was the first attempt, and did draw the car - took the
  shaft's whole editing surface with it and the user could no longer extend an
  elevator. The overlay follows the flag now.

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

* **Set a build type.** CMake passes no optimisation flag without one, so
  emscripten compiles at -O0 and the software rasteriser runs three times
  slower for a page three times larger. Release is the default here now.
* **The shell is not a source of any target.** `web/shell.html` is baked in at
  link time, so nothing rebuilt when it changed and the page kept its old text
  while the build reported no work to do. It is a `LINK_DEPENDS` now.
* **Deploy with `tools/deploy.sh`, never by copying the file.** The game is one
  2 MB page; served from a fixed URL a browser keeps showing an old build, which
  is indistinguishable from a fix that did not work, and telling a player to
  clear their cache is not a fix. The game goes to `docs/play.html` and
  `docs/index.html` is a few hundred bytes that redirect to
  `play.html?v=<stamp>`. A new build is a new URL and can never come out of a
  cache; only the tiny index can go stale, and Pages revalidates that inside its
  ten-minute max-age. The build is also stamped bottom-left on the page, so a
  screenshot says which build produced it - `BUILD_STAMP` in `web/shell.html`
  is the placeholder deploy.sh fills.
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

## Next: what play-testing has reported

From someone playing the deployed page, newest first within each group.
Reproduce with the harness rather than by reasoning about the code: every one
of these so far looked like a different bug from the one it was.

### Open

0. ~~**Housekeeping staff never appear**~~ **Resolved: it is the original's
   own rule - maids ride only the Service elevator.**  The route scorer's gate
   `((elevator.type != 2U) != tracked_route)` (original_people.cpp, exact
   11b0:11af) sends untracked staff routes exclusively to type-2 shafts, and
   housekeeping requests untracked routes.  A linen room whose maids cannot
   reach the hotel floors by service elevator (or stairs) leaves them parked
   at home forever: measured with debug key `H`, a maid sat at state=0 with
   the dirty room found (`room_floor=11`) for five straight minutes while the
   rooms stayed dirty (0x28/0x30, twenty samples per room).  Rebuilt the same
   tower with a Service elevator beside the standard one and the whole cycle
   ran: maid state 0 -> 2 (cleaning, day-1 morning), a rider visible in the
   service car, and 0x28 appeared exactly once across the run before being
   cleaned away.  Everything else was already correct - six type-15 people are
   created at activation and stepped every frame in the shared people pass.

   Where things live, for replaying it: the linen room is the **fourth row of
   the hotel group** (icon 23 -> type 15); the Service elevator is the
   **second row of the elevator group** (icon 5 -> command 43 -> elevator
   type 2).  Debug key `H` dumps every type-15 person (state, target room,
   home) plus every hotel room's status byte; dirty is 0x28/0x30.  One more
   harness gotcha: an extension drag's START floor becomes the new top, so
   reaching floor 12 from a 1F shaft takes two drags (489->453 then 453->417).

1. ~~**The ceiling pattern is wrong.**~~ **Fixed** (fork `5f19a70`): the twelve
   rows above a facility come from BITMAP/3880 - the hatched band the type-45
   boundary tenants use, tiled on world x - not from BITMAP/1000 cell two,
   which is a flat legend bar.  Settled against a screenshot of the real game:
   the speckle, the double slab line and the 32-pixel vertical seam all match.
   The reference screenshot lives at `simtower-a-1.png` (untracked).
   Previously: The twelve rows above a facility come from
   cell two of BITMAP/1000 (`kFloorCeilingCell` in
   `render_original_direct_facilities`). That was chosen because the strip has
   to match what an empty Floor carries along the same row, but BITMAP/1000 is
   96x36 of mostly flat colour bars and looks more like a legend than floor
   art. Type 0 - Floor - is *not* in `kDirectFacilityGraphics`, so the empty
   floor band is drawn somewhere else; find that and take the ceiling from the
   same source. The lobby is expected to differ: "the ceiling above the lobby
   has its own pattern". Reported three times; the most wanted fix.
2. **People inside offices and rooms move far too fast.**  The earlier
   hypothesis is half wrong, checked against the pass plans: an ordinary
   repaint does NOT double-advance - `window_paint`, `preview_repaint` and
   `palette_repaint` all carry `advance_visible_facility_people = false`, so
   the defaulted `advance_state=true` inside `paint_known_original_surface`
   is a no-op for people.  What DOES advance them outside the simulation
   frame is **scrolling**: the camera/scrollbar refresh paths rebuild with
   `rebuild_with_sky` / `rebuild_without_sky` (native_main.cpp 1913/1947,
   5502, 8290, 8329), whose plans advance people once per scroll step, so
   people accelerate while the view is scrolled.  Open question: the
   original's 1080:0a1e rebuild includes 1038:050e, so it may do exactly the
   same.  Compare against the real game while scrolling before changing
   anything.
3. ~~**Extending a lobby garbles it.**~~ **Fixed** (fork `5f19a70`): a
   leftward extension leaves two adjacent type-24 records - the record surgery
   keeps edge remainders by design and is exact, so the records are right -
   and the renderer keyed the entrance tiles off each record's left edge,
   drawing a second entrance mid-lobby.  The entrance is keyed to the
   contiguous run now.  Verified: `24@161 24@177` in the data, one entrance in
   the picture.  Also fixed nearby: **a shaft can no longer be extended through
   occupied floors** - the collision test only covered other shafts and
   stairs, so a shaft would run straight through offices.  Verified both ways
   in the harness.
4. ~~**Nobody rides, and the population stays zero.**~~ **It was patience,
   measured this time.** The game clock: one simulation frame per 58 ms (Fast
   Mode, the default - Options toggles it, command 40007; 96 ms without), a
   day is 2,600 frames, so **one game day is about 2.7 minutes of wall time**,
   and a fresh tower starts at frame ~2545 of day zero, so the first full
   daily cycle does not even begin for four minutes.  Every earlier run
   watched for less than that.  A seven-minute run of the standard scenario
   reached `2nd WD` with **__FINAL_POP__**, and the interim screenshot shows a
   guest in a hotel room at night.  Final dumps: `__FINAL_ELEV__`.

   Note the shape of the proof: the screenshot mid-run shows `Pop 5` at
   night, and the final dump shows `pop=0` on day two - that is guests
   checking in at night and out in the morning, the whole hotel cycle,
   working.  A population that returns to zero is hotel behaviour, not a
   regression.

   Two useful facts fell out of the measurement.  The daily machinery -
   arrivals, check-ins, checkouts, housekeeping - hangs off exact frame
   numbers in `step_original_simulation`'s schedule switch (`case 0x0640:`
   morning, `0x07d0` evening, `0x08fc` day++), and `++frame_time` advances one
   frame at a time, so nothing can be skipped.  And the linen-room report
   ("no bed-makers") is this same item: housekeepers dispatch after guests
   check out, which is day two at the earliest - about six minutes in.
5. **The tool palette icons do not match their entries.** Raised as "the
   elevator cell shows a bed".  **Verified correct end to end from the data**,
   so treat this as unconfirmed until someone reproduces it with a screenshot:
   TABL/(1000+rating) encodes each entry as TABM<<8|choice, the TABM word is
   the icon number, BITMAP/300/301/302 are 8-column grids of 32x32 icons, the
   blit indexes them `(icon%8)*32, (icon/8)*32`, and TABL/1000[icon] is the
   build type.  At three stars: pos0 icon0 lobby(24), pos1 icon4 elevator(1)
   group {4,5,6}, pos2 icon7 office(7), pos3 icon8 hotel single(3) group
   {8,9,10,23} - **and 23 is Housekeeping (type 15): the linen room lives in
   the hotel group, fourth row, not with Security** - pos4 icon24 condo(9),
   pos5 icon11 retail(12) group {11..15}, pos6 icon16 parking(44) group
   {16=ramp,17=space(11),18=recycling(20)}, pos7 icon21 security(14) group
   {21,22=Medical(13)}.  Icon 4 - the standard elevator's brown doors - is easy
   to misread as furniture at one-to-one scale; that may be the whole report.

6. ~~**Escalators cannot be placed on the second floor or above / phantom
   clicks and broken hold-drag on elevators.**~~ **Fixed and closed by the
   player** (shim `b2e66ac`, fork `78fdc12`): the cause was the browser mouse
   bridge, not construction rules.  The DOM replays a double click as
   down/up/down/up plus a fifth dblclick event, where USER32 folds the second
   press into WM_...DBLCLK and MAINWNDPROC's double-click branch never begins
   an interaction - so fast successive presses fired spurious build/extend
   attempts and popup selections landed on the palette underneath.  onMouse
   now performs the USER32 substitution itself (500 ms / 4 px, CS_DBLCLKS
   gated) and no longer listens for dblclick; moves and releases moved to the
   document so a drag leaving the canvas cannot lose its mouseup and leave a
   button logically held forever.  Construction itself was verified fine in
   the harness: escalators place on bare floor at any story, between offices,
   and stacked at the same x; shafts extend repeatedly on clear floors.
   Beware in scripts: two same-spot presses within 500 ms now arrive as a
   double click - space palette re-presses 600 ms apart.  The player has since
   confirmed all of it in the browser on build 0286656: escalator placement,
   button clicks, and repeated shaft extension - the 'second extension always
   fails' report was this same input bug, not the construction rules.

7. ~~**The 3-story lobby's spiral staircase sits in a white box.**~~ **Fixed**
   (fork `78fdc12`): CGPK/(4071+lobbyHeight) frames are overlays composed on
   an index-zero field, and CLUT/1000 resolves index zero to white.
   draw_cgpk_tile grew a transparent mode used only by the tall-transport
   renderer; lobby banks stay opaque.  Verified for both the tall stair
   (shape 5) and tall escalator (shape 4): marble, chandeliers and riders all
   show through.

### Reported, but the game is behaving as written

Worth confirming against the original before "fixing" any of them. Most do say
why, in the info bar, out of the game's own STRL/1003 - it is worth reading
that strip before assuming a click did nothing:

    15 Place Metro station on bottom floor
    16 Cathedral is available only on 100th floor
    17 Only one Metro Station allowed
    19 Only one Cathedral allowed
    20 Cannot place item there
    21 Cannot destroy this item
    33 Cannot destroy items under construction

The express elevator is the exception: its floor check returns a **zero** status
code, so nothing is said and the click simply does nothing.

One thing here does not add up and is worth settling. The Cathedral message
says the hundredth floor, which is floor index 109, but `build_original_-
cathedral` demands index **113** - floor 104 - and builds its five parts at
109..113 with the main type-36 record on top. The Metro is the same shape: its
message says the bottom floor, and the check demands index **2** (B8) with the
type-31 record there and parts below at 1 and 0. Either the click is meant to
be the top of the stack and both messages describe where the building ends up,
or both checks are off by the stack height. `apply_original_replacement_-
demolition` agrees with the port (`first_floor -= 4` for 36, `-= 2` for 31),
so do not change one without the other.

* **The Metro Station cannot be built** (`build_original_metro_station`). It
  wants floor index **2** - that is B8 - and floor index 0 (B10) must already
  hold exactly one ordinary type-0 floor record. It is a three-part
  constructor over floors 0..2, 30 cells wide, one per tower.
* **The Cathedral cannot be built** (`build_original_cathedral`). It wants
  floor index **113**, five parts over 109..113 (types 40 down to 36), 28
  cells wide, one per tower. Floor index 113 is floor 104, so a tower that
  does not already reach the top cannot even scroll there - the vertical
  scroll range is clamped to what is built.
* **Some rooms cannot be bulldozed.** Ordinary rooms do demolish - types 7, 3
  and 12 were placed and removed in the harness - but
  `original_facility_damage_protected` refuses types **14 (Security), 15
  (Housekeeping), 24/25/26 (the Lobby and its upper stories), 31/32/33 (the
  Metro), 36..40 (the Cathedral) and 45**, and the info bar says the game's own
  *"Cannot destroy this item"* (STRL/1003 entry 21). A facility still under
  construction - a negative tenant type - gives entry 33, *"Cannot destroy
  items under construction"*, which is the likeliest thing to hit by accident.
  The suspect entry is Security and Housekeeping: everything else in that list
  is structural or one-per-tower. The table carries no address citation, and
  the same routine serves both the bulldozer and fire damage (the `flags`
  argument only picks the alert), so if the original let you bulldoze a
  Housekeeping office, that is where to look.
* **The large (express) elevator cannot be built.** `build_original_elevator`
  takes command type 42 as the express: internal type 0, capacity 42, six
  cells wide - and above floor ten it is permitted only on the sky-lobby
  sequence 24, 39, 54, 69, 84, 99. At floor ten it builds: verified in the
  harness, `x=179 type=0 cap=42`. Service (command type 43, internal 2) builds
  too.

### Fixed since the last handover

* Elevator cars are drawn and move (`4cd55f4`); shafts still extend
  (`4cd55f4` again - the first attempt broke that).
* Escalators and stairs no longer arrive in a white box (`ec9a8db`).
* The digit keys no longer leave the palette unable to build (`60ad32b`), and
  they refill the bank (`588f1c1`).
* The two- and three-story lobby is **Ctrl** and **Ctrl+Shift** on the *first*
  lobby press, and only then (`old_height == 0` in
  `begin_original_lobby_drag`). It works; it is undiscoverable, not broken.
  The harness can hold both: `mods:ctrl` before a `drag:`.

### Driving the tool palette from a script

This cost several runs. The catalogue grid starts at y=239 with 32-pixel rows
and two 32-pixel columns centred on x=150 and x=182, so entry *n* is at
`(150 or 182, 255 + 32 * (n / 2))`. A **group** cell opens a popup on the
press, and **the popup is positioned so that the group's current choice sits
over the cell** - so the row coordinates move as soon as something is picked.
Press, `dump` to read the popup's rectangle, then release on the row wanted.
Pressing and releasing without moving keeps the current choice.

At six stars the entries are: 0 lobby/floor/escalator/service, 1 the three
elevators, 2 condo, 3 office group, 4 type 9, 5 the retail group, 6 the group
holding **Metro (31) and Cathedral (36)**, 7 the restaurant group. Metro and
Cathedral are the last two rows of entry **6**, at (150,351) - not entry 5.

## Still unseen

* ~~**Hear the sound.**~~ Heard - see "What is still missing" above for the
  three stacked faults.  The harness `sound` action prints the submission
  counter, which is what separates 'silent because headless' from 'silent for
  real'.
* **The event dialogs.** The templates that arrive during play - the VIP, the
  fire, the alerts - have never come up.
* **Compare against the native build pixel for pixel.** It builds and runs; the
  only thing in the way was that a locked desktop screenshots black.

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
