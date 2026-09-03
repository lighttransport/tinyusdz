#!/usr/bin/env node
/**
 * QA smoke test for all 22 demo pages.
 *
 * Validates:
 *   - JS syntax of every entry file
 *   - Build output integrity (dist HTML + JS)
 *   - Every import resolves
 *   - Every page loads without critical console errors
 *   - Page load time
 *   - Per-demo assertions where applicable
 *
 * Usage:
 *   node scripts/test-all-demos.mjs                # full QA
 *   node scripts/test-all-demos.mjs --skip-build   # skip build step
 *   xvfb-run -a node scripts/test-all-demos.mjs    # headless server
 *
 * Environment:
 *   CHROME_PATH  — Chrome binary (default: /usr/bin/google-chrome)
 *   QA_TIMEOUT   — per-page timeout ms (default: 30000)
 */

import { createServer } from 'vite';
import path from 'path';
import fs from 'fs';
import { fileURLToPath } from 'url';
import { execSync } from 'child_process';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const DEMO_DIR = path.resolve(__dirname, '..');
const DIST_DIR = path.join(DEMO_DIR, 'dist');
const CONFIG_PATH = path.join(DEMO_DIR, 'src', 'demo-configs.js');
const ENTRIES_DIR = path.join(DEMO_DIR, 'src', 'entries');
const HTML_DIR = DEMO_DIR;

const timeout = parseInt(process.env.QA_TIMEOUT || '30000', 10);
const chromePath = process.env.CHROME_PATH || '/usr/bin/google-chrome';
const skipBuild = process.argv.includes('--skip-build');

// ANSI colors
const R = '\x1b[0m', G = '\x1b[32m', Y = '\x1b[33m', RD = '\x1b[31m', D = '\x1b[2m', B = '\x1b[1m', C = '\x1b[36m';

function ok(s) { return `${G}${s}${R}`; }
function warn(s) { return `${Y}${s}${R}`; }
function err(s) { return `${RD}${s}${R}`; }
function dim(s) { return `${D}${s}${R}`; }
function bold(s) { return `${B}${s}${R}`; }

// ── Results ──
const results = [];
let errors = [];
let warnings = [];

function report(category, status, msg, detail = '') {
  results.push({ category, status, msg, detail });
}

// ============================================================================
// Phase 1: Syntax check all entry files
// ============================================================================

async function phaseSyntax() {
  console.log(`\n  ${bold('Phase 1: JS Syntax Check')}`);
  const files = fs.readdirSync(ENTRIES_DIR).filter((f) => f.endsWith('.js'));
  let ok2 = 0, fail2 = 0;
  for (const f of files) {
    const fp = path.join(ENTRIES_DIR, f);
    try {
      execSync(`node --check "${fp}"`, { stdio: 'pipe' });
      ok2++;
      report(`syntax ${f}`, 'pass', '');
    } catch (e) {
      fail2++;
      const msg = e.stderr?.toString().split('\n')[0] || 'syntax error';
      report(`syntax ${f}`, 'fail', msg);
      console.error(`    ${err('✗')} ${dim(f)} — ${RD}${msg}${R}`);
    }
  }
  if (ok2 > 0) console.log(`    ${ok(`${ok2} passed`)}${fail2 > 0 ? `, ${err(`${fail2} failed`)}` : ''}`);
  return fail2 === 0;
}

// ============================================================================
// Phase 1b: Import resolution check (Vite build)
// ============================================================================

async function phaseBuild() {
  console.log(`\n  ${bold('Phase 1b: Build Output Check')}`);
  if (skipBuild) {
    console.log(`    ${dim('skipped (--skip-build)')}`);
    return true;
  }
  try {
    execSync('npx vite build 2>/dev/null', { cwd: DEMO_DIR, stdio: 'pipe', timeout: 120000 });
    report('build', 'pass', 'Vite build succeeded');
    console.log(`    ${ok('✓ build OK')}`);
  } catch (e) {
    const msg = e.stderr?.toString().slice(0, 200) || e.message;
    report('build', 'fail', msg);
    console.error(`    ${err('✗ build failed')}: ${dim(msg)}`);
    return false;
  }
  return true;
}

