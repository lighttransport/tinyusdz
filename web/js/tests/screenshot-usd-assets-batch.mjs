#!/usr/bin/env node
// Batch screenshotter for usd-wg/assets test_assets through TinyUSDZ WebGL.
//
// Run from web/js:
//   xvfb-run -a node tests/screenshot-usd-assets-batch.mjs --hw
//   node tests/screenshot-usd-assets-batch.mjs --sw --limit 4
//   node tests/screenshot-usd-assets-batch.mjs --remote-base https://raw.githubusercontent.com/usd-wg/assets/main/test_assets/
//
// Defaults:
//   local assets root : /mnt/nvme02/work/usd/assets
//   scan folder       : <assets>/test_assets (or --set full_assets)
//   output            : tests/screenshots/usd-assets
//   catalog           : catalog.png, 4 columns, 2500 px wide

import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import puppeteer from 'puppeteer';

const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url));
const WEB_JS_DIR = path.resolve(SCRIPT_DIR, '..');
const DEFAULT_ASSETS = '/mnt/nvme02/work/usd/assets';
const USD_EXTS = new Set(['.usd', '.usda', '.usdc', '.usdz']);
const SKIP_DIRS = new Set(['screenshots', 'screenshot', 'thumbnails', 'thumbnail', 'cards', 'card', '.git']);

function parseArgs(argv = process.argv.slice(2)) {
  const opts = {
    assets: path.resolve(process.env.USD_WG_ASSETS_DIR || DEFAULT_ASSETS),
    testAssets: '',
    remoteBase: '',
    set: 'test_assets',
    out: path.join(SCRIPT_DIR, 'screenshots', 'usd-assets'),
    port: 5190,
    width: 1212,
    height: 413,
    headful: false,
    hw: false,
    sw: false,
    timeout: 180000,
    limit: Infinity,
    all: false,
    includeCommon: false,
    allowBlank: false,
    catalog: true,
    catalogColumns: 4,
    catalogWidth: 2500,
    config: path.join(SCRIPT_DIR, 'usd-assets-camera.json'),
    explicit: [],
  };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === '-h' || a === '--help') { printHelp(); process.exit(0); }
    else if (a === '--assets') opts.assets = path.resolve(argv[++i]);
    else if (a === '--test-assets') opts.testAssets = path.resolve(argv[++i]);
    else if (a === '--full-assets') { opts.set = 'full_assets'; opts.testAssets = path.join(opts.assets, 'full_assets'); }
    else if (a === '--set') {
      opts.set = argv[++i];
      if (opts.set !== 'test_assets' && opts.set !== 'full_assets') {
        throw new Error(`--set must be test_assets or full_assets (got ${opts.set})`);
      }
    }
    else if (a === '--remote-base') opts.remoteBase = argv[++i].replace(/\/?$/, '/');
    else if (a === '--out') opts.out = path.resolve(argv[++i]);
    else if (a === '--port') opts.port = Number(argv[++i]);
    else if (a === '--width') opts.width = Number(argv[++i]);
    else if (a === '--height') opts.height = Number(argv[++i]);
    else if (a === '--headful') opts.headful = true;
    else if (a === '--hw') opts.hw = true;
    else if (a === '--sw') opts.sw = true;
    else if (a === '--timeout') opts.timeout = Number(argv[++i]);
    else if (a === '--limit') opts.limit = Number(argv[++i]);
    else if (a === '--all') opts.all = true;
    else if (a === '--include-common') opts.includeCommon = true;
    else if (a === '--allow-blank') opts.allowBlank = true;
    else if (a === '--catalog') opts.catalog = true;
    else if (a === '--no-catalog') opts.catalog = false;
    else if (a === '--catalog-columns') opts.catalogColumns = Number(argv[++i]);
    else if (a === '--catalog-width') opts.catalogWidth = Number(argv[++i]);
    else if (a === '--config') opts.config = path.resolve(argv[++i]);
    else if (a.startsWith('-')) throw new Error(`Unknown option: ${a}`);
    else opts.explicit.push(a);
  }
  return opts;
}

