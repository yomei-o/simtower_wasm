// Drive the published page in a real browser, headlessly.
//
//   node tools/browser_check.js docs/index.html /path/to/SIMTOWER.EXE out.png \
//        [action...]
//
// The node harness stubs the DOM, which is the right way to check the renderer
// quickly but says nothing about the page itself: the file input, the canvas
// element, the event listeners, the audio context.  This loads docs/ over HTTP
// in headless Chrome, hands the file input a real SIMTOWER.EXE, clicks with
// real mouse events and takes a real screenshot.
//
// Actions are the node harness's, minus the ones that need the shim's own
// injection: wait:<ms>  click:<x>,<y>  move:<x>,<y>  key:<name>  shot:<file>
// Coordinates are canvas pixels; the canvas's own offset in the page is added.

const fs = require('fs');
const path = require('path');
const http = require('http');
const { spawn } = require('child_process');

const pagePath = path.resolve(process.argv[2] || 'docs/index.html');
const executable = path.resolve(process.argv[3]);
const outputPath = path.resolve(process.argv[4] || 'browser.png');
const actions = process.argv.slice(5);

// Edge first: a managed Chrome can have DevTools remote debugging disabled by
// policy, and then this fails with nothing to say about the page.
const CHROME = (process.env.SIMTOWER_BROWSER ? [process.env.SIMTOWER_BROWSER] : [])
  .concat([
    'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
    'C:/Program Files/Microsoft/Edge/Application/msedge.exe',
    'C:/Program Files/Google/Chrome/Application/chrome.exe',
    '/usr/bin/google-chrome',
    '/usr/bin/chromium',
  ]).find((p) => fs.existsSync(p));

if (!CHROME) {
  console.error('no Chrome or Edge found');
  process.exit(2);
}

const root = path.dirname(pagePath);
const pageName = path.basename(pagePath);
const PORT = 8731;
const DEBUG_PORT = 9333;

const TYPES = {
  '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm',
};