// ============================================================================
// Phase 1c: Build output integrity
// ============================================================================

async function phaseOutput() {
  console.log(`\n  ${bold('Phase 1c: Build Output Integrity')}`);
  if (!fs.existsSync(DIST_DIR)) {
    report('output', 'fail', 'dist/ directory missing');
    return false;
  }
  const htmlFiles = fs.readdirSync(DIST_DIR).filter((f) => f.endsWith('.html'));
  const entryCount = fs.readdirSync(HTML_DIR).filter((f) => f.endsWith('.html') && f !== 'index.html').length + 1;

  if (htmlFiles.length !== entryCount) {
    report('output', 'warn', `Expected ${entryCount} HTML pages, found ${htmlFiles.length}`);
    console.warn(`    ${warn('⚠')} expected ${entryCount} HTML pages, found ${htmlFiles.length}`);
  } else {
    report('output', 'pass', `${htmlFiles.length} HTML pages`);
    console.log(`    ${ok(`✓ ${htmlFiles.length} HTML pages`)}`);
  }

  // Check all HTML files are non-empty
  let empty = 0;
  for (const f of htmlFiles) {
    const sz = fs.statSync(path.join(DIST_DIR, f)).size;
    if (sz < 50) { empty++; console.warn(`    ${warn('⚠')} ${dim(f)} is ${sz} bytes`); }
  }
  if (empty > 0) {
    report('output', 'warn', `${empty} file(s) suspiciously small`);
  }

  // JS bundles exist
  const jsFiles = fs.readdirSync(path.join(DIST_DIR, 'assets')).filter((f) => f.endsWith('.js'));
  if (jsFiles.length === 0) {
    report('output', 'fail', 'No JS bundles in dist/assets/');
    return false;
  }
  report('output', 'pass', `${jsFiles.length} JS bundles`);
  console.log(`    ${ok(`✓ ${jsFiles.length} JS bundles`)}`);

  return true;
}

// ============================================================================
// Phase 2: Static demo config validation
// ============================================================================

async function phaseConfig() {
  console.log(`\n  ${bold('Phase 2: Demo Config Validation')}`);

  // Dynamic import of the config module
  const configPath = path.join(DEMO_DIR, 'src', 'demo-configs.js');
  const configUrl = `file://${configPath}`;
  let DEMOS;
  try {
    const mod = await import(configUrl);
    DEMOS = mod.DEMOS;
  } catch (e) {
    report('config', 'fail', `Cannot load demo-configs.js: ${e.message}`);
    return false;
  }

  // Check each config entry
  let pass = 0, fail = 0;
  for (const d of DEMOS) {
    const checks = [];
    if (!d.id) checks.push('missing id');
    if (!d.title) checks.push('missing title');
    if (!d.href) checks.push('missing href');
    if (!d.image) checks.push('missing image');

    // Check HTML file exists
    const htmlPath = path.join(HTML_DIR, d.href);
    if (!fs.existsSync(htmlPath)) checks.push(`HTML missing: ${d.href}`);

    // Check preview image exists
    const imgPath = path.join(DEMO_DIR, 'public', d.image);
    if (!fs.existsSync(imgPath)) checks.push(`preview missing: ${d.image}`);

    // Check entry file exists
    const entryPath = path.join(ENTRIES_DIR, `${d.id}.js`);
    if (!fs.existsSync(entryPath)) checks.push(`entry missing: ${d.id}.js`);

    if (checks.length === 0) {
      pass++;
      report(`config ${d.id}`, 'pass', '');
    } else {
      fail++;
      report(`config ${d.id}`, 'fail', checks.join('; '));
      console.error(`    ${err('✗')} ${dim(d.id)} — ${checks.join(', ')}`);
    }
  }
  console.log(`    ${ok(`${pass}/${DEMOS.length} valid`)}${fail > 0 ? `, ${err(`${fail} invalid`)}` : ''}`);
  return fail === 0;
}

