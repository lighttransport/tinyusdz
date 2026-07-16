#!/usr/bin/env node
// Render-only visual parity check for openpbr-nodegraph-demo.html.
//
// From web/js:
//   node cli/openpbr-visual-diff.mjs --sw
//   node cli/openpbr-visual-diff.mjs --sw --dump-composite
//   xvfb-run -a node cli/openpbr-visual-diff.mjs --hw
//   node cli/openpbr-visual-diff.mjs --sw mtlx/test-invert-nodes.usda

// Each scene is rendered once with the legacy backend and once with next. The
// node editor and viewer overlays are hidden before capture, so only the 3D
// viewport contributes to the diff.

import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import puppeteer from 'puppeteer';
import { PNG } from 'pngjs';
import pixelmatch from 'pixelmatch';

const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url));
const WEB_JS_DIR = path.resolve(SCRIPT_DIR, '..');
const DEFAULT_SCENES = [
  'mtlx/test-add-sub-nodes.usda',
  'mtlx/test-invert-nodes.usda',
  'mtlx/test-combine-nodes.usda',
  'mtlx/test-mix-nodes.usda',
  'mtlx/test-multiply-nodes.usda',
  'mtlx/test-separate-nodes.usda',
  'mtlx/test-clamp-nodes.usda',
  'mtlx/test-remap-nodes.usda',
  'mtlx/test-hsv-adjust-nodes.usda',
  'mtlx/test-power-nodes.usda',
  'mtlx/test-divide-nodes.usda',
  'mtlx/test-min-nodes.usda',
  'mtlx/test-max-nodes.usda',
  'mtlx/test-complex-chain.usdz',
  'mtlx/test-texture-extract-nodes.usdz',
  'colorspace-constant-test.usdz',
  'colorspace-gamut-test.usdz',
  'colorspace-texture-test.usdz',
  'texture_channel_blender.usdz',
];

function parseArgs(argv = process.argv.slice(2)) {
  const options = {
    out: path.join(WEB_JS_DIR, 'tests', 'screenshots', 'openpbr-visual-diff'),
    port: 5195,
    baseUrl: process.env.TINYUSDZ_VISUAL_DIFF_BASE_URL?.replace(/\/$/, '') || '',
    width: 1280,
    height: 800,
    timeout: 90000,
    threshold: 0.12,
    maxDiffPercent: 2,
    maxMeanError: 0.02,
    hw: false,
    sw: false,
    headful: false,
    keepServer: false,
    environment: 'studio',
    dumpImages: true,
    dumpComposite: false,
    scenes: [],
  };
  for (let i = 0; i < argv.length; ++i) {
    const arg = argv[i];
    if (arg === '-h' || arg === '--help') {
      printHelp();
      process.exit(0);
    } else if (arg === '--out') options.out = path.resolve(argv[++i]);
    else if (arg === '--port') options.port = Number(argv[++i]);
    else if (arg === '--base-url') options.baseUrl = argv[++i].replace(/\/$/, '');
    else if (arg === '--width') options.width = Number(argv[++i]);
    else if (arg === '--height') options.height = Number(argv[++i]);
    else if (arg === '--timeout') options.timeout = Number(argv[++i]);
    else if (arg === '--threshold') options.threshold = Number(argv[++i]);
    else if (arg === '--max-diff-percent') options.maxDiffPercent = Number(argv[++i]);
    else if (arg === '--max-mean-error') options.maxMeanError = Number(argv[++i]);
    else if (arg === '--hw') options.hw = true;
    else if (arg === '--sw') options.sw = true;
    else if (arg === '--headful') options.headful = true;
    else if (arg === '--keep-server') options.keepServer = true;
    else if (arg === '--environment') options.environment = argv[++i];
    else if (arg === '--authored-environment') options.environment = '';
    else if (arg === '--dump-images') options.dumpImages = true;
    else if (arg === '--no-dump-images') options.dumpImages = false;
    else if (arg === '--dump-composite' || arg === '--composite') options.dumpComposite = true;
    else if (arg.startsWith('-')) throw new Error(`Unknown option: ${arg}`);
    else options.scenes.push(arg);
  }
  if (!options.scenes.length) options.scenes = DEFAULT_SCENES.slice();
  return options;
}

