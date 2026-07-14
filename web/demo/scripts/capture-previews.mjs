#!/usr/bin/env node

import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import fs from 'node:fs/promises';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { DEMOS } from '../src/demo-configs.js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const demoRoot = path.resolve(__dirname, '..');
const previewDir = path.join(demoRoot, 'public', 'assets', 'previews');
const configPath = path.join(demoRoot, 'src', 'demo-configs.js');
const width = Number(process.env.TINYUSDZ_PREVIEW_WIDTH || 1280);
const height = Number(process.env.TINYUSDZ_PREVIEW_HEIGHT || 820);
const waitMs = Number(process.env.TINYUSDZ_PREVIEW_WAIT_MS || 90000);

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

async function findFreePort() {
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.unref();
    server.on('error', reject);
    server.listen(0, '127.0.0.1', () => {
      const port = server.address().port;
      server.close(() => resolve(port));
    });
  });
}

function findExecutable(names) {
  const pathDirs = (process.env.PATH || '').split(path.delimiter);
  for (const name of names) {
    if (name.includes(path.sep) && existsSync(name)) return name;
    for (const dir of pathDirs) {
      const fullPath = path.join(dir, name);
      if (existsSync(fullPath)) return fullPath;
    }
  }
  return null;
}

async function waitForHTTP(url, timeoutMs = 30000) {
  const deadline = Date.now() + timeoutMs;
  let lastError = null;
  while (Date.now() < deadline) {
    try {
      const response = await fetch(url);
      if (response.ok) return response;
    } catch (error) {
      lastError = error;
    }
    await sleep(250);
  }
  throw new Error(`Timed out waiting for ${url}${lastError ? `: ${lastError.message}` : ''}`);
}

function startProcess(command, args, options = {}) {
  const child = spawn(command, args, {
    cwd: demoRoot,
    detached: process.platform !== 'win32',
    stdio: ['ignore', 'pipe', 'pipe'],
    ...options
  });
  child.stdout.on('data', (chunk) => process.stdout.write(chunk));
  child.stderr.on('data', (chunk) => process.stderr.write(chunk));
  child.on('exit', (code, signal) => {
    if (child.__tinyusdzStopping) return;
    if (code !== 0 && signal !== 'SIGTERM') {
      console.error(`${path.basename(command)} exited with ${code ?? signal}`);
    }
  });
  return child;
}

async function stopProcess(child) {
  if (!child || child.exitCode !== null || child.signalCode !== null) return;
  child.__tinyusdzStopping = true;
  const exited = new Promise((resolve) => child.once('exit', resolve));
  const killTarget = process.platform === 'win32' ? child.pid : -child.pid;
  try {
    process.kill(killTarget, 'SIGTERM');
  } catch {
    return;
  }
  const stopped = await Promise.race([exited.then(() => true), sleep(1500).then(() => false)]);
  if (stopped || child.exitCode !== null || child.signalCode !== null) return;
  try {
    process.kill(killTarget, 'SIGKILL');
  } catch {
    return;
  }
  await Promise.race([exited, sleep(1000)]);
}

class CDPClient {
  constructor(wsUrl) {
    this.wsUrl = wsUrl;
    this.nextId = 1;
    this.pending = new Map();
    this.handlers = new Map();
  }

  async connect() {
    this.ws = new WebSocket(this.wsUrl);
    this.ws.addEventListener('message', (event) => {
      const message = JSON.parse(event.data);
      if (message.id && this.pending.has(message.id)) {
        const { resolve, reject } = this.pending.get(message.id);
        this.pending.delete(message.id);
        if (message.error) reject(new Error(message.error.message || JSON.stringify(message.error)));
        else resolve(message.result);
        return;
      }
      const callbacks = this.handlers.get(message.method);
      if (callbacks) {
        for (const callback of callbacks) callback(message.params || {});
      }
    });
    await new Promise((resolve, reject) => {
      this.ws.addEventListener('open', resolve, { once: true });
      this.ws.addEventListener('error', reject, { once: true });
    });
  }

  on(method, callback) {
    if (!this.handlers.has(method)) this.handlers.set(method, []);
    this.handlers.get(method).push(callback);
  }