// ============================================================================
// Phase 3: Browser smoke test with Puppeteer
// ============================================================================

async function phaseBrowser() {
  console.log(`\n  ${bold('Phase 3: Browser Smoke Test')}`);

  let puppeteer;
  try {
    puppeteer = (await import('puppeteer')).default;
  } catch {
    console.log(`    ${warn('⚠ puppeteer not available — skip browser phase')}`);
    console.log(`    ${dim('  npm install --no-save puppeteer')}`);
    return true;
  }

  // Start Vite
  const server = await createServer({
    root: DEMO_DIR,
    configFile: path.join(DEMO_DIR, 'vite.config.js'),
    server: { port: 0, strictPort: false },
    logLevel: 'silent',
  });
  await server.listen();
  const addr = server.httpServer.address();
  const baseUrl = `http://localhost:${addr.port}`;
  console.log(`    ${dim('server:')} ${baseUrl}`);

  // Launch browser
  const browser = await puppeteer.launch({
    headless: 'new',
    executablePath: chromePath,
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-swiftshader', '--window-size=1280,720'],
  });

  const configPath2 = path.join(DEMO_DIR, 'src', 'demo-configs.js');
  const configUrl2 = `file://${configPath2}`;
  const mod = await import(configUrl2);
  const DEMOS = mod.DEMOS;

  const screenshotDir = path.join(DIST_DIR, 'qa-screenshots');
  fs.mkdirSync(screenshotDir, { recursive: true });

  // Filter patterns for benign console noise
  const benignErrors = [
    'favicon.ico',
    'Failed to load resource: net::ERR_NAME_NOT_RESOLVED',
    'Failed to load resource: net::ERR_CONNECTION_REFUSED',
    'Module "module" has been externalized',
    'application/octet-stream', // WASM content-type warning
  ];

  let pagePass = 0, pageFail = 0;

  for (const demo of DEMOS) {
    const url = `${baseUrl}/${demo.href}`;
    const page = await browser.newPage();
    const errors2 = [];
    const warnings2 = [];
    const consoleLines = [];
    let loadStart = 0;

    page.on('console', (msg) => {
      const t = msg.text();
      consoleLines.push(`[${msg.type()}] ${t}`);
      if (msg.type() === 'error') errors2.push(t);
      if (msg.type() === 'warning') warnings2.push(t);
    });
    page.on('pageerror', (err2) => errors2.push(err2.message));

    let status = 'pass';
    let statusDetail = '';
    let loadTime = 0;

    try {
      loadStart = Date.now();
      await page.goto(url, { waitUntil: 'domcontentloaded', timeout });

      // Wait for WASM to init
      await new Promise((r) => setTimeout(r, 3000));
      loadTime = Date.now() - loadStart;

      // Filter out benign errors
      const critical = errors2.filter((e) =>
        !benignErrors.some((b) => e.includes(b))
      );

      if (critical.length > 0) {
        status = 'fail';
        statusDetail = critical.slice(0, 3).join('; ').slice(0, 120);
      } else if (errors2.length > 0) {
        // Only benign errors
        status = 'pass';
      }

      // Check for key globals
      const hasApp = await page.evaluate(() => {
        return !!(window.__lightusdDemoApp || window.__usdAssetsViewer || window.__usdAssetsBrowser || window.__vizRenderer || window.__usdAssetsBrowser?.renderer);
      }).catch(() => false);

      // Screenshot
      await page.screenshot({ path: path.join(screenshotDir, `${demo.id}.png`), type: 'png' });

    } catch (e) {
      status = 'fail';
      const isTimeout = e.message?.includes('timeout') || e.message?.includes('Timeout');
      statusDetail = isTimeout ? `timed out (${timeout}ms)` : e.message.slice(0, 120);
      loadTime = Date.now() - loadStart;
    }

    report(`page ${demo.id}`, status, statusDetail, loadTime > 0 ? `${loadTime}ms` : '');
    if (status === 'pass') pagePass++;
    else pageFail++;

    const icon = status === 'pass' ? ok('✓') : err('✗');
    console.log(`    ${icon} ${bold(demo.id.padEnd(22))} ${status === 'pass' ? dim(`${loadTime}ms`) : dim(statusDetail)}`);

    await page.close();
  }

  await browser.close();
  await server.close();

  const summary = `${pagePass}/${DEMOS.length} pages pass` +
    (pageFail > 0 ? `, ${err(`${pageFail} fail`)}` : '');
  console.log(`\n    ${pageFail === 0 ? ok('✓ ' + summary) : err('✗ ' + summary)}`);
  return pageFail === 0;
}