function printHelp() {
  console.log(`Usage: node cli/openpbr-visual-diff.mjs [options] [scene ...]

Scenes are asset-relative paths such as mtlx/test-invert-nodes.usda, absolute
demo paths such as /models/fancy-teapot-mtlx.usdz, or HTTP(S) URLs. With no
scene arguments, the deterministic constant-node test scenes are checked.

Options:
  --out <dir>                PNG and summary output directory
  --base-url <url>           Reuse an existing Vite server instead of starting one
  --port <n>                 Vite port when starting a server (default: 5195)
  --width/--height <px>      Browser viewport (default: 1280x800)
  --timeout <ms>             Per-render timeout (default: 90000)
  --threshold <0..1>         Pixelmatch perceptual threshold (default: 0.12)
  --max-diff-percent <n>     Maximum differing pixels percentage (default: 2)
  --max-mean-error <0..1>    Maximum mean absolute RGB error (default: 0.02)
  --hw                       ANGLE/Vulkan; run under xvfb-run -a
  --sw                       Headless SwiftShader
  --headful                  Show the browser
  --keep-server              Leave the automatically started Vite server running
  --environment <preset>     Force the same environment in both renders (default: studio)
  --authored-environment     Keep each scene's authored/default environment
  --dump-images              Keep the legacy, next, and diff PNGs (default)
  --no-dump-images           Remove the three individual PNGs after comparison
  --dump-composite           Write a 3-column legacy | next | diff PNG
  --composite                Alias for --dump-composite
  -h, --help`);
}

