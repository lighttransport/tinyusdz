#!/usr/bin/env node
/**
 * USD Assets Preview Generator
 *
 * Batch-renders preview images for every asset in the curated manifest.
 * Launches a Vite dev server for web/js/, opens each asset in headless
 * Chrome via usd-assets-view.html, waits for the render to complete, and
 * saves a screenshot.
 *
 * Usage:
 *   node generate-previews.js [--settings ./usd-assets-settings.json] [--assets ...]
 *
 * Settings file controls camera, clear color, and output resolution per
 * asset, with a "defaults" block that applies to every asset without
 * explicit overrides.
 *
 * Per-asset overrides are keyed by the asset's `id` field from the
 * manifest. Each override may contain { camera: {az, el, dist, fov,
 * padding}, clear: [r,g,b,a] }.
 *
 * Examples:
 *   node generate-previews.js
 *   node generate-previews.js --assets sphere,CesiumMan       # only these two
 *   node generate-previews.js --settings ./my-settings.json
 *   xvfb-run -a node generate-previews.js                     # headless env
 */

import { program } from 'commander';
import { createServer } from 'vite';
import puppeteer from 'puppeteer';
import fs from 'fs';
import fsp from 'fs/promises';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// Import the asset manifest from the demo source tree.
import { ASSETS } from '../demo/src/usd-assets-manifest.js';

const GITHUB_RAW = 'https://raw.githubusercontent.com/usd-wg/assets/main/';

const SETTINGS_PATH = path.join(__dirname, 'usd-assets-settings.json');
const WEB_JS_DIR = path.resolve(__dirname, '..', 'js');
const DEMO_DIR = path.resolve(__dirname, '..', 'demo');

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function assetUrl(a) {
  return GITHUB_RAW + a.repoPath + '/' + a.filename;
}
function baseUrl(a) {
  return GITHUB_RAW + a.repoPath + '/';
}
function fmtMB(b) {
  return (b / 1048576).toFixed(1) + ' MB';
}
function elapsed(start) {
  return ((performance.now() - start) / 1000).toFixed(1);
}

function resolveSettings(settings, assetId, categoryId) {
  const over = settings.assets?.[assetId] || {};
  const cat = settings.categories?.[categoryId] || {};
  const dflt = settings.defaults || {};
  const camera = { ...dflt.camera, ...(cat.camera || {}), ...(over.camera || {}) };
  const clear = over.clear || cat.clear || dflt.clear || null;
  const backend = over.backend || cat.backend || dflt.backend || 'legacy';
  return { camera, clear, backend };
}

function buildViewerUrl(serverUrl, asset, assetSettings) {
  const url = assetUrl(asset);
  const base = baseUrl(asset);
  const params = new URLSearchParams({
    uri: url,
    base,
    name: asset.filename,
    label: asset.name,
    ui: '0',
    camera: JSON.stringify(assetSettings.camera),
    backend: assetSettings.backend,
  });
  if (assetSettings.clear) {
    params.set('clear', assetSettings.clear.join(','));
  }
  return `${serverUrl}/usd-assets-view.html?${params.toString()}`;
}

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------

const RESET = '\x1b[0m';
const GREEN = '\x1b[32m';
const YELLOW = '\x1b[33m';
const RED = '\x1b[31m';
const CYAN = '\x1b[36m';
const BOLD = '\x1b[1m';
const DIM = '\x1b[2m';

function logStatus(emoji, label, msg, detail = '') {
  const d = detail ? ` ${DIM}(${detail})${RESET}` : '';
  console.log(`  ${emoji} ${BOLD}${label}${RESET}${d}`);
  if (msg) console.log(`        ${DIM}${msg}${RESET}`);
}

function logProgress(current, total, assetId, status, extra = '') {
  const pct = Math.round((current / total) * 100);
  const icon = status === 'ok' ? GREEN + '✓' : status === 'skip' ? YELLOW + '⊘' : RED + '✗';
  const line = `[${current}/${total}] ${String(pct).padStart(2)}% ${icon}${RESET} ${assetId}${extra ? DIM + ' ' + extra : ''}`;
  console.log(line);
}

// ---------------------------------------------------------------------------
// Vite dev server (web/js/)
// ---------------------------------------------------------------------------

async function startDevServer(verbose) {
  const server = await createServer({
    root: WEB_JS_DIR,
    configFile: false,
    server: {
      port: 0,
      strictPort: false,
      cors: true,
      fs: {
        allow: [WEB_JS_DIR],
      },
      headers: {
        'Cross-Origin-Opener-Policy': 'same-origin',
        'Cross-Origin-Embedder-Policy': 'require-corp',
      },
    },
    appType: 'mpa',
    resolve: {
      alias: [
        { find: 'tinyusdz', replacement: path.join(WEB_JS_DIR, 'src/tinyusdz') },
        { find: 'three', replacement: path.join(DEMO_DIR, 'node_modules/three') },
        { find: 'fzstd', replacement: path.join(DEMO_DIR, 'node_modules/fzstd') },
      ],
    },
    optimizeDeps: {
      exclude: ['tinyusdz'],
    },
    logLevel: verbose ? 'info' : 'silent',
  });
  await server.listen();
  const addr = server.httpServer.address();
  const url = `http://localhost:${addr.port}`;
  if (verbose) console.log(`  Dev server: ${url}`);
  return { url, close: () => server.close() };
}