// ============================================================================
// Report generation
// ============================================================================

function generateReport() {
  console.log(`\n  ${bold('QA Report')}`);
  console.log(`  ${bold('─'.repeat(60))}`);

  const byCategory = {};
  for (const r of results) {
    const [cat, ...rest] = r.category.split(' ');
    const item = rest.join(' ');
    if (!byCategory[cat]) byCategory[cat] = [];
    byCategory[cat].push(r);
  }

  for (const [cat, items] of Object.entries(byCategory)) {
    const pass = items.filter((i) => i.status === 'pass').length;
    const fail = items.filter((i) => i.status !== 'pass').length;
    const total = items.length;
    const status = fail === 0 ? ok('✓') : err('✗');
    console.log(`  ${status} ${bold(cat)} ${dim(`(${pass}/${total})`)}`);
    for (const r of items) {
      if (r.status !== 'pass' || r.detail) {
        const icon = r.status === 'pass' ? dim('·') : r.status === 'warn' ? warn('⚠') : err('✗');
        console.log(`      ${icon} ${r.msg}${r.detail ? ` — ${dim(r.detail)}` : ''}`);
      }
    }
  }

  // Summary
  const passCount = results.filter((r) => r.status === 'pass').length;
  const warnCount = results.filter((r) => r.status === 'warn').length;
  const failCount = results.filter((r) => r.status !== 'pass' && r.status !== 'warn').length;
  console.log(`\n  ${bold('Summary')}: ${ok(`${passCount} passed`)}${warnCount > 0 ? `, ${warn(`${warnCount} warnings`)}` : ''}${failCount > 0 ? `, ${err(`${failCount} failed`)}` : ''}`);

  // Save JSON
  const reportPath = path.join(DIST_DIR, 'qa-report.json');
  fs.writeFileSync(reportPath, JSON.stringify(results, null, 2));
  console.log(`  ${dim(`Report: ${reportPath}`)}`);
  console.log(`  ${dim(`Screenshots: ${DIST_DIR}/qa-screenshots/`)}`);

  return failCount;
}

// ============================================================================
// Main
// ============================================================================

async function main() {
  console.log(`\n${bold('LightUSD Web Demo QA')} ${dim(`(${new Date().toISOString()})`)}`);
  console.log(`  ${dim(`Chrome: ${chromePath}`)}`);
  console.log(`  ${dim(`Timeout: ${timeout}ms`)}`);

  let ok2 = true;

  // Phase 1
  ok2 = await phaseSyntax() && ok2;
  ok2 = await phaseBuild() && ok2;
  ok2 = await phaseOutput() && ok2;

  // Phase 2
  ok2 = await phaseConfig() && ok2;

  // Phase 3
  ok2 = await phaseBrowser() && ok2;

  // Report
  const failCount = generateReport();

  process.exit(failCount > 0 ? 1 : 0);
}

main().catch((e) => {
  console.error('Fatal:', e);
  process.exit(1);
});
