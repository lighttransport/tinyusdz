#!/usr/bin/env node
/**
 * Automated QA test for all demo pages.
 *
 * Starts Vite dev server, opens each demo page in headless Chrome,
 * checks for console errors, and reports pass/fail per page.
 *
 * Usage:
 *   node scripts/test-all-demos.mjs
 *   xvfb-run -a node scripts/test-all-demos.mjs   # headless server
 *
 * Environment:
 *   CHROME_PATH  — path to Chrome/Chromium binary (default: auto-detect)
 *   TEST_TIMEOUT — per-page timeout in ms (default: 30000)
 */

import { createServer } from 'vite';
import puppeteer from 'puppeteer';
import path from 'path';
import fs from 'fs';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const DEMO_DIR = path.resolve(__dirname, '..');
const DEMO_CONFIG = path.join(DEMO_DIR, 'src', 'demo-configs.js');

const timeout = parseInt(process.env.TEST_TIMEOUT || '30000', 10);
const chromePath = process.env.CHROME_PATH || '/usr/bin/google-chrome';

const RESET = '\x1b[0m';
const GREEN = '\x1b[32m';
const RED = '\x1b[31m';
const YELLOW = '\x1b[33m';
const DIM = '\x1b[2m';
const BOLD = '\x1b[1m';

// ── Parse demo config ──

let DEMOS;
try {
  // Use dynamic import to load the config
  const configModule = await import(path.join(DEMO_DIR, 'src', 'demo-configs.js'));
  DEMOS = configModule.DEMOS;
} catch (e) {
  console.error('Cannot load demo-configs.js:', e.message);
  process.exit(1);
}

console.log(`\n  ${BOLD}QA Test: ${DEMOS.length} demo pages${RESET}\n`);

// ── Start Vite ──
console.log(`  ${YELLOW}Starting Vite dev server...${RESET}`);

const server = await createServer({
  root: DEMO_DIR,
  configFile: path.join(DEMO_DIR, 'vite.config.js'),
  server: { port: 0, strictPort: false },
  logLevel: 'silent',
});
await server.listen();
const addr = server.httpServer.address();
const baseUrl = `http://localhost:${addr.port}`;
console.log(`  Server: ${baseUrl}\n`);

// ── Launch browser ──
const browser = await puppeteer.launch({
  headless: 'new',
  executablePath: chromePath,
  args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-swiftshader', '--window-size=1280,720'],
});

// ── Results ──
const results = [];

for (const demo of DEMOS) {
  const url = `${baseUrl}/${demo.href}`;
  console.log(`  ${BOLD}${demo.id}${RESET} → ${DIM}${url}${RESET}`);

  const page = await browser.newPage();
  const errors = [];
  const warnings = [];

  page.on('console', (msg) => {
    const text = msg.text();
    if (msg.type() === 'error') errors.push(text);
    if (msg.type() === 'warning') warnings.push(text);
  });
  page.on('pageerror', (err) => errors.push(err.message));

  let status = 'ok';
  let statusDetail = '';

  try {
    await page.goto(url, { waitUntil: 'domcontentloaded', timeout });

    // Wait a bit for WASM to load and JS to execute
    await new Promise((r) => setTimeout(r, 2000));

    // Check for critical errors
    const criticalErrors = errors.filter((e) =>
      !e.includes('favicon') &&
      !e.includes('Failed to load resource: net::ERR_NAME_NOT_RESOLVED') &&
      !e.includes('404') &&
      !e.includes('Failed to load module') &&
      !e.includes('Module "module" has been externalized')
    );

    if (criticalErrors.length > 0) {
      status = 'error';
      statusDetail = criticalErrors.slice(0, 3).join('; ');
    } else if (warnings.length > 5) {
      status = 'warn';
      statusDetail = `${warnings.length} warnings`;
    }

    // Check if the demo initialized its viewer
    const hasViewer = await page.evaluate(() => {
      return !!(window.__tinyusdzDemoApp || window.__usdAssetsViewer || window.__usdAssetsBrowser || window.__vizRenderer);
    }).catch(() => false);

    // Take a screenshot for visual inspection
    const screenshotDir = path.join(DEMO_DIR, 'dist', 'qa-screenshots');
    fs.mkdirSync(screenshotDir, { recursive: true });
    await page.screenshot({ path: path.join(screenshotDir, `${demo.id}.png`), type: 'png' });

  } catch (e) {
    status = 'timeout';
    statusDetail = e.message?.includes('timeout') ? `Timed out after ${timeout}ms` : e.message;
  }

  results.push({ id: demo.id, status, detail: statusDetail, errors: errors.length, warnings: warnings.length });
  await page.close();

  const icon = status === 'ok' ? `${GREEN}✓${RESET}` : status === 'warn' ? `${YELLOW}⚠${RESET}` : `${RED}✗${RESET}`;
  console.log(`    ${icon} ${status}${statusDetail ? ` (${DIM}${statusDetail}${RESET})` : ''}`);
  if (errors.length > 0) {
    console.log(`      ${DIM}console errors: ${errors.length}${RESET}`);
  }
}

// ── Summary ──
await browser.close();
await server.close();

console.log(`\n  ${BOLD}${'='.repeat(50)}${RESET}`);
console.log(`  ${BOLD}QA Summary${RESET}`);
console.log(`  ${BOLD}${'='.repeat(50)}${RESET}`);
const pass = results.filter((r) => r.status === 'ok').length;
const warn = results.filter((r) => r.status === 'warn').length;
const fail = results.filter((r) => r.status !== 'ok' && r.status !== 'warn').length;
console.log(`  ${GREEN}✓ Pass:${RESET}  ${pass}/${results.length}`);
if (warn) console.log(`  ${YELLOW}⚠ Warn:${RESET}  ${warn}`);
if (fail) {
  console.log(`  ${RED}✗ Fail:${RESET}  ${fail}`);
  for (const r of results.filter((r) => r.status !== 'ok' && r.status !== 'warn')) {
    console.log(`    ${RED}${r.id}${RESET}: ${r.detail}`);
  }
}

// Save results as JSON
const reportPath = path.join(DEMO_DIR, 'dist', 'qa-report.json');
fs.writeFileSync(reportPath, JSON.stringify(results, null, 2));
console.log(`\n  Report: ${reportPath}`);
console.log(`  Screenshots: ${path.join(DEMO_DIR, 'dist', 'qa-screenshots/')}`);

process.exit(fail > 0 ? 1 : 0);