// ---------------------------------------------------------------------------
// Puppeteer screenshot
// ---------------------------------------------------------------------------

async function screenshotAsset(page, viewerUrl, outputPath, timeoutMs, consoleLogs) {
  const logs = consoleLogs || [];
  await fsp.mkdir(path.dirname(outputPath), { recursive: true });

  await page.goto(viewerUrl, { waitUntil: 'load', timeout: 300000 });
  const realStart = Date.now();
  // The page has loaded (DOM + subresources). Now wait for the viewer's
  // render pipeline to finish (WASM load, fetch, compose, render).
  let timedOut = false;
  try {
    await page.waitForFunction(
      () => window.__usdAssetsViewer?.ready || window.__usdAssetsViewer?.error,
      { timeout: timeoutMs, polling: 1000 },
    );
  } catch (e) {
    timedOut = true;
  }

  logs.push(`[TIMING] waitForFunction completed in ${Date.now() - realStart}ms (timedOut=${timedOut})`);

  if (timedOut) {
    // Even if we timed out, check if the viewer object exists at all.
    const viewerState = await page.evaluate(() => {
      const v = window.__usdAssetsViewer;
      if (!v) return 'not found';
      return JSON.stringify({ ready: v.ready, error: v.error, hasStats: !!v.stats, url: v.url });
    }).catch(() => 'evaluate failed');
    logs.push(`[TIMING] viewerState=${viewerState}`);
    return { ok: false, error: `Render timed out after ${timeoutMs}ms`, console: logs };
  }

  // Check for errors
  const hasError = await page.evaluate(() => window.__usdAssetsViewer?.error || null);
  if (hasError) {
    return { ok: false, error: hasError, console: logs };
  }

  // Let a few frames settle for the GPU to finish drawing.
  await page.evaluate(() => new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r))));

  // Gather stats before screenshot.
  const stats = await page.evaluate(() => window.__usdAssetsViewer?.stats || null);

  await page.screenshot({ path: outputPath, type: 'png' });

  return { ok: true, stats, console: logs };
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

