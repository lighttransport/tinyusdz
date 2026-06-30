#!/usr/bin/env node
// SPDX-License-Identifier: Apache-2.0
//
// End-to-end browser test for the tusdview WebSocket stream viewer's keyboard
// forwarding. Drives a REAL browser (Playwright + system Chrome by default),
// loads the embedded viewer, and verifies:
//   1. real keydown/keyup events become the correct WS JSON messages
//      (printable keys, special keys, and modifier flags),
//   2. the full loop works -- pressing 'w' (wireframe hotkey) re-renders on the
//      server and streams changed frames back to the browser.
//
// Requirements (not part of the default build/test deps):
//   npm i playwright        # the Node module
//   # plus a browser: either a system Google Chrome (default, --channel chrome)
//   # or `npx playwright install chromium` and pass --channel "".
// If Playwright isn't installed the test prints SKIP and exits 0.
//
// Usage:
//   node stream-keyboard-test.js [--tusdview ./build/tusdview]
//                                [--model models/suzanne-pbr.usda]
//                                [--port 8090] [--channel chrome]
//   node stream-keyboard-test.js --url http://host:8090/   # use a running server

'use strict';
let chromium;
try { ({ chromium } = require('playwright')); }
catch (e) {
  console.log('SKIP: Playwright not installed (run `npm i playwright`).');
  process.exit(0);
}
const { spawn } = require('child_process');
const crypto = require('crypto');

const md5 = b => crypto.createHash('md5').update(b).digest('hex').slice(0, 10);
const sleep = ms => new Promise(r => setTimeout(r, ms));

function arg(name, def) {
  const i = process.argv.indexOf('--' + name);
  return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : def;
}

const opt = {
  tusdview: arg('tusdview', './build/tusdview'),
  model: arg('model', 'models/suzanne-pbr.usda'),
  port: parseInt(arg('port', '8090'), 10),
  channel: arg('channel', 'chrome'),  // '' -> Playwright's bundled Chromium
  url: arg('url', null),
};

// Spawn a headless-VK tusdview stream server and resolve once it is listening.
function startServer() {
  return new Promise((resolve, reject) => {
    // NOTE: --stream-http takes its value with '=' (an optional-value flag).
    const p = spawn(opt.tusdview, ['--headless', '--backend', 'vk',
      '--stream-http=' + opt.port, '--stream-idle-ms', '200', opt.model],
      { stdio: ['ignore', 'pipe', 'pipe'] });
    let done = false;
    const onData = d => {
      if (!done && /stream server listening/.test(d.toString())) {
        done = true; resolve(p);
      }
    };
    p.stdout.on('data', onData);
    p.stderr.on('data', onData);
    p.on('exit', c => { if (!done) reject(new Error('tusdview exited early (code ' + c + ')')); });
    setTimeout(() => { if (!done) reject(new Error('timed out waiting for stream server')); }, 20000);
  });
}

async function launchBrowser() {
  try { return await chromium.launch({ headless: true, channel: opt.channel || undefined }); }
  catch (e) {
    if (opt.channel) { return await chromium.launch({ headless: true }); }  // fall back to bundled
    throw e;
  }
}

(async () => {
  let server = null;
  const checks = [];
  const check = (name, ok) => { checks.push({ name, ok }); console.log(`  [${ok ? 'PASS' : 'FAIL'}] ${name}`); };

  if (!opt.url) server = await startServer();
  const url = opt.url || `http://127.0.0.1:${opt.port}/`;
  const browser = await launchBrowser();
  try {
    const page = await browser.newPage({ viewport: { width: 1100, height: 720 } });
    const sent = [], recv = [];
    page.on('websocket', ws => {
      ws.on('framesent', f => { if (typeof f.payload === 'string') { try { sent.push(JSON.parse(f.payload)); } catch (e) {} } });
      ws.on('framereceived', f => { if (Buffer.isBuffer(f.payload)) recv.push(md5(f.payload)); });
    });

    await page.goto(url, { waitUntil: 'domcontentloaded' });
    const t0 = Date.now();
    while (recv.length < 3 && Date.now() - t0 < 15000) await sleep(200);
    check(`stream delivered frames to the browser (${recv.length})`, recv.length >= 3);

    await page.mouse.click(550, 400);  // focus off the page's <input>
    await sleep(700);

    const before = recv.length;
    await page.keyboard.press('w');    // wireframe hotkey -> re-render
    await sleep(1200);
    const afterW = recv.length - before;
    await page.keyboard.press('w');
    await sleep(1200);
    check(`'w' keypress streamed new frames back (${afterW})`, afterW > 0);
    check('render changed across wireframe toggles', new Set(recv).size >= 2);

    // Special keys + modifier combo.
    await page.keyboard.press('ArrowDown');
    await page.keyboard.press('Backspace');
    await page.keyboard.down('Control'); await page.keyboard.press('a'); await page.keyboard.up('Control');
    await sleep(400);

    const keys = sent.filter(m => m.t === 'k');
    const has = (k, down) => keys.some(m => m.k === k && m.down === down);
    check("'w' sent keydown+keyup", has('w', true) && has('w', false));
    check("special keys forwarded (ArrowDown, Backspace)", has('ArrowDown', true) && has('Backspace', true));
    check("modifier flag forwarded (Ctrl+A)", keys.some(m => m.k === 'a' && m.ctrl === true));
  } finally {
    await browser.close();
    if (server) server.kill('SIGKILL');
  }

  const failed = checks.filter(c => !c.ok);
  console.log(failed.length ? `\nFAILED ${failed.length}/${checks.length}` : `\nOK: ${checks.length}/${checks.length} checks passed`);
  process.exit(failed.length ? 1 : 0);
})().catch(e => { console.error('TEST ERROR:', e.message); process.exit(2); });