  send(method, params = {}) {
    const id = this.nextId++;
    const payload = JSON.stringify({ id, method, params });
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.ws.send(payload);
    });
  }

  close() {
    this.ws?.close();
  }
}

async function startVite(port) {
  const viteBin = path.join(demoRoot, 'node_modules', '.bin', process.platform === 'win32' ? 'vite.cmd' : 'vite');
  const child = startProcess(viteBin, ['--host', '127.0.0.1', '--port', String(port), '--strictPort']);
  await waitForHTTP(`http://127.0.0.1:${port}/`);
  return child;
}

async function startChrome(debugPort) {
  const chrome = process.env.CHROME_BIN || findExecutable([
    'google-chrome',
    'google-chrome-stable',
    'chromium',
    'chromium-browser'
  ]);
  if (!chrome) throw new Error('Could not find google-chrome or chromium in PATH.');

  const userDataDir = path.join(os.tmpdir(), `tinyusdz-demo-previews-${process.pid}`);
  const xvfb = findExecutable(['xvfb-run']);
  const useXvfb = !!xvfb && process.env.TINYUSDZ_PREVIEW_NO_XVFB !== '1';
  const chromeArgs = [
    `--remote-debugging-port=${debugPort}`,
    '--remote-debugging-address=127.0.0.1',
    `--user-data-dir=${userDataDir}`,
    `--window-size=${width},${height}`,
    '--no-first-run',
    '--no-default-browser-check',
    '--disable-dev-shm-usage',
    '--disable-gpu-sandbox',
    '--enable-webgl',
    '--ignore-gpu-blocklist',
    '--use-gl=angle',
    '--use-angle=swiftshader',
    '--enable-unsafe-swiftshader',
    '--hide-scrollbars',
    '--autoplay-policy=no-user-gesture-required',
    '--no-sandbox',
    'about:blank'
  ];

  if (!useXvfb) {
    chromeArgs.unshift('--headless=new');
  }

  const child = useXvfb
    ? startProcess(xvfb, ['--auto-servernum', '--server-args=-screen 0 1280x960x24', chrome, ...chromeArgs])
    : startProcess(chrome, chromeArgs);

  await waitForHTTP(`http://127.0.0.1:${debugPort}/json/version`);
  return child;
}

async function connectToFirstPage(debugPort) {
  const listResponse = await waitForHTTP(`http://127.0.0.1:${debugPort}/json/list`);
  const targets = await listResponse.json();
  const page = targets.find((target) => target.type === 'page');
  if (!page?.webSocketDebuggerUrl) throw new Error('No Chrome page target found.');
  const client = new CDPClient(page.webSocketDebuggerUrl);
  await client.connect();
  await client.send('Page.enable');
  await client.send('Runtime.enable');
  await client.send('Log.enable');
  await client.send('Emulation.setDeviceMetricsOverride', {
    width,
    height,
    deviceScaleFactor: 1,
    mobile: false
  });
  return client;
}

async function evaluate(client, expression) {
  const result = await client.send('Runtime.evaluate', {
    expression,
    awaitPromise: true,
    returnByValue: true
  });
  if (result.exceptionDetails) {
    throw new Error(result.exceptionDetails.text || 'Runtime.evaluate failed');
  }
  return result.result.value;
}

async function navigate(client, url) {
  const loaded = new Promise((resolve) => {
    const handler = () => resolve();
    client.on('Page.loadEventFired', handler);
  });
  await client.send('Page.navigate', { url });
  await loaded;
}