function printHelp() {
  console.log(`Usage: node tests/screenshot-usd-assets-batch.mjs [options] [file-or-url ...]

Options:
  --assets <dir>             usd-wg/assets root (default: USD_WG_ASSETS_DIR or ${DEFAULT_ASSETS})
  --set <name>               Asset set: test_assets or full_assets (default: test_assets)
  --full-assets              Shortcut for --set full_assets
  --test-assets <dir>        Asset-set folder override
  --remote-base <url>        Fetch discovered local relpaths from this HTTP base
  --out <dir>                Output directory (default: tests/screenshots/usd-assets)
  --port <n>                 Vite dev-server port (default: 5190)
  --width/--height <px>      Viewport size (default: 1212x413)
  --hw                       ANGLE/Vulkan GPU path; run under xvfb-run -a
  --sw                       Force true-headless SwiftShader
  --headful                  Visible browser, for debugging
  --timeout <ms>             Per-scene timeout (default: 180000)
  --limit <n>                Limit discovered scenes
  --all                      Include nested support USD files
  --include-common           Include test_assets/_common in discovery
  --allow-blank              Do not fail scenes with zero rendered meshes
  --config <json>            Camera override config (default: tests/usd-assets-camera.json)
  --catalog-columns <n>      Preview catalog columns (default: 4)
  --catalog-width <px>       Preview catalog width (default: 2500)
  --no-catalog               Skip catalog.png generation
  -h, --help`);
}

