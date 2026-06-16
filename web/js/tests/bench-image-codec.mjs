#!/usr/bin/env node
// Micro-benchmark for the WASM image decode/resize/encode path
// (native.convertImage — the same call the usdzconvert texture pipeline makes
// per texture). Reports per-image and aggregate wall time + throughput so
// build-flag changes (-Oz vs -O3 on the codec TUs) can be quantified.
//
//   node tests/bench-image-codec.mjs --images <dir> [--limit 8] [--iters 3]
//     [--format png|jpeg] [--resize N] [--wasm32] [--pngjs]
//
// --pngjs additionally times a pngjs (node native zlib) decode+encode of the
// same files as a non-wasm reference point.

import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';
import { loadWasm } from '../src/usdzconvert.js';

const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url));

function parseArgs(argv = process.argv.slice(2)) {
  const o = {
    images: [], limit: 8, iters: 3, format: 'png', resize: 0,
    wasm64: true, pngjs: false, quality: 90,
  };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === '-h' || a === '--help') {
      console.log('Usage: node tests/bench-image-codec.mjs --images <dir|file...> [--limit N] [--iters N] [--format png|jpeg] [--resize N] [--wasm32] [--pngjs]');
      process.exit(0);
    }
    else if (a === '--images') o.images.push(argv[++i]);
    else if (a === '--limit') o.limit = Number(argv[++i]);
    else if (a === '--iters') o.iters = Number(argv[++i]);
    else if (a === '--format') o.format = argv[++i];
    else if (a === '--resize') o.resize = Number(argv[++i]);
    else if (a === '--quality') o.quality = Number(argv[++i]);
    else if (a === '--wasm32') o.wasm64 = false;
    else if (a === '--pngjs') o.pngjs = true;
    else o.images.push(a);
  }
  if (!o.images.length) throw new Error('--images <dir|file...> required');
  return o;
}

function collectImages(inputs, limit) {
  const files = [];
  for (const input of inputs) {
    const st = fs.statSync(input);
    if (st.isDirectory()) {
      const walk = (dir) => {
        for (const ent of fs.readdirSync(dir, { withFileTypes: true })) {
          const p = path.join(dir, ent.name);
          if (ent.isDirectory()) walk(p);
          else if (/\.(png|jpe?g)$/i.test(ent.name)) files.push(p);
        }
      };
      walk(input);
    } else {
      files.push(input);
    }
  }
  // Deterministic spread: biggest files first (the interesting ones), capped.
  files.sort((a, b) => fs.statSync(b).size - fs.statSync(a).size);
  return files.slice(0, limit);
}

function pngDims(bytes) {
  if (bytes.length >= 24 && bytes[0] === 0x89 && bytes[1] === 0x50) {
    const dv = new DataView(bytes.buffer, bytes.byteOffset);
    return { w: dv.getUint32(16), h: dv.getUint32(20) };
  }
  return { w: 0, h: 0 };
}

async function main() {
  const o = parseArgs();
  const files = collectImages(o.images, o.limit);
  if (!files.length) throw new Error('no png/jpg files found');

  const glue = o.wasm64 ? '../src/tinyusdz/tinyusdz_64.js' : '../src/tinyusdz/tinyusdz.js';
  const native = await loadWasm(() => import(new URL(glue, import.meta.url).href));
  console.log(`wasm: ${o.wasm64 ? 'wasm64' : 'wasm32'}  format=${o.format} resize=${o.resize} iters=${o.iters}`);
  console.log(`images: ${files.length} (largest-first)`);

  let totalMs = 0, totalInMB = 0, totalMP = 0;
  for (const file of files) {
    const bytes = new Uint8Array(fs.readFileSync(file));
    const { w, h } = pngDims(bytes);
    const opts = {
      maxSize: o.resize, format: o.format, pngEncoder: 'auto',
      jpegQuality: o.quality, resizeColorspace: 'linear',
    };
    // warmup
    let res = native.convertImage(bytes, opts);
    if (!res || !res.success) {
      console.log(`  SKIP ${path.basename(file)}: ${res && res.error}`);
      continue;
    }
    const t0 = performance.now();
    for (let i = 0; i < o.iters; i++) res = native.convertImage(bytes, opts);
    const ms = (performance.now() - t0) / o.iters;
    const outLen = res.data.byteLength ?? res.data.length;
    totalMs += ms;
    totalInMB += bytes.length / 1e6;
    totalMP += (w * h) / 1e6;
    console.log(`  ${path.basename(file).padEnd(44)} ${String(w + 'x' + h).padEnd(10)} ` +
      `${(bytes.length / 1e6).toFixed(2)}MB -> ${(outLen / 1e6).toFixed(2)}MB  ${ms.toFixed(1)} ms  ` +
      `${((w * h) / 1e6 / (ms / 1000)).toFixed(1)} MP/s`);
  }
  console.log(`TOTAL wasm convertImage: ${totalMs.toFixed(0)} ms  ` +
    `${(totalInMB / (totalMs / 1000)).toFixed(1)} MB/s in  ` +
    `${(totalMP / (totalMs / 1000)).toFixed(1)} MP/s`);

  if (o.pngjs) {
    const { PNG } = await import('pngjs');
    let jsMs = 0, jsMP = 0;
    for (const file of files) {
      const bytes = fs.readFileSync(file);
      if (!(bytes[0] === 0x89 && bytes[1] === 0x50)) continue;
      const t0 = performance.now();
      for (let i = 0; i < o.iters; i++) {
        const png = PNG.sync.read(bytes);
        PNG.sync.write(png, { deflateLevel: 6, filterType: 4 });
      }
      const ms = (performance.now() - t0) / o.iters;
      const { w, h } = pngDims(new Uint8Array(bytes));
      jsMs += ms;
      jsMP += (w * h) / 1e6;
      console.log(`  [pngjs] ${path.basename(file).padEnd(36)} ${ms.toFixed(1)} ms  ${((w * h) / 1e6 / (ms / 1000)).toFixed(1)} MP/s`);
    }
    console.log(`TOTAL pngjs decode+encode: ${jsMs.toFixed(0)} ms  ${(jsMP / (jsMs / 1000)).toFixed(1)} MP/s`);
  }
}

main().catch((err) => { console.error(err.stack || String(err)); process.exit(1); });
