// Run the shim outside a browser and write what it presented to a PNG.
//
// The EM_JS glue talks to document/canvas, so those are stubbed here rather than
// in the shim: the point is to exercise the same code the page runs, not a
// second path that might disagree with it.
//
//   node tools/node_harness.js build/node/simtower_node.js out.png [SIMTOWER.EXE]

const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const modulePath = path.resolve(process.argv[2]);
const outputPath = process.argv[3] || 'frame.png';
const executable = process.argv[4];

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
  await new Promise((r) => setTimeout(r, 300));

  console.log('canvas', canvas.width + 'x' + canvas.height);
  if (!presented) {
    console.log('nothing was presented');
    process.exit(2);
  }
  writePng(outputPath, presented.width, presented.height,
           new Uint8Array(presented.data.buffer));
  console.log('wrote', outputPath, presented.width + 'x' + presented.height);

  // The main loop keeps the event loop alive for ever, so the process has to be
  // told to stop.  Without this nothing is flushed and the run just hangs.
  process.exit(0);
})();