function posixPath(p) { return p.split(path.sep).join('/'); }
function urlDir(url) { const i = url.lastIndexOf('/'); return i >= 0 ? url.slice(0, i + 1) : './'; }
function fileUrlPath(abs) { return `/@fs${posixPath(abs)}`; }
function safeName(s) {
  return String(s).replace(/\\/g, '/').replace(/^\/+/, '').replace(/[^A-Za-z0-9._-]+/g, '__').slice(0, 220);
}
function isHttp(s) { return /^https?:\/\//i.test(s); }

function testAssetsRoot(opts) {
  if (opts.testAssets) return opts.testAssets;
  if (path.basename(opts.assets) === opts.set) return opts.assets;
  return path.join(opts.assets, opts.set);
}

function discoverLocalScenes(root, opts) {
  const out = [];
  const walk = (dir) => {
    for (const ent of fs.readdirSync(dir, { withFileTypes: true })) {
      if (ent.name.startsWith('.') || SKIP_DIRS.has(ent.name)) continue;
      const p = path.join(dir, ent.name);
      const rel = posixPath(path.relative(root, p));
      if (ent.isDirectory()) {
        if (!opts.includeCommon && rel === '_common') continue;
        walk(p);
      } else if (ent.isFile() && USD_EXTS.has(path.extname(ent.name).toLowerCase())) {
        if (!opts.all && !isDefaultRoot(rel, opts.set)) continue;
        out.push({ kind: 'local', file: p, rel, label: rel });
      }
    }
  };
  walk(root);
  out.sort((a, b) => a.rel.localeCompare(b.rel));
  return out;
}

function isDefaultRoot(rel, setName) {
  const parts = rel.split('/');
  if (setName === 'full_assets') return isDefaultFullAssetRoot(parts);
  if (parts.length === 2) return !parts[1].startsWith('_');
  if (parts[0] === 'USDZ' && parts.length === 3 && /\.usdz$/i.test(parts[2])) return true;
  return false;
}

function isDefaultFullAssetRoot(parts) {
  if (!parts.length) return false;
  const name = parts[parts.length - 1];
  if (/(?:^|[_-])thumbnail\.usd[ac]?$/i.test(name)) return false;
  if (parts.includes('assets') || parts.includes('layers') || parts.includes('geo') ||
      parts.includes('materials') || parts.includes('material_surface_geo')) return false;
  if (parts.length === 2) return true;
  return parts.join('/') === 'Vehicles/USD_Mini_Car_Kit/USD_Mini_Car.usda';
}

function scenesFromExplicit(args, root) {
  return args.map((arg) => {
    if (isHttp(arg)) {
      const rel = decodeURIComponent(arg.split(/[?#]/)[0].split('/').slice(-2).join('/'));
      return { kind: 'http', url: arg, rel, label: rel || arg };
    }
    const file = path.resolve(arg);
    const rel = fs.existsSync(root) ? posixPath(path.relative(root, file)) : path.basename(file);
    return { kind: 'local', file, rel, label: rel };
  });
}

function applyRemoteBase(scenes, remoteBase) {
  if (!remoteBase) return scenes;
  return scenes.map((s) => ({
    kind: 'http',
    url: new URL(s.rel, remoteBase).href,
    rel: s.rel,
    label: s.rel,
  }));
}

function loadCameraConfig(file) {
  if (!file || !fs.existsSync(file)) return { defaults: {}, scenes: {} };
  const data = JSON.parse(fs.readFileSync(file, 'utf8'));
  return {
    defaults: data.defaults || {},
    scenes: data.scenes || {},
  };
}

function cameraFor(scene, cfg) {
  return {
    ...(cfg.defaults?.camera || {}),
    ...(cfg.scenes?.[scene.rel]?.camera || {}),
  };
}

function sceneUrlAndBase(scene, baseUrl) {
  if (scene.kind === 'http') {
    return { url: scene.url, base: urlDir(scene.url), name: path.posix.basename(scene.url.split(/[?#]/)[0]) };
  }
  const rootUrl = `${baseUrl}${fileUrlPath(scene.file)}`;
  return { url: rootUrl, base: urlDir(rootUrl), name: path.basename(scene.file) };
}

function collectPreloads(scene, opts, root, baseUrl) {
  if (scene.kind !== 'local' || !scene.file || !fs.existsSync(scene.file)) return [];
  const parts = scene.rel.split('/');
  if (parts.length < 2) return [];
  const assetDir = path.join(root, parts[0]);
  if (!fs.existsSync(assetDir) || !fs.statSync(assetDir).isDirectory()) return [];
  const sceneDir = path.dirname(scene.file);
  const out = [];
  const walk = (dir, depth) => {
    if (depth > 5) return;
    for (const ent of fs.readdirSync(dir, { withFileTypes: true })) {
      if (ent.name.startsWith('.') || SKIP_DIRS.has(ent.name)) continue;
      const p = path.join(dir, ent.name);
      if (ent.isDirectory()) walk(p, depth + 1);
      else if (ent.isFile() && USD_EXTS.has(path.extname(ent.name).toLowerCase()) && p !== scene.file) {
        out.push({
          key: posixPath(path.relative(sceneDir, p)),
          url: `${baseUrl}${fileUrlPath(p)}`,
        });
      }
    }
  };
  walk(assetDir, 0);
  return out;
}

async function waitForServer(url, timeoutMs, proc = null) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (proc && proc.exitCode !== null) {
      throw new Error(`vite server exited before becoming ready (exit ${proc.exitCode})`);
    }
    try {
      const res = await fetch(url);
      if (res.ok) return true;
    } catch (_) {}
    await new Promise((r) => setTimeout(r, 300));
  }
  throw new Error(`vite server did not become ready at ${url}`);
}

function startVite(port, opts) {
  const bin = path.join(WEB_JS_DIR, 'node_modules', '.bin', 'vite');
  const proc = spawn(bin, ['--host', '127.0.0.1', '--port', String(port), '--strictPort'], {
    cwd: WEB_JS_DIR,
    stdio: ['ignore', 'pipe', 'pipe'],
    env: { ...process.env, USD_WG_ASSETS_DIR: opts.testAssets || opts.assets },
  });
  proc.stdout.on('data', () => {});
  proc.stderr.on('data', (d) => process.stderr.write(`[vite] ${d}`));
  return proc;
}

function browserLaunchOptions(opts) {
  const NV_ICD = '/usr/share/vulkan/icd.d/nvidia_icd.json';
  const NV_EGL = '/usr/share/glvnd/egl_vendor.d/10_nvidia.json';
  let useHw = opts.hw && !opts.sw;
  if (useHw) {
    const reasons = [];
    if (!process.env.DISPLAY) reasons.push('no DISPLAY (run under `xvfb-run -a`)');
    if (!fs.existsSync(NV_ICD) && !fs.existsSync(NV_EGL)) reasons.push('no NVIDIA Vulkan/EGL driver');
    if (reasons.length) {
      useHw = false;
      console.warn(`  (note) --hw unavailable: ${reasons.join('; ')}; falling back to headless SwiftShader.`);
    }
  }
  const commonArgs = ['--no-sandbox', '--disable-setuid-sandbox', '--disable-dev-shm-usage',
    '--ignore-gpu-blocklist', '--disable-gpu-blocklist', `--window-size=${opts.width},${opts.height}`,
    '--disable-backgrounding-occluded-windows', '--disable-renderer-backgrounding',
    '--disable-background-timer-throttling', '--disable-gpu-vsync', '--disable-frame-rate-limit'];
  const args = useHw
    ? [...commonArgs, '--use-gl=angle', '--use-angle=vulkan', '--enable-features=Vulkan',
       '--enable-gpu-rasterization', '--enable-zero-copy']
    : [...commonArgs, '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'];
  const hwEnv = useHw ? {
    __NV_PRIME_RENDER_OFFLOAD: '1',
    __GLX_VENDOR_LIBRARY_NAME: 'nvidia',
    __EGL_VENDOR_LIBRARY_FILENAMES: '/usr/share/glvnd/egl_vendor.d/10_nvidia.json',
  } : {};
  return {
    useHw,
    launch: {
      headless: (useHw || opts.headful) ? false : true,
      args,
      env: { ...process.env, ...hwEnv },
    },
  };
}

async function shootScene(browser, baseUrl, scene, opts, cfg, root) {
  const page = await browser.newPage();
  page.setDefaultTimeout(opts.timeout);
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e.message || e)));
  page.on('error', (e) => errors.push(`PAGE CRASH: ${e.message || e}`));
  page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });

  const outPath = path.join(opts.out, `${safeName(scene.rel)}.png`);
  const failPath = path.join(opts.out, `${safeName(scene.rel)}.FAIL.png`);
  try {
    if (scene.kind === 'local' && !fs.existsSync(scene.file)) {
      throw new Error(`missing local file: ${scene.file}`);
    }
    await page.setViewport({ width: opts.width, height: opts.height, deviceScaleFactor: 1 });
    const io = sceneUrlAndBase(scene, baseUrl);
    const camera = cameraFor(scene, cfg);
    const preload = collectPreloads(scene, opts, root, baseUrl);
    const params = new URLSearchParams({
      url: io.url,
      base: io.base,
      name: io.name,
      label: scene.rel,
      ui: '0',
      camera: JSON.stringify(camera),
    });
    if (preload.length) params.set('preload', JSON.stringify(preload));
    await page.goto(`${baseUrl}/usd-assets-view.html?${params.toString()}`, { waitUntil: 'load', timeout: opts.timeout });
    await page.waitForFunction(() => {
      const v = window.__usdAssetsViewer;
      return !!v && (v.ready || v.error);
    }, { timeout: opts.timeout });
    const state = await page.evaluate(() => window.__usdAssetsViewer);
    if (state.error) throw new Error(state.error);
    if (!opts.allowBlank && (!state.stats || state.stats.meshes <= 0)) throw new Error('blank render (0 meshes)');
    await page.evaluate(() => new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r))));
    await page.screenshot({ path: outPath });
    return { ok: true, scene: scene.rel, outPath, stats: state.stats, errors };
  } catch (err) {
    await page.screenshot({ path: failPath }).catch(() => {});
    return { ok: false, scene: scene.rel, kind: failureKind(err.message), error: err.message, failPath, errors };
  } finally {
    await page.close().catch(() => {});
  }
}