async function waitForDemoReady(client, demo) {
  const deadline = Date.now() + waitMs;
  let lastState = null;
  while (Date.now() < deadline) {
    lastState = await evaluate(client, `(() => {
      const status = document.querySelector('#status')?.textContent || '';
      const canvas = document.querySelector('#viewport canvas');
      const viewport = document.querySelector('#viewport');
      const rect = viewport?.getBoundingClientRect();
      return {
        status,
        hasApp: !!window.__tinyusdzDemoApp,
        hasCanvas: !!canvas,
        canvasWidth: canvas?.width || 0,
        canvasHeight: canvas?.height || 0,
        rect: rect ? { x: rect.x, y: rect.y, width: rect.width, height: rect.height } : null,
        failed: status.startsWith('Failed:')
      };
    })()`);
    if (lastState.failed) {
      throw new Error(`${demo.id} failed to load: ${lastState.status}`);
    }
    if (
      lastState.hasApp &&
      lastState.status.startsWith('Loaded ') &&
      lastState.hasCanvas &&
      lastState.canvasWidth > 0 &&
      lastState.canvasHeight > 0 &&
      lastState.rect?.width > 100 &&
      lastState.rect?.height > 100
    ) {
      return lastState;
    }
    await sleep(500);
  }
  throw new Error(`Timed out waiting for ${demo.id}. Last state: ${JSON.stringify(lastState)}`);
}

async function captureDemo(client, baseUrl, demo) {
  const href = demo.href.replace(/^\.\//, '');
  const url = `${baseUrl}/${href}`;
  console.log(`Capturing ${demo.id}: ${url}`);
  await navigate(client, url);
  await waitForDemoReady(client, demo);
  await evaluate(client, `(() => {
    const app = window.__tinyusdzDemoApp;
    app?.fitScene?.();
    app?.render?.();
    const status = document.querySelector('#status');
    const dropHint = document.querySelector('#drop-hint');
    if (status) status.style.display = 'none';
    if (dropHint) dropHint.style.display = 'none';
  })()`);
  await sleep(750);
  const rect = await evaluate(client, `(() => {
    const r = document.querySelector('#viewport').getBoundingClientRect();
    return { x: Math.round(r.x), y: Math.round(r.y), width: Math.round(r.width), height: Math.round(r.height) };
  })()`);
  const screenshot = await client.send('Page.captureScreenshot', {
    format: 'jpeg',
    quality: 84,
    clip: {
      x: rect.x,
      y: rect.y,
      width: rect.width,
      height: rect.height,
      scale: 1
    },
    fromSurface: true
  });
  const bytes = Buffer.from(screenshot.data, 'base64');
  if (bytes.length < 4096) {
    throw new Error(`${demo.id} screenshot is unexpectedly small (${bytes.length} bytes).`);
  }
  const outputPath = path.join(previewDir, `${demo.id}.jpg`);
  await fs.writeFile(outputPath, bytes);
  console.log(`Wrote ${path.relative(demoRoot, outputPath)} (${bytes.length} bytes)`);
}

async function updateDemoConfig() {
  const byId = new Set(DEMOS.map((demo) => demo.id));
  const source = await fs.readFile(configPath, 'utf8');
  const lines = source.split('\n');
  let currentId = null;
  let replacements = 0;
  const next = lines.map((line) => {
    const idMatch = line.match(/^\s*id:\s*'([^']+)'/);
    if (idMatch) currentId = idMatch[1];
    if (currentId && byId.has(currentId) && line.match(/^\s*image:\s*/)) {
      replacements++;
      return line.replace(/image:\s*(?:null|'[^']*')/, `image: './assets/previews/${currentId}.jpg'`);
    }
    if (currentId && line.match(/^\s*href:\s*/)) currentId = null;
    return line;
  }).join('\n');
  if (replacements !== DEMOS.length) {
    throw new Error(`Expected to update ${DEMOS.length} preview paths, updated ${replacements}.`);
  }
  await fs.writeFile(configPath, next, 'utf8');
  console.log(`Updated ${path.relative(demoRoot, configPath)} preview paths.`);
}

async function main() {
  await fs.mkdir(previewDir, { recursive: true });
  const vitePort = Number(process.env.TINYUSDZ_PREVIEW_VITE_PORT) || await findFreePort();
  const debugPort = Number(process.env.TINYUSDZ_PREVIEW_CHROME_PORT) || await findFreePort();
  const vite = await startVite(vitePort);
  const chrome = await startChrome(debugPort);
  let client = null;
  try {
    client = await connectToFirstPage(debugPort);
    const baseUrl = `http://127.0.0.1:${vitePort}`;
    for (const demo of DEMOS) {
      await captureDemo(client, baseUrl, demo);
    }
    await updateDemoConfig();
  } finally {
    client?.close();
    await stopProcess(chrome);
    await stopProcess(vite);
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
