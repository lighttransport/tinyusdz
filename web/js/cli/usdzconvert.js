#!/usr/bin/env node
// usdzconvert CLI — TinyUSDZ WASM
//
// Convert a USD file (plus its textures) or a whole folder into a USDZ, with
// optional texture resize / re-encode. Also a standalone channel-repack mode.
//
// Usage:
//   vite-node cli/usdzconvert.js <input-dir|input.usd> [options]
//   vite-node cli/usdzconvert.js --repack out.png -packR a.png:0 -packG b.png:0 [options]

import fs from 'node:fs';
import path from 'node:path';
import { convertFolderToUSDZ, loadWasm, parseByteSize } from '../src/usdzconvert.js';

// Load the Emscripten glue directly (no three.js / vite-node dependency) so the
// CLI runs with plain `node`.
const wasmGlue = new URL('../src/tinyusdz/tinyusdz.js', import.meta.url).href;

function printHelp() {
  console.log(`
usdzconvert CLI — TinyUSDZ WASM

Usage:
  vite-node cli/usdzconvert.js <input-dir|input.usd> [options]
  vite-node cli/usdzconvert.js --repack <out.png> -packR <src> [-packG <src> ...] [options]

Convert options:
  -o, --output <file>      Output .usdz path (default: <root>.usdz)
  --root <relpath>         Root USD layer within the input dir (default: auto)
  --resize <N>             Cap each texture's longest edge to N pixels
  --texture-format <fmt>   Texture output: keep, png, jpeg (default: keep)
  --root-layer-format <fmt> USDZ root layer: usdc, usda (default: usdc)
  --arkit-compatible       Force ARKit-friendly flattened USDC package metadata
  --no-flatten             Accepted for parity; JS/WASM export still flattens
  --jpeg-quality <1-100>   JPEG quality when re-encoding (default 90)
  --png-encoder <fpnge|fpng|auto>  PNG encoder hint (WASM always uses fpng)
  --no-reencode            Copy unmodified textures through unchanged
  -v, --verbose            Verbose logging
  -h, --help               Show this help

Fit textures to a total size budget:
  --target-size <size>     Shrink all textures so their total fits <size> (e.g. 100MB)
  --fit-strategy <size|quality>  Lever: reduce dimensions (size) or transcode to
                           JPEG + lower quality (quality). Default: size
  --fit-min-size <N>       Smallest longest-edge for the size search (default 64)
  --fit-min-quality <N>    Lowest JPEG quality for the quality search (default 30)

Repack mode (merge channels into one image, e.g. R=gloss, G=roughness):
  --repack <out.png>       Enable repack mode; write packed image here
  -packR/-packG/-packB/-packA <src>
                           Channel source: 'file.png:CH' (CH 0..3) or 'const:VALUE'
  --pack-channels <1-4>    Output channel count (default: from -pack* flags)

Examples:
  vite-node cli/usdzconvert.js ./model_folder -o model.usdz --resize 1024 -v
  vite-node cli/usdzconvert.js ./model/scene.usda -o out.usdz
  vite-node cli/usdzconvert.js --repack orm.png -packR ao.png:0 -packG rough.png:0 -packB metal.png:0 --pack-channels 3
`);
}

function parseArgs() {
  const args = process.argv.slice(2);
  const o = {
    input: null, output: null, root: null, resize: 0, jpegQuality: 90,
    pngEncoder: 'auto', reencode: true, verbose: false,
    textureFormat: 'keep', rootLayerFormat: 'usdc', arkitCompatible: false,
    flatten: true,
    targetSize: 0, fitStrategy: 'size', fitMinSize: 64, fitMinQuality: 30,
    repack: null, packChannels: 0, pack: { R: null, G: null, B: null, A: null },
  };
  for (let i = 0; i < args.length; i++) {
    const a = args[i];
    if (a === '-h' || a === '--help') { printHelp(); process.exit(0); }
    else if (a === '-o' || a === '--output') o.output = args[++i];
    else if (a === '--root') o.root = args[++i];
    else if (a === '--resize') o.resize = parseInt(args[++i], 10) || 0;
    else if (a === '--texture-format') o.textureFormat = args[++i];
    else if (a === '--root-layer-format') o.rootLayerFormat = args[++i];
    else if (a === '--arkit-compatible') o.arkitCompatible = true;
    else if (a === '--no-flatten') o.flatten = false;
    else if (a === '--jpeg-quality') o.jpegQuality = parseInt(args[++i], 10) || 90;
    else if (a === '--png-encoder') o.pngEncoder = args[++i];
    else if (a === '--no-reencode') o.reencode = false;
    else if (a === '--target-size') o.targetSize = parseByteSize(args[++i]);
    else if (a === '--fit-strategy') o.fitStrategy = args[++i];
    else if (a === '--fit-min-size') o.fitMinSize = parseInt(args[++i], 10) || 64;
    else if (a === '--fit-min-quality') o.fitMinQuality = parseInt(args[++i], 10) || 30;
    else if (a === '-v' || a === '--verbose') o.verbose = true;
    else if (a === '--repack') o.repack = args[++i];
    else if (a === '--pack-channels') o.packChannels = parseInt(args[++i], 10) || 0;
    else if (a === '-packR') o.pack.R = args[++i];
    else if (a === '-packG') o.pack.G = args[++i];
    else if (a === '-packB') o.pack.B = args[++i];
    else if (a === '-packA') o.pack.A = args[++i];
    else if (!o.input) o.input = a;
    else { console.error('Unknown argument: ' + a); process.exit(1); }
  }
  return o;
}