async function main() {
  program
    .description('Generate preview images for USD assets in the curated manifest')
    .option('--settings <path>', 'Path to settings JSON', SETTINGS_PATH)
    .option('--assets <ids>', 'Comma-separated asset IDs to render (default: all)')
    .option('--output <dir>', 'Output directory for preview images', './previews')
    .option('-w, --width <px>', 'Screenshot width', String(1280))
    .option('-H, --height <px>', 'Screenshot height', String(720))
    .option('--timeout <ms>', 'Per-asset render timeout', String(180000))
    .option('--concurrency <n>', 'Max parallel pages (not yet implemented; runs serially)', String(1))
    .option('--skip-existing', 'Skip assets that already have a preview image', false)
    .option('--chrome <path>', 'Chrome executable path')
    .option('--no-sandbox', 'Disable Chrome sandbox', false)
    .option('--swiftshader', 'Use SwiftShader (software) rendering', false)
    .option('--gpu', 'Enable hardware GPU rendering via --use-gl=angle --use-angle=vulkan', false)
    .option('-v, --verbose', 'Verbose output', false)
    .parse(process.argv);

  const opts = program.opts();
  const outputDir = path.resolve(opts.output);
  const settingsPath = path.resolve(opts.settings);
  const timeoutMs = parseInt(opts.timeout, 10);
  const width = parseInt(opts.width, 10);
  const height = parseInt(opts.height, 10);

  // ---- Read settings ----
  let settings;
  try {
    settings = JSON.parse(await fsp.readFile(settingsPath, 'utf-8'));
  } catch (e) {
    console.error(`Cannot read settings at ${settingsPath}: ${e.message}`);
    process.exit(1);
  }
  const outputWidth = settings.width || width;
  const outputHeight = settings.height || height;

  // ---- Select assets ----
  const allAssets = ASSETS;
  const selectedIds = opts.assets
    ? opts.assets.split(',').map((s) => s.trim()).filter(Boolean)
    : allAssets.map((a) => a.id);

  const toRender = allAssets.filter((a) => selectedIds.includes(a.id));
  if (toRender.length === 0) {
    console.error('No matching assets found in the manifest.');
    process.exit(1);
  }

  const skipped = [];

  // ---- Filter skip-existing ----
  const renderList = opts.skipExisting
    ? toRender.filter((a) => {
        const outPath = path.join(outputDir, a.category, a.id + '.png');
        if (fs.existsSync(outPath)) {
          skipped.push(a.id);
          return false;
        }
        return true;
      })
    : toRender;

  if (skipped.length > 0) {
    console.log(`  ${YELLOW}Skipping ${skipped.length} existing preview(s)${RESET}`);
  }

  // ---- Start dev server ----
  console.log(`\n  ${CYAN}Starting Vite dev server for web/js/...${RESET}`);
  const server = await startDevServer(opts.verbose);
  console.log(`  ${GREEN}Server ready:${RESET} ${server.url}`);

  // ---- Launch browser ----
  console.log(`  ${CYAN}Launching headless Chrome...${RESET}`);
  const chromePath = opts.chrome || process.env.CHROME_PATH || '/usr/bin/google-chrome';

  const launchArgs = [
    '--no-first-run',
    '--disable-dev-shm-usage',
    '--no-sandbox',
    '--disable-gpu-blocklist',
    `--window-size=${outputWidth},${outputHeight}`,
    'about:blank',
  ];

  if (opts.swiftshader) {
    launchArgs.push('--use-gl=angle', '--use-angle=swiftshader');
  } else if (opts.gpu) {
    launchArgs.push('--use-gl=angle', '--use-angle=vulkan', '--enable-features=Vulkan');
  } else {
    launchArgs.push('--enable-unsafe-swiftshader');
  }

  const browser = await puppeteer.launch({
    headless: 'new',
    executablePath: chromePath,
    args: launchArgs,
    env: {
      ...process.env,
      ...(opts.gpu ? {
        __NV_PRIME_RENDER_OFFLOAD: '1',
        __GLX_VENDOR_LIBRARY_NAME: 'nvidia',
        __EGL_VENDOR_LIBRARY_FILENAMES: '/usr/share/glvnd/egl_vendor.d/10_nvidia.json',
      } : {}),
    },
    dumpio: opts.verbose,
  });

  console.log(`  ${GREEN}Chrome:${RESET} ${await browser.version()}`);
  console.log(`  ${GREEN}Assets:${RESET} ${renderList.length} to render`);

  // ---- Render loop ----
  const results = [];
  const total = renderList.length;
  const startAll = performance.now();

  const page = await browser.newPage();
  await page.setViewport({ width: outputWidth, height: outputHeight });

  for (let i = 0; i < total; i++) {
    const asset = renderList[i];
    const assetSettings = resolveSettings(settings, asset.id, asset.category);
    const viewerUrl = buildViewerUrl(server.url, asset, assetSettings);
    const outputPath = path.join(outputDir, asset.category, asset.id + '.png');
    const t0 = performance.now();

    logStatus('→', asset.id, `viewer → ${viewerUrl.slice(0, 160)}…`);

    const consoleLogs = [];
    page.on('console', (msg) => {
      const args = msg.args()?.length > 0 ? ' ' + msg.args().map(a => a.toString()).join(' ') : '';
      consoleLogs.push(`[${msg.type()}] ${msg.text()}${args}`);
    });
    page.on('pageerror', (err) => { consoleLogs.push(`[PAGE ERROR] ${err.message}`); });
    page.on('response', (resp) => {
      if (resp.status() >= 400) {
        consoleLogs.push(`[HTTP ${resp.status()}] ${resp.url()}`);
      }
    });

    const result = await screenshotAsset(page, viewerUrl, outputPath, timeoutMs, consoleLogs);
    const secs = elapsed(t0);
    const size = result.ok
      ? fmtMB((await fsp.stat(outputPath)).size)
      : '';

    if (result.ok) {
      logProgress(i + 1, total, asset.id, 'ok', `${secs}s ${size}`);
      results.push({ id: asset.id, status: 'ok', stats: result.stats, path: outputPath, elapsed: secs });
    } else {
      logProgress(i + 1, total, asset.id, 'err', result.error);
      console.error(`    ${RED}Error:${RESET} ${result.error}`);
      if (result.console?.length) {
        for (const line of result.console) {
          console.error(`    ${DIM}${line}${RESET}`);
        }
      }
      // Also take a debug screenshot on failure
      try {
        const debugPath = outputPath.replace(/\.png$/, '.debug.png');
        await page.screenshot({ path: debugPath, type: 'png' });
        console.error(`    ${DIM}Debug screenshot: ${debugPath}${RESET}`);
      } catch (_) {}
      results.push({ id: asset.id, status: 'error', error: result.error, path: null });
    }
  }

  await browser.close();
  await server.close();

  // ---- Summary ----
  const totalTime = elapsed(startAll);
  const ok = results.filter((r) => r.status === 'ok').length;
  const err = results.filter((r) => r.status === 'error').length;

  console.log('\n' + '='.repeat(60));
  console.log(`  ${BOLD}SUMMARY${RESET}`);
  console.log('='.repeat(60));
  console.log(`  ${GREEN}✓ OK:${RESET}     ${ok}`);
  if (skipped.length) console.log(`  ${YELLOW}⊘ Skipped:${RESET} ${skipped.length}`);
  if (err) console.log(`  ${RED}✗ Errors:${RESET} ${err}`);
  console.log(`  ${BOLD}Total:${RESET}   ${total}`);
  console.log(`  Time:    ${totalTime}s`);
  console.log(`  Output:  ${path.resolve(outputDir)}`);
  console.log('='.repeat(60));

  // Exit with error code if any renders failed.
  if (err > 0) process.exit(1);
}

main().catch((e) => {
  console.error('Fatal:', e);
  process.exit(1);
});