function serve() {
  return new Promise((resolve) => {
    const server = http.createServer((req, res) => {
      const name = decodeURIComponent(req.url.split('?')[0]).replace(/^\//, '')
                   || pageName;
      const file = path.join(root, name);
      if (!file.startsWith(root) || !fs.existsSync(file)) {
        res.writeHead(404);
        res.end();
        return;
      }
      res.writeHead(200, {
        'Content-Type': TYPES[path.extname(file)] || 'application/octet-stream',
      });
      fs.createReadStream(file).pipe(res);
    });
    server.listen(PORT, '127.0.0.1', () => resolve(server));
  });
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function targetUrl() {
  // Chrome takes a moment to open its debugging port.
  for (let attempt = 0; attempt < 60; attempt++) {
    try {
      const response = await fetch(`http://127.0.0.1:${DEBUG_PORT}/json/list`);
      const targets = await response.json();
      const page = targets.find((t) => t.type === 'page' && t.webSocketDebuggerUrl);
      if (page) return page.webSocketDebuggerUrl;
    } catch (e) { /* not up yet */ }
    await sleep(250);
  }
  throw new Error('Chrome never opened its debugging port');
}

function connect(url) {
  return new Promise((resolve, reject) => {
    const socket = new WebSocket(url);
    let nextId = 1;
    const pending = new Map();
    socket.addEventListener('message', (event) => {
      const message = JSON.parse(event.data);
      if (message.id && pending.has(message.id)) {
        const { resolve: ok, reject: fail } = pending.get(message.id);
        pending.delete(message.id);
        if (message.error) fail(new Error(JSON.stringify(message.error)));
        else ok(message.result);
      }
    });
    socket.addEventListener('error', reject);
    socket.addEventListener('open', () => resolve({
      send(method, params) {
        const id = nextId++;
        socket.send(JSON.stringify({ id, method, params: params || {} }));
        return new Promise((ok, fail) => pending.set(id, { resolve: ok, reject: fail }));
      },
      close() { socket.close(); },
    }));
  });
}

(async () => {
  const server = await serve();
  const profile = fs.mkdtempSync(path.join(require('os').tmpdir(), 'simtower-'));
  const chrome = spawn(CHROME, [
    '--headless=new',
    `--remote-debugging-port=${DEBUG_PORT}`,
    `--user-data-dir=${profile}`,
    '--no-first-run', '--no-default-browser-check', '--disable-gpu',
    // So the page may make a sound without a gesture, which a headless run
    // cannot give it.
    '--autoplay-policy=no-user-gesture-required',
    '--window-size=1000,900',
    `http://127.0.0.1:${PORT}/${pageName}`,
  ], { stdio: 'ignore' });

  const cdp = await connect(await targetUrl());
  await cdp.send('Page.enable');
  await cdp.send('Runtime.enable');
  await cdp.send('DOM.enable');

  const evaluate = async (expression) => {
    const result = await cdp.send('Runtime.evaluate', {
      expression, returnByValue: true,
    });
    return result.result && result.result.value;
  };

  // Wait for the wasm runtime rather than for a fixed time.
  let ready = false;
  for (let attempt = 0; attempt < 120; attempt++) {
    const status = await evaluate(
      "document.getElementById('status') && document.getElementById('status').textContent");
    if (status && /ready/.test(status)) { ready = true; break; }
    await sleep(250);
  }
  console.log('runtime ready:', ready);
  if (!ready) {
    console.log('status:', await evaluate(
      "document.getElementById('status').textContent"));
    console.log('log:', await evaluate("document.getElementById('log').textContent"));
  }

  // Hand the page's own file input a real executable.
  const { root: document } = await cdp.send('DOM.getDocument');
  const { nodeId } = await cdp.send('DOM.querySelector', {
    nodeId: document.nodeId, selector: '#file',
  });
  await cdp.send('DOM.setFileInputFiles', { nodeId, files: [executable] });
  await sleep(3000);
  console.log('status:', await evaluate(
    "document.getElementById('status').textContent"));

  const origin = await evaluate(
    "(function(){var r=document.getElementById('canvas').getBoundingClientRect();"
    + "return JSON.stringify({x:r.left,y:r.top,w:r.width,h:r.height});})()");
  const canvas = JSON.parse(origin);
  console.log('canvas at', canvas.x + ',' + canvas.y,
              canvas.w + 'x' + canvas.h);

  const shot = async (file) => {
    const { data } = await cdp.send('Page.captureScreenshot', { format: 'png' });
    fs.writeFileSync(file, Buffer.from(data, 'base64'));
    console.log('wrote', file);
  };

  const mouse = async (type, x, y) => {
    await cdp.send('Input.dispatchMouseEvent', {
      type, x: canvas.x + x, y: canvas.y + y,
      button: type === 'mouseMoved' ? 'none' : 'left',
      buttons: type === 'mousePressed' ? 1 : 0,
      clickCount: type === 'mouseMoved' ? 0 : 1,
    });
  };

  for (const action of actions) {
    const colon = action.indexOf(':');
    const verb = colon < 0 ? action : action.slice(0, colon);
    const rest = colon < 0 ? '' : action.slice(colon + 1);
    const parts = rest.split(',').map(Number);
    if (verb === 'wait') await sleep(parts[0] || 100);
    else if (verb === 'move') { await mouse('mouseMoved', parts[0], parts[1]); await sleep(60); }
    else if (verb === 'click') {
      await mouse('mouseMoved', parts[0], parts[1]);
      await sleep(50);
      await mouse('mousePressed', parts[0], parts[1]);
      await sleep(80);
      await mouse('mouseReleased', parts[0], parts[1]);
      await sleep(200);
    } else if (verb === 'key') {
      await cdp.send('Input.dispatchKeyEvent', { type: 'keyDown', key: rest });
      await sleep(50);
      await cdp.send('Input.dispatchKeyEvent', { type: 'keyUp', key: rest });
      await sleep(150);
    } else if (verb === 'shot') await shot(rest);
    else console.log('unknown action', action);
  }

  console.log('log:', await evaluate(
    "document.getElementById('log').textContent"));
  console.log('audio:',
    await evaluate("(Module.simtowerAudio && Module.simtowerAudio.ctx.state) || 'none'"));
  await shot(outputPath);

  cdp.close();
  chrome.kill();
  server.close();
  process.exit(0);
})().catch((error) => {
  console.error(error);
  process.exit(1);
});