function safeName(value) {
  return String(value).replace(/^https?:\/\//, '').replace(/^\/+/, '')
    .replace(/[^A-Za-z0-9._-]+/g, '__').slice(0, 180);
}

function sceneUri(scene) {
  if (/^https?:\/\//i.test(scene) || scene.startsWith('/')) return scene;
  return `/assets/${scene.replace(/^assets\//, '')}`;
}

function startVite(port) {
  const executable = path.join(WEB_JS_DIR, 'node_modules', '.bin', 'vite');
  return spawn(executable, ['--host', '127.0.0.1', '--port', String(port), '--strictPort'], {
    cwd: WEB_JS_DIR,
    stdio: ['ignore', 'pipe', 'pipe'],
    env: {
      ...process.env,
      // Serve the tracked demo fixtures directly. web/js/assets is an ignored
      // developer convenience directory and may be empty in a clean checkout.
      TINYUSDZ_VITE_PUBLIC_DIR: process.env.TINYUSDZ_VITE_PUBLIC_DIR ||
        path.resolve(WEB_JS_DIR, '../demo/public'),
    },
  });
}

async function waitForServer(url, processHandle, timeout) {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    if (processHandle?.exitCode !== null) {
      throw new Error(`Vite exited with code ${processHandle.exitCode}`);
    }
    try {
      const response = await fetch(url);
      if (response.ok) return;
    } catch (_) {}
    await new Promise((resolve) => setTimeout(resolve, 250));
  }
  throw new Error(`Timed out waiting for ${url}`);
}

function browserOptions(options) {
  const common = [
    '--no-sandbox', '--disable-setuid-sandbox', '--disable-dev-shm-usage',
    '--ignore-gpu-blocklist', '--disable-gpu-blocklist',
    '--disable-breakpad', '--disable-crash-reporter', '--noerrdialogs',
    '--disable-backgrounding-occluded-windows', '--disable-renderer-backgrounding',
    '--disable-background-timer-throttling', '--disable-gpu-vsync',
  ];
  const hardware = options.hw && !options.sw;
  const args = hardware
    ? [...common, '--use-gl=angle', '--use-angle=vulkan', '--enable-features=Vulkan']
    : [...common, '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'];
  return {
    headless: hardware || options.headful ? false : true,
    args,
    env: hardware ? {
      ...process.env,
      __NV_PRIME_RENDER_OFFLOAD: '1',
      __GLX_VENDOR_LIBRARY_NAME: 'nvidia',
    } : process.env,
  };
}

async function renderViewport(browser, baseUrl, scene, backend, output, options) {
  const page = await browser.newPage();
  const errors = [];
  page.on('pageerror', (error) => errors.push(error.message || String(error)));
  page.on('console', (message) => {
    if (message.type() === 'error' &&
        !message.text().startsWith('Failed to load resource:')) {
      errors.push(message.text());
    }
  });
  page.on('response', (response) => {
    if (response.status() >= 400 && !response.url().endsWith('/favicon.ico')) {
      errors.push(`HTTP ${response.status()}: ${response.url()}`);
    }
  });
  try {
    await page.setViewport({width: options.width, height: options.height, deviceScaleFactor: 1});
    const params = new URLSearchParams({ backend, uri: sceneUri(scene) });
    const url = `${baseUrl}/openpbr-nodegraph-demo.html?${params}`;
    await page.goto(url, {waitUntil: 'load', timeout: options.timeout});
    await page.waitForFunction(() => window.renderComplete === true || !!window.renderError,
      {timeout: options.timeout});
    const status = await page.$eval('#status', (element) => element.textContent || '');
    if (/Error:|Failed/i.test(status)) errors.push(status.trim());
    if (errors.length) throw new Error(errors.join(' | '));

    if (options.environment) {
      await page.evaluate((preset) => {
        const select = document.getElementById('env-select');
        if (select) select.value = preset;
        if (typeof window.changeEnvironment === 'function') {
          window.changeEnvironment(preset);
        } else if (select) {
          select.dispatchEvent(new Event('change', {bubbles: true}));
        }
      }, options.environment);
    }

    await page.evaluate(() => {
      const panel = document.getElementById('viewer-panel');
      for (const child of panel?.children || []) {
        if (child.id !== 'canvas-3d') child.style.setProperty('display', 'none', 'important');
      }
      document.getElementById('toast')?.style.setProperty('display', 'none', 'important');
    });
    await page.evaluate(() => new Promise((resolve) =>
      requestAnimationFrame(() => requestAnimationFrame(resolve))));
    const panel = await page.$('#viewer-panel');
    if (!panel) throw new Error('viewer panel was not found');
    await panel.screenshot({path: output});
    return {status: status.trim(), errors};
  } finally {
    await page.close();
  }
}

function compareImages(legacyPath, nextPath, diffPath, options) {
  const legacy = PNG.sync.read(fs.readFileSync(legacyPath));
  const next = PNG.sync.read(fs.readFileSync(nextPath));
  if (legacy.width !== next.width || legacy.height !== next.height) {
    throw new Error(`image dimensions differ: ${legacy.width}x${legacy.height} vs ${next.width}x${next.height}`);
  }
  const diff = new PNG({width: legacy.width, height: legacy.height});
  const pixelsDifferent = pixelmatch(legacy.data, next.data, diff.data,
    legacy.width, legacy.height, {threshold: options.threshold});
  fs.writeFileSync(diffPath, PNG.sync.write(diff));

  let absoluteError = 0;
  for (let offset = 0; offset < legacy.data.length; offset += 4) {
    absoluteError += Math.abs(legacy.data[offset] - next.data[offset]);
    absoluteError += Math.abs(legacy.data[offset + 1] - next.data[offset + 1]);
    absoluteError += Math.abs(legacy.data[offset + 2] - next.data[offset + 2]);
  }
  const totalPixels = legacy.width * legacy.height;
  const diffPercent = pixelsDifferent * 100 / totalPixels;
  const meanError = absoluteError / (totalPixels * 3 * 255);
  return {
    width: legacy.width,
    height: legacy.height,
    pixelsDifferent,
    totalPixels,
    diffPercent,
    meanError,
    passed: diffPercent <= options.maxDiffPercent && meanError <= options.maxMeanError,
  };
}

function writeComposite(legacyPath, nextPath, diffPath, outputPath) {
  const columns = [legacyPath, nextPath, diffPath]
    .map((imagePath) => PNG.sync.read(fs.readFileSync(imagePath)));
  const [{width, height}] = columns;
  if (columns.some((column) => column.width !== width || column.height !== height)) {
    throw new Error('composite image dimensions differ');
  }

  const composite = new PNG({width: width * columns.length, height});
  for (let columnIndex = 0; columnIndex < columns.length; ++columnIndex) {
    const column = columns[columnIndex];
    for (let y = 0; y < height; ++y) {
      const sourceStart = y * width * 4;
      const destinationStart = (y * composite.width + columnIndex * width) * 4;
      column.data.copy(composite.data, destinationStart, sourceStart, sourceStart + width * 4);
    }
  }
  fs.writeFileSync(outputPath, PNG.sync.write(composite));
}

const options = parseArgs();
fs.mkdirSync(options.out, {recursive: true});
let vite = null;
let browser = null;
try {
  const baseUrl = options.baseUrl || `http://127.0.0.1:${options.port}`;
  if (!options.baseUrl) {
    vite = startVite(options.port);
    vite.stderr.on('data', (data) => process.stderr.write(`[vite] ${data}`));
    await waitForServer(baseUrl, vite, options.timeout);
  }
  browser = await puppeteer.launch(browserOptions(options));
  const results = [];
  for (const scene of options.scenes) {
    const name = safeName(scene);
    const legacyPath = path.join(options.out, `${name}.legacy.png`);
    const nextPath = path.join(options.out, `${name}.next.png`);
    const diffPath = path.join(options.out, `${name}.diff.png`);
    const compositePath = path.join(options.out, `${name}.comparison.png`);
    process.stdout.write(`... ${scene}\r`);
    try {
      const legacy = await renderViewport(browser, baseUrl, scene, 'legacy', legacyPath, options);
      const next = await renderViewport(browser, baseUrl, scene, 'next', nextPath, options);
      const metrics = compareImages(legacyPath, nextPath, diffPath, options);
      if (options.dumpComposite) {
        writeComposite(legacyPath, nextPath, diffPath, compositePath);
      }
      if (!options.dumpImages) {
        for (const imagePath of [legacyPath, nextPath, diffPath]) fs.rmSync(imagePath);
      }
      const result = {scene, passed: metrics.passed, legacy, next, metrics,
        images: {
          ...(options.dumpImages ? {legacy: legacyPath, next: nextPath, diff: diffPath} : {}),
          ...(options.dumpComposite ? {comparison: compositePath} : {}),
        }};
      results.push(result);
      console.log(`${metrics.passed ? 'OK  ' : 'FAIL'} ${scene}: ` +
        `${metrics.diffPercent.toFixed(2)}% pixels, mean RGB ${metrics.meanError.toFixed(4)}`);
    } catch (error) {
      results.push({scene, passed: false, error: error.message || String(error)});
      console.log(`FAIL ${scene}: ${error.message || error}`);
    }
  }
  const passed = results.filter((result) => result.passed).length;
  const summary = {
    generatedAt: new Date().toISOString(),
    renderer: options.hw && !options.sw ? 'angle-vulkan' : 'swiftshader',
    limits: {
      pixelmatchThreshold: options.threshold,
      maxDiffPercent: options.maxDiffPercent,
      maxMeanError: options.maxMeanError,
    },
    passed,
    failed: results.length - passed,
    results,
  };
  fs.writeFileSync(path.join(options.out, 'summary.json'), JSON.stringify(summary, null, 2));
  console.log(`\n${passed}/${results.length} visual comparisons passed; results: ${options.out}`);
  if (passed !== results.length) process.exitCode = 1;
} finally {
  await browser?.close().catch(() => {});
  if (vite && !options.keepServer) vite.kill('SIGTERM');
}
