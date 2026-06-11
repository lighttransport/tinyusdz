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
// CLI runs with plain `node`. TINYUSDZ_WASM64=1 selects the 64-bit (8 GB) glue
// — used as a fallback for scenes that overflow the wasm32 2 GB ceiling. Falls
// back to the 32-bit glue if the 64-bit build is not present.
function selectWasmGlue() {
  if (process.env.TINYUSDZ_WASM64 === '1') {
    const url64 = new URL('../src/tinyusdz/tinyusdz_64.js', import.meta.url);
    if (fs.existsSync(url64)) return url64.href;
    console.error('[usdzconvert] TINYUSDZ_WASM64=1 but tinyusdz_64.js not found; using wasm32.');
  }
  return new URL('../src/tinyusdz/tinyusdz.js', import.meta.url).href;
}
const wasmGlue = selectWasmGlue();

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
  --resize-colorspace <s>  How to resample when resizing:
                           'auto'   = per-texture from UsdUVTexture sourceColorSpace
                                      (sRGB textures in linear light, data maps
                                      gamma-space) — correct without guessing;
                           'srgb'   = force linear-light for ALL resized textures;
                           'linear' = force gamma-space for all (default).
  --texture-format <fmt>   Texture output: keep, png, jpeg (default: keep)
  --root-layer-format <fmt> USDZ root layer: usdc, usda (default: usdc)
  --arkit-compatible       Force ARKit-friendly flattened USDC package metadata
  --no-flatten             Accepted for parity; JS/WASM export still flattens
  --jpeg-quality <1-100>   JPEG quality when re-encoding (default 90)
  --max-usdc-mb <N>        Raise the USDC root-layer write size cap to N MB
                           (0 = keep the conservative ~100 MB WASM default).
                           Needed for very large scenes.
  --max-mem-mb <N>         Raise the USDC writer memory cap to N MB (0 = default)
  --pipeline <legacy|next> Flatten pipeline (default: legacy). 'next' uses the
                           experimental low-memory lazy-ValueRep path for a single
                           .usdz with a top-level USDC root; falls back to legacy
                           otherwise. Also via TINYUSDZ_PIPELINE env.
  --stream-textures        Re-encode/resize textures one at a time and repack in
                           JS, so decoded images never accumulate in the WASM
                           heap. This is the DEFAULT for a single .usdz with
                           --texture-format keep + re-encode/resize (it keeps
                           large texture-heavy scenes under the wasm32
                           2 GB ceiling). The flag forces it; otherwise it is
                           auto-enabled and falls back for nested roots / non-keep
                           formats.
  --no-stream-textures     Force the in-heap batch texture path (higher memory).
  --stream-write           (--pipeline next, DEFAULT) Stream the flattened root
                           crate straight into the .usdz instead of buffering it,
                           so the output crate never materializes in the WASM heap
                           or in JS. Byte-identical output; roughly halves peak
                           RSS on large scenes. Engages when writing to a file
                           (-o); falls back to buffering otherwise.
  --no-stream-write        Force the buffered root path (TINYUSDZ_STREAM_WRITE=0).
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
    pngEncoder: 'auto', reencode: true, verbose: false, resizeColorspace: '',
    textureFormat: 'keep', rootLayerFormat: 'usdc', arkitCompatible: false,
    flatten: true,
    targetSize: 0, fitStrategy: 'size', fitMinSize: 64, fitMinQuality: 30,
    maxUsdcMb: 0, maxMemMb: 0,
    pipeline: process.env.TINYUSDZ_PIPELINE || 'legacy',
    // undefined => auto (stream textures for a single .usdz keep-format re-encode,
    // the low-memory default); true => force; false => force the in-heap path.
    streamTextures: undefined,
    // Stream the flattened root crate straight into the .usdz (next pipeline only)
    // instead of buffering it — keeps the output crate out of the WASM heap and JS.
    // Default ON for --pipeline next when writing to a file; TINYUSDZ_STREAM_WRITE=0
    // (or --no-stream-write) disables it.
    streamWrite: process.env.TINYUSDZ_STREAM_WRITE !== '0',
    repack: null, packChannels: 0, pack: { R: null, G: null, B: null, A: null },
  };
  for (let i = 0; i < args.length; i++) {
    const a = args[i];
    if (a === '-h' || a === '--help') { printHelp(); process.exit(0); }
    else if (a === '-o' || a === '--output') o.output = args[++i];
    else if (a === '--root') o.root = args[++i];
    else if (a === '--resize') o.resize = parseInt(args[++i], 10) || 0;
    else if (a === '--resize-colorspace') o.resizeColorspace = args[++i];
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
    else if (a === '--max-usdc-mb') o.maxUsdcMb = parseInt(args[++i], 10) || 0;
    else if (a === '--max-mem-mb') o.maxMemMb = parseInt(args[++i], 10) || 0;
    else if (a === '--pipeline') o.pipeline = args[++i];
    else if (a === '--stream-textures') o.streamTextures = true;
    else if (a === '--no-stream-textures') o.streamTextures = false;
    else if (a === '--stream-write') o.streamWrite = true;
    else if (a === '--no-stream-write') o.streamWrite = false;
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
// fs.writeFileSync caps a single write at 2^31-1 bytes; a passthrough .usdz of
// a multi-GB scene exceeds that. Write in <2 GiB chunks instead.
function writeFileChunked(path, bytes) {
  const CHUNK = 1 << 30;  // 1 GiB
  const fd = fs.openSync(path, 'w');
  try {
    for (let pos = 0; pos < bytes.length; pos += CHUNK) {
      const n = Math.min(CHUNK, bytes.length - pos);
      fs.writeSync(fd, bytes, pos, n, pos);
    }
  } finally {
    fs.closeSync(fd);
  }
}

function readDirToMap(dir) {
  const map = new Map();
  const walk = (cur, prefix) => {
    for (const entry of fs.readdirSync(cur, { withFileTypes: true })) {
      const full = path.join(cur, entry.name);
      const rel = prefix ? `${prefix}/${entry.name}` : entry.name;
      if (entry.isDirectory()) walk(full, rel);
      else if (entry.isFile()) map.set(rel, fs.readFileSync(full));
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
      args[key] = { data: fs.readFileSync(parsed.file), channel: parsed.channel };
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
    assetMap = new Map([[base, fs.readFileSync(o.input)]]);
    rootRel = o.root || base;
  } else {
    // Single file: read its whole directory so sibling textures resolve.
    const dir = path.dirname(o.input);
    assetMap = readDirToMap(dir);
    rootRel = o.root || path.basename(o.input);
  }

  const log = o.verbose ? (m) => console.log(m) : () => {};

  // When an explicit -o path is given, offer a file-backed zip sink so the
  // texture-streaming path can write the archive straight to disk (one entry at
  // a time) instead of building the whole USDZ in memory — the lowest-peak
  // legacy path for texture-heavy scenes. Opened lazily on first write, so if a
  // non-streaming path runs the sink is simply never used.
  // Patch-capable (seekable) sink: write() appends at the running offset; patch()
  // rewrites already-written bytes at an absolute position (used to backfill a
  // streamed root entry's local-header CRC/size). All writes use an explicit
  // position so append and patch never depend on the fd's implicit cursor.
  let streamFd = null, streamBytes = 0;
  const ensureFd = () => { if (streamFd === null) streamFd = fs.openSync(o.output, 'w'); };
  const zipSink = o.output
    ? {
        write: (chunk) => {
          ensureFd();
          fs.writeSync(streamFd, chunk, 0, chunk.length, streamBytes);
          streamBytes += chunk.length;
        },
        patch: (pos, chunk) => {
          ensureFd();
          fs.writeSync(streamFd, chunk, 0, chunk.length, pos);
        },
      }
    : undefined;

  const { usdz, streamedToSink, stats } = await convertFolderToUSDZ(native, assetMap, {
    rootPath: rootRel,
    maxTextureSize: o.resize,
    resizeColorspace: o.resizeColorspace,
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
    maxUsdcMb: o.maxUsdcMb,
    maxMemMb: o.maxMemMb,
    pipeline: o.pipeline,
    streamTextures: o.streamTextures,
    streamWrite: o.streamWrite,
    zipSink,
    log,
  });

  const statLine = `root: ${stats.rootPath}, textures: ${stats.textures}, ` +
    `resized: ${stats.resized}, reencoded: ${stats.reencoded}, ` +
    `audio: ${stats.audio || 0}, other assets: ${stats.otherAssets || 0}`;

  if (streamedToSink && streamFd !== null) {
    fs.closeSync(streamFd);
    console.log(`Wrote ${o.output} (${streamBytes} bytes, streamed) — ${statLine}`);
  } else {
    if (streamFd !== null) fs.closeSync(streamFd);  // sink opened but path bailed
    const outPath = o.output ||
      `${stats.rootPath.split('/').pop().replace(/\.(usd|usda|usdc|usdz)$/i, '')}.usdz`;
    writeFileChunked(outPath, usdz);
    console.log(`Wrote ${outPath} (${usdz.length} bytes) — ${statLine}`);
  }
}

main().catch(err => { console.error('Error:', err); process.exit(1); });