function failureKind(message) {
  const s = String(message || '');
  if (/blank render/i.test(s)) return 'blank-render';
  if (/loadAsLayerFromBinary|Failed to load USD/i.test(s)) return 'parse-load';
  if (/Asset not found in cache|Composite|composeReferences|composePayload|composeSublayers/i.test(s)) return 'composition';
  if (/SkelAnimation|blendShapes/i.test(s)) return 'convert-skel-animation';
  if (/not a Material Prim|shader-network/i.test(s)) return 'convert-material-binding';
  if (/layerToRenderScene|ConvertToRenderScene|RenderScene/i.test(s)) return 'convert-render-scene';
  return 'unknown';
}

function imageDataUri(file) {
  return `data:image/png;base64,${fs.readFileSync(file).toString('base64')}`;
}

async function buildCatalog(browser, rows, opts) {
  const okRows = rows.filter((r) => r.ok && r.outPath && fs.existsSync(r.outPath));
  if (!okRows.length) return null;
  const columns = Math.max(1, opts.catalogColumns | 0);
  const width = Math.max(320, opts.catalogWidth | 0);
  const gap = 18;
  const labelH = 34;
  const cellW = Math.floor((width - gap * (columns + 1)) / columns);
  const shots = okRows.map((r) => ({ label: r.scene, src: imageDataUri(r.outPath) }));
  const html = `<!doctype html><html><head><style>
    *{box-sizing:border-box} body{margin:0;background:#202124;color:#e6e6e6;font:18px system-ui,sans-serif}
    .grid{width:${width}px;padding:${gap}px;display:grid;grid-template-columns:repeat(${columns},${cellW}px);gap:${gap}px;align-items:start}
    .cell{background:#303134;border:1px solid #555;border-radius:6px;overflow:hidden}
    img{width:${cellW}px;display:block;background:#474747}
    .label{height:${labelH}px;display:flex;align-items:center;padding:0 10px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;font-size:14px}
  </style></head><body><div class="grid">${shots.map((s) =>
    `<div class="cell"><img src="${s.src}"><div class="label">${escapeHtml(s.label)}</div></div>`).join('')}</div></body></html>`;
  const page = await browser.newPage();
  await page.setViewport({ width, height: 1000, deviceScaleFactor: 1 });
  await page.setContent(html, { waitUntil: 'load' });
  await page.waitForFunction(() => Array.from(document.images).every((img) => img.complete));
  const box = await page.$eval('.grid', (el) => {
    const r = el.getBoundingClientRect();
    return { x: 0, y: 0, width: Math.ceil(r.width), height: Math.ceil(r.height) };
  });
  await page.setViewport({ width: Math.ceil(box.width), height: Math.ceil(box.height), deviceScaleFactor: 1 });
  const outPath = path.join(opts.out, 'catalog.png');
  await page.screenshot({ path: outPath, clip: box });
  await page.close();
  return outPath;
}

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

