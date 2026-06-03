#!/usr/bin/env node
// usddiff CLI — TinyUSDZ WASM
//
// Diff two USD files at the Layer / PrimSpec / Attribute level. Files are loaded
// as Layers (pre-composition), so the full prim/attribute tree is compared.
// Mirrors the native `usddiff` / `tusddiff` tool (examples/usddiff/usddiff-main.cc).
//
// Usage:
//   node cli/usddiff.js [OPTIONS] <file1> <file2>
//
// Options:
//   --json     Output diff in JSON format
//   --quiet    Suppress diff output, exit code only
//   --help,-h  Show this help message
//
// Exit codes:
//   0  No differences found
//   1  Differences found
//   2  Error (file not found, parse failure, etc.)
//
// Supported formats: .usd, .usda, .usdc, .usdz

import fs from 'node:fs';
import path from 'node:path';
import { loadWasm } from '../src/usdzconvert.js';

// Load the Emscripten glue directly (no three.js / vite-node dependency) so the
// CLI runs with plain `node`.
const wasmGlue = new URL('../src/tinyusdz/tinyusdz.js', import.meta.url).href;

function printUsage() {
  console.log(`USD Layer Diff Tool — TinyUSDZ WASM

USAGE:
  node cli/usddiff.js [OPTIONS] <file1> <file2>

OPTIONS:
  --json      Output diff in JSON format
  --quiet     Suppress diff output, exit code only
  --help      Show this help message
  -h          Show this help message

EXIT CODES:
  0  No differences found
  1  Differences found
  2  Error (file not found, parse failure, etc.)

EXAMPLES:
  node cli/usddiff.js old.usd new.usd
  node cli/usddiff.js --json scene1.usda scene2.usda
  node cli/usddiff.js --quiet model.usda model.usdc

SUPPORTED FORMATS:
  .usd, .usda, .usdc, .usdz`);
}

function parseArgs() {
  const args = process.argv.slice(2);
  const o = { json: false, quiet: false, file1: null, file2: null };
  for (const a of args) {
    if (a === '--help' || a === '-h') { printUsage(); process.exit(0); }
    else if (a === '--json') o.json = true;
    else if (a === '--quiet') o.quiet = true;
    else if (o.file1 === null) o.file1 = a;
    else if (o.file2 === null) o.file2 = a;
    else { console.error('Error: Too many arguments'); printUsage(); process.exit(2); }
  }
  return o;
}

async function main() {
  const o = parseArgs();

  if (!o.file1 || !o.file2) {
    console.error('Error: Please specify two USD files to compare');
    printUsage();
    process.exit(2);
  }
  for (const f of [o.file1, o.file2]) {
    if (!fs.existsSync(f)) { console.error('Error: file not found: ' + f); process.exit(2); }
  }

  const native = await loadWasm(() => import(wasmGlue));

  const res = native.usddiff({
    left: { data: new Uint8Array(fs.readFileSync(o.file1)), name: o.file1 },
    right: { data: new Uint8Array(fs.readFileSync(o.file2)), name: o.file2 },
    format: o.json ? 'json' : 'text',
  });

  if (!res || !res.success) {
    console.error('Error: ' + (res && res.error ? res.error : 'usddiff failed'));
    process.exit(2);
  }
  if (res.warn) console.error(res.warn);

  if (!o.quiet) {
    if (o.json) process.stdout.write(res.json);
    else process.stdout.write(res.text);
  }

  process.exit(res.hasDiffs ? 1 : 0);
}

main().catch(err => { console.error('Error:', err && err.message ? err.message : err); process.exit(2); });
