import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath, pathToFileURL } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const packageRoot = path.resolve(__dirname, '..');
const distDir = path.resolve(packageRoot, 'dist');
const smokeDir = path.join(os.tmpdir(), `tinyusdz-npm-smoke-${process.pid}`);

const REQUIRED_STAGED_FILES = [
  'index.js',
  'LICENSE',
  'README.md',
  'package.json',
  'tinyusdz.js',
  'tinyusdz.wasm',
  'tinyusdz.wasm.zst',
  'tinyusdz_64.js',
  'tinyusdz_64.wasm',
  'tinyusdz_64.wasm.zst',
  'TinyUSDZLoader.js',
  'TinyUSDZLoaderUtils.js',
  'TinyUSDZWorkerLoader.js'
];

function readPublishManifest() {
  return JSON.parse(fs.readFileSync(path.join(distDir, 'package.json'), 'utf8'));
}

function patternToRegExp(pattern) {
  const escaped = pattern
    .replace(/[.+^${}()|[\]\\]/g, '\\$&')
    .replace(/\*/g, '[^/]*');
  return new RegExp(`^${escaped}$`);
}

function listPublishableFiles(manifest) {
  const entries = fs.readdirSync(distDir)
    .filter((entry) => fs.statSync(path.join(distDir, entry)).isFile())
    .sort();
  const patterns = (manifest.files || []).map(patternToRegExp);
  const included = new Set(['package.json']);

  for (const entry of entries) {
    if (entry === 'package.json') {
      continue;
    }
    if (patterns.some((regex) => regex.test(entry))) {
      included.add(entry);
    }
  }

  return included;
}

function validateStagedFiles() {
  for (const file of REQUIRED_STAGED_FILES) {
    const filePath = path.join(distDir, file);
    if (!fs.existsSync(filePath)) {
      throw new Error(`Missing staged file: ${filePath}`);
    }
  }
}

function validatePublishableContents(manifest) {
  const publishableFiles = listPublishableFiles(manifest);
  for (const file of REQUIRED_STAGED_FILES) {
    if (!publishableFiles.has(file)) {
      throw new Error(`File would be omitted from published package: ${file}`);
    }
  }
}

function prepareSmokeFixture() {
  fs.rmSync(smokeDir, { recursive: true, force: true });
  fs.mkdirSync(path.join(smokeDir, 'node_modules'), { recursive: true });
  fs.symlinkSync(distDir, path.join(smokeDir, 'node_modules', 'tinyusdz'), 'dir');

  const script = `
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import * as pkg from 'tinyusdz';
import { TinyUSDZLoader as DeepLoader } from 'tinyusdz/TinyUSDZLoader.js';
import TinyUSDZWorkerLoaderDefault, { TinyUSDZWorkerLoader } from 'tinyusdz/TinyUSDZWorkerLoader.js';

const require = createRequire(import.meta.url);

assert.equal(typeof pkg.TinyUSDZLoader, 'function');
assert.equal(typeof pkg.TinyUSDZLoaderUtils, 'function');
assert.equal(typeof pkg.TextureLoadingManager, 'function');
assert.equal(typeof pkg.TinyUSDZComposer, 'function');
assert.equal(typeof pkg.MaterialX, 'object');
assert.equal(typeof pkg.OpenPBRWebGL, 'object');
assert.equal(typeof pkg.OpenPBRTSL, 'object');
assert.equal(pkg.TinyUSDZLoader, DeepLoader);
assert.equal(TinyUSDZWorkerLoaderDefault, TinyUSDZWorkerLoader);
assert.equal(require.resolve('tinyusdz/TinyUSDZLoader.js').endsWith('TinyUSDZLoader.js'), true);
assert.equal(require.resolve('tinyusdz/tinyusdz.wasm').endsWith('tinyusdz.wasm'), true);
assert.equal(require.resolve('tinyusdz/tinyusdz.wasm.zst').endsWith('tinyusdz.wasm.zst'), true);
assert.equal(require.resolve('tinyusdz/tinyusdz_64.wasm').endsWith('tinyusdz_64.wasm'), true);
assert.equal(require.resolve('tinyusdz/tinyusdz_64.wasm.zst').endsWith('tinyusdz_64.wasm.zst'), true);
`;

  fs.writeFileSync(path.join(smokeDir, 'smoke.mjs'), script.trimStart(), 'utf8');
}

async function runSmokeTest() {
  prepareSmokeFixture();
  await import(pathToFileURL(path.join(smokeDir, 'smoke.mjs')).href);
}

async function main() {
  const packOnly = process.argv.includes('--pack-only');

  if (!fs.existsSync(distDir)) {
    throw new Error(`Package dist directory not found: ${distDir}. Run npm run build first.`);
  }

  validateStagedFiles();

  const manifest = readPublishManifest();
  validatePublishableContents(manifest);

  if (!packOnly) {
    await runSmokeTest();
  }

  console.log(`Validated npm package staging in ${distDir}`);
}

try {
  await main();
} catch (error) {
  console.error(`[validate-package] ${error.message}`);
  process.exit(1);
}