async function main() {
  const opts = parseArgs();
  fs.mkdirSync(opts.out, { recursive: true });
  const root = testAssetsRoot(opts);
  const cfg = loadCameraConfig(opts.config);

  let scenes = opts.explicit.length ? scenesFromExplicit(opts.explicit, root) : discoverLocalScenes(root, opts);
  scenes = applyRemoteBase(scenes, opts.remoteBase);
  if (Number.isFinite(opts.limit)) scenes = scenes.slice(0, opts.limit);
  if (!scenes.length) throw new Error(`No USD assets found. Checked ${root}`);

  const baseUrl = `http://127.0.0.1:${opts.port}`;
  const vite = startVite(opts.port, opts);
  let browser;
  const rows = [];
  try {
    await waitForServer(`${baseUrl}/usd-assets-view.html`, 30000, vite);
    const launch = browserLaunchOptions(opts);
    browser = await puppeteer.launch(launch.launch);
    console.log(`Screenshotting ${scenes.length} USD asset(s) -> ${opts.out} (${launch.useHw ? 'ANGLE/Vulkan GPU' : 'SwiftShader/headless'})`);

    for (const scene of scenes) {
      process.stdout.write(`  ...   ${scene.rel}`);
      // eslint-disable-next-line no-await-in-loop
      const res = await shootScene(browser, baseUrl, scene, opts, cfg, root);
      rows.push(res);
      if (res.ok) {
        const st = res.stats || {};
        console.log(`\r  OK    ${scene.rel}  [meshes=${st.meshes ?? '?'} textures=${st.result?.textures ?? '?'}] -> ${path.basename(res.outPath)}`);
      } else {
        console.log(`\r  FAIL  ${scene.rel} [${res.kind}]: ${res.error}`);
        if (res.errors?.length) console.log(`          page: ${res.errors.slice(0, 2).join(' | ')}`);
      }
    }

    if (opts.catalog) {
      const catalog = await buildCatalog(browser, rows, opts);
      if (catalog) console.log(`  CAT   ${path.basename(catalog)} (${opts.catalogColumns} cols, ${opts.catalogWidth}px wide)`);
    }
  } finally {
    if (browser) await browser.close().catch(() => {});
    vite.kill('SIGTERM');
  }

  const jsonPath = path.join(opts.out, 'summary.json');
  const tsvPath = path.join(opts.out, 'summary.tsv');
  fs.writeFileSync(jsonPath, JSON.stringify({
    generatedAt: new Date().toISOString(),
    assets: opts.assets,
    testAssets: root,
    rows,
  }, null, 2));
  fs.writeFileSync(tsvPath, ['status\tkind\tscene\tmeshes\ttextures\tms\tfile\terror',
    ...rows.map((r) => [
      r.ok ? 'PASS' : 'FAIL',
      r.kind || '',
      r.scene,
      r.stats?.meshes ?? '',
      r.stats?.result?.textures ?? '',
      r.stats?.elapsedMs ?? '',
      r.ok ? r.outPath : r.failPath,
      r.error || '',
    ].join('\t')),
  ].join('\n') + '\n');

  const pass = rows.filter((r) => r.ok).length;
  console.log(`\n==== usd-assets screenshots: ${pass}/${rows.length} OK -> ${opts.out} ====`);
  process.exit(pass === rows.length ? 0 : 1);
}

main().catch((err) => {
  console.error(`screenshot-usd-assets-batch: ${err.message}`);
  console.error(err.stack);
  process.exit(1);
});
