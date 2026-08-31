// Run the shim outside a browser and write what it presented to a PNG.
//
// The EM_JS glue talks to document/canvas, so those are stubbed here rather than
// in the shim: the point is to exercise the same code the page runs, not a
// second path that might disagree with it.
//
//   node tools/node_harness.js <module.js> out.png [SIMTOWER.EXE] [waitMs] [action...]
//
// Actions run in order after the wait, so a session can be played out from the
// command line instead of by hand in a tab:
//
//   wait:<ms>  click:<x>,<y>  move:<x>,<y>  dbl:<x>,<y>  key:<name-or-char>
//   shot:<file>  dump
//
// They go in through simtowerInjectMouse/simtowerInjectKey, which call the very
// handlers the browser's listeners call - the point is to exercise the path the
// page takes, not a second one that can quietly disagree with it.

const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const modulePath = path.resolve(process.argv[2]);
const outputPath = process.argv[3] || 'frame.png';
const executable = process.argv[4];
// The game does a great deal at startup; the viewer does not.
const waitMs = Number(process.argv[5] || 400);
const actions = process.argv.slice(6);

let presented = null;

class FakeImageData {
  constructor(w, h) {
    this.width = w;
    this.height = h;
    this.data = new Uint8ClampedArray(w * h * 4);
  }
}

const canvas = {
  width: 0,
  height: 0,
  style: {},
  getContext(kind) {
    if (kind !== '2d') return null;
    return {
      createImageData: (w, h) => new FakeImageData(w, h),
      putImageData: (image) => { presented = image; },
    };
  },
  addEventListener() {},
};

globalThis.document = {
  getElementById: (id) => (id === 'canvas' ? canvas : null),
  // emscripten's html5 event registration resolves its target with
  // querySelector, so a stub that only has getElementById is not enough.
  querySelector: (selector) => (selector === '#canvas' ? canvas : null),
  addEventListener() {},
  body: { addEventListener() {} },
};
// Deliberately no globalThis.window: emscripten decides it is running in a
// browser from that alone, and then refuses a node-only build.
globalThis.UTF8ToString = () => '';

function writePng(file, width, height, rgba) {
  const chunk = (type, body) => {
    const length = Buffer.alloc(4);
    length.writeUInt32BE(body.length);
    const typed = Buffer.concat([Buffer.from(type, 'ascii'), body]);
    const crc = Buffer.alloc(4);
    crc.writeUInt32BE(zlib.crc32 ? zlib.crc32(typed) : crc32(typed));
    return Buffer.concat([length, typed, crc]);
  };
  // Node has no crc32 before 20.x, so carry one.
  function crc32(buf) {
    let c = ~0;
    for (const b of buf) {
      c ^= b;
      for (let k = 0; k < 8; k++) c = (c >>> 1) ^ (0xEDB88320 & -(c & 1));
    }
    return ~c >>> 0;
  }
  const header = Buffer.alloc(13);
  header.writeUInt32BE(width, 0);
  header.writeUInt32BE(height, 4);
  header[8] = 8; header[9] = 6;                 // 8-bit RGBA
  const raw = Buffer.alloc((width * 4 + 1) * height);
  for (let y = 0; y < height; y++) {
    raw[y * (width * 4 + 1)] = 0;
    Buffer.from(rgba.buffer, rgba.byteOffset + y * width * 4, width * 4)
      .copy(raw, y * (width * 4 + 1) + 1);
  }
  fs.writeFileSync(file, Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]),
    chunk('IHDR', header),
    chunk('IDAT', zlib.deflateSync(raw)),
    chunk('IEND', Buffer.alloc(0)),
  ]));
}