// Recursively read a directory into a Map<relPath, Uint8Array>.
function readDirToMap(dir) {
  const map = new Map();
  const walk = (cur, prefix) => {
    for (const entry of fs.readdirSync(cur, { withFileTypes: true })) {
      const full = path.join(cur, entry.name);
      const rel = prefix ? `${prefix}/${entry.name}` : entry.name;
      if (entry.isDirectory()) walk(full, rel);
      else if (entry.isFile()) map.set(rel, new Uint8Array(fs.readFileSync(full)));
    }
  };
  walk(dir, '');
  return map;
}

function parseChannelSrc(s) {
  if (s.startsWith('const:')) return { const: parseInt(s.slice(6), 10) & 0xff };
  const m = s.match(/^(.*):([0-3])$/);
  if (m) return { file: m[1], channel: parseInt(m[2], 10) };
  return { file: s, channel: 0 };
}

async function runRepack(native, o) {
  const slots = ['R', 'G', 'B', 'A'];
  const args = { channels: o.packChannels, format: 'png', pngEncoder: o.pngEncoder };
  let inferred = 0;
  for (let i = 0; i < 4; i++) {
    const spec = o.pack[slots[i]];
    if (!spec) continue;
    inferred = Math.max(inferred, i + 1);
    const parsed = parseChannelSrc(spec);
    const key = slots[i].toLowerCase();
    if (parsed.file !== undefined) {
      if (!fs.existsSync(parsed.file)) { console.error('File not found: ' + parsed.file); process.exit(1); }
      args[key] = { data: new Uint8Array(fs.readFileSync(parsed.file)), channel: parsed.channel };
    } else {
      args[key] = { const: parsed.const };
    }
  }
  if (!args.channels || args.channels < 1) args.channels = inferred || 4;

  const res = native.repackChannels(args);
  if (!res || !res.success) { console.error('Repack failed:', res && res.error); process.exit(1); }
  fs.writeFileSync(o.repack, new Uint8Array(res.data));
  console.log(`Wrote ${o.repack} (${res.width}x${res.height}x${res.channels}, ${res.data.length} bytes)`);
}

async function main() {
  const o = parseArgs();

  const native = await loadWasm(() => import(wasmGlue));
  if (o.verbose) console.log('WASM module loaded.');
  if (!['keep', 'png', 'jpeg', 'jpg'].includes(String(o.textureFormat).toLowerCase())) {
    console.error('Invalid --texture-format. Expected keep, png, or jpeg.');
    process.exit(1);
  }
  if (!['usdc', 'usda'].includes(String(o.rootLayerFormat).toLowerCase())) {
    console.error('Invalid --root-layer-format. Expected usdc or usda.');
    process.exit(1);
  }

  if (o.repack) {
    await runRepack(native, o);
    return;
  }

  if (!o.input) { console.error('Error: input directory or USD file required.'); printHelp(); process.exit(1); }
  if (!fs.existsSync(o.input)) { console.error('Not found: ' + o.input); process.exit(1); }

  // Build the asset map and determine the root USD's relative path.
  let assetMap, rootRel;
  const st = fs.statSync(o.input);
  if (st.isDirectory()) {
    assetMap = readDirToMap(o.input);
    rootRel = o.root || null;
  } else if (/\.usdz$/i.test(o.input)) {
    // A .usdz is self-contained; read only the archive and let the converter
    // unpack it to repack its internal textures (avoids slurping siblings).
    const base = path.basename(o.input);
    assetMap = new Map([[base, new Uint8Array(fs.readFileSync(o.input))]]);
    rootRel = o.root || base;
  } else {
    // Single file: read its whole directory so sibling textures resolve.
    const dir = path.dirname(o.input);
    assetMap = readDirToMap(dir);
    rootRel = o.root || path.basename(o.input);
  }

  const log = o.verbose ? (m) => console.log(m) : () => {};
  const { usdz, stats } = await convertFolderToUSDZ(native, assetMap, {
    rootPath: rootRel,
    maxTextureSize: o.resize,
    targetTextureBytes: o.targetSize,
    fitStrategy: o.fitStrategy,
    fitMinTextureSize: o.fitMinSize,
    fitMinQuality: o.fitMinQuality,
    reencode: o.reencode,
    textureFormat: o.textureFormat,
    rootLayerFormat: o.rootLayerFormat,
    arkitCompatible: o.arkitCompatible,
    flatten: o.flatten,
    pngEncoder: o.pngEncoder,
    jpegQuality: o.jpegQuality,
    log,
  });

  const outPath = o.output ||
    `${stats.rootPath.split('/').pop().replace(/\.(usd|usda|usdc|usdz)$/i, '')}.usdz`;
  fs.writeFileSync(outPath, usdz);
  console.log(`Wrote ${outPath} (${usdz.length} bytes) — root: ${stats.rootPath}, ` +
              `textures: ${stats.textures}, resized: ${stats.resized}, reencoded: ${stats.reencoded}, ` +
              `audio: ${stats.audio || 0}, other assets: ${stats.otherAssets || 0}`);
}

main().catch(err => { console.error('Error:', err); process.exit(1); });