(async () => {
  const createSimTower = require(modulePath);
  const Module = await createSimTower({
    print: (t) => console.log('[out]', t),
    printErr: (t) => console.log('[err]', t),
  });

  console.log('runtime up; calling main');
  Module.callMain([]);

  if (executable) {
    const bytes = fs.readFileSync(executable);
    const pointer = Module._malloc(bytes.length);
    Module.HEAPU8.set(bytes, pointer);
    const count = Module._simtowerLoadExecutable(pointer, bytes.length);
    Module._free(pointer);
    console.log('simtowerLoadExecutable ->', count);
  }

  // emscripten_set_main_loop falls back to setTimeout where there is no
  // requestAnimationFrame, so the loop runs on its own; this just waits for a
  // few frames of it.
  await new Promise((r) => setTimeout(r, waitMs));

  // The wasm runs on its own timer, so every action is followed by a turn of
  // the event loop; injecting two in the same tick would give the game no
  // chance to act on the first.
  const settle = (ms) => new Promise((r) => setTimeout(r, ms));

  const save = (file) => {
    if (!presented) {
      console.log('nothing was presented');
      return false;
    }
    writePng(file, presented.width, presented.height,
             new Uint8Array(presented.data.buffer));
    console.log('wrote', file, presented.width + 'x' + presented.height);
    return true;
  };

  // The virtual key codes the shim maps back from a synthetic DOM code.
  const KEYS = {
    enter: 0x0D, escape: 0x1B, esc: 0x1B, tab: 0x09, space: 0x20,
    backspace: 0x08, up: 0x26, down: 0x28, left: 0x25, right: 0x27,
  };

  const MOUSEMOVE = 8, MOUSEDOWN = 5, MOUSEUP = 6, DBLCLICK = 7;

  for (const action of actions) {
    // Split on the first colon only: a Windows path in shot:C:/... has one of
    // its own, and splitting on every colon loses the drive letter.
    const colon = action.indexOf(':');
    const verb = colon < 0 ? action : action.slice(0, colon);
    const rest = colon < 0 ? '' : action.slice(colon + 1);
    const parts = (rest || '').split(',');
    switch (verb) {
      case 'wait':
        await settle(Number(parts[0] || 100));
        break;
      case 'move':
        Module._simtowerInjectMouse(MOUSEMOVE, +parts[0], +parts[1], 0);
        await settle(50);
        break;
      case 'click':
        // Move first: a control that tracks the pointer has to see it arrive
        // before it is pressed, exactly as it would from a real mouse.
        Module._simtowerInjectMouse(MOUSEMOVE, +parts[0], +parts[1], 0);
        await settle(30);
        Module._simtowerInjectMouse(MOUSEDOWN, +parts[0], +parts[1], 0);
        await settle(60);
        Module._simtowerInjectMouse(MOUSEUP, +parts[0], +parts[1], 0);
        await settle(120);
        break;
      case 'press':
        // Press and hold: the command selector is only up while the button is
        // down, so seeing what is in a group needs the two halves apart.
        Module._simtowerInjectMouse(MOUSEMOVE, +parts[0], +parts[1], 0);
        await settle(40);
        Module._simtowerInjectMouse(MOUSEDOWN, +parts[0], +parts[1], 0);
        await settle(150);
        break;
      case 'release':
        Module._simtowerInjectMouse(MOUSEMOVE, +parts[0], +parts[1], 0);
        await settle(60);
        Module._simtowerInjectMouse(MOUSEUP, +parts[0], +parts[1], 0);
        await settle(250);
        break;
      case 'drag': {
        // Press, move in steps, release - which is the only way to test
        // anything that tracks the pointer while a button is held.
        const [x1, y1, x2, y2] = parts.map(Number);
        Module._simtowerInjectMouse(MOUSEMOVE, x1, y1, 0);
        await settle(30);
        Module._simtowerInjectMouse(MOUSEDOWN, x1, y1, 0);
        await settle(60);
        for (let step = 1; step <= 8; step++) {
          const x = Math.round(x1 + (x2 - x1) * step / 8);
          const y = Math.round(y1 + (y2 - y1) * step / 8);
          Module._simtowerInjectMouse(MOUSEMOVE, x, y, 0);
          await settle(40);
        }
        Module._simtowerInjectMouse(MOUSEUP, x2, y2, 0);
        await settle(200);
        break;
      }
      case 'dbl':
        Module._simtowerInjectMouse(DBLCLICK, +parts[0], +parts[1], 0);
        await settle(120);
        break;
      case 'key': {
        const name = (rest || '').toLowerCase();
        const vk = KEYS[name] !== undefined
          ? KEYS[name]
          : (rest.length === 1 ? rest.toUpperCase().charCodeAt(0) : 0);
        const character = rest.length === 1 ? rest.charCodeAt(0) : 0;
        Module._simtowerInjectKey(2, vk, character);      // KEYDOWN
        await settle(40);
        Module._simtowerInjectKey(3, vk, character);      // KEYUP
        await settle(120);
        break;
      }
      case 'shot':
        save(rest);
        break;
      case 'dump':
        Module._simtowerDumpWindows();
        break;
      case 'putfile': {
        // Put a file into the wasm file system - a debugging tower, say - so a
        // session can start from something the game would take an hour to
        // reach on its own.
        const [from, to] = rest.split('|');
        try {
          Module.FS.writeFile(to, new Uint8Array(fs.readFileSync(from)));
          console.log('put', from, '->', to);
        } catch (error) {
          console.log('could not write', to, String(error));
        }
        break;
      }
      case 'getfile': {
        // Copy a file out of the wasm file system - a saved tower, say - so a
        // browser run can be handed the same bytes.
        const [from, to] = rest.split('|');
        try {
          fs.writeFileSync(to, Buffer.from(Module.FS.readFile(from)));
          console.log('pulled', from, '->', to);
        } catch (error) {
          console.log('could not read', from, String(error));
        }
        break;
      }
      case 'pack': {
        // The pack as this build assembled it, for comparing against the one
        // the Python tool writes and the native build embeds.
        const at = Module._simtowerPackData();
        const size = Module._simtowerPackSize();
        if (!at || size <= 0) {
          console.log('no pack');
          break;
        }
        fs.writeFileSync(rest, Buffer.from(Module.HEAPU8.buffer, at, size));
        console.log('wrote', rest, size, 'bytes');
        break;
      }
      default:
        console.log('unknown action', action);
        process.exit(3);
    }
  }

  console.log('canvas', canvas.width + 'x' + canvas.height);
  if (!save(outputPath)) process.exit(2);

  // The main loop keeps the event loop alive for ever, so the process has to be
  // told to stop.  Without this nothing is flushed and the run just hangs.
  process.exit(0);
})();
