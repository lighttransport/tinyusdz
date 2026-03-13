#!/usr/bin/env node
/**
 * Light HDRI Projection CLI
 * Standalone command-line tool for projecting lights to HDRI
 *
 * Usage: node light-hdri-projection-cli.js [options]
 */

import {
  LightHDRIProjection,
  SphereLight,
  AreaLight,
  DiskLight,
  EXRWriter,
  writeEXR,
  DEFAULT_WIDTH,
  DEFAULT_HEIGHT
} from '../light-hdri-projection.js';

import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// Parse command line arguments
const args = process.argv.slice(2);

const usage = `
Light HDRI Projection CLI
Usage: node light-hdri-projection-cli.js [options]

Options:
  --width <n>        Output width (default: 1024)
  --height <n>       Output height (default: 512)
  --output <file>    Output file path (default: output.raw)
  --format <fmt>     Output format: raw, pfm, hdr, exr (default: raw)
  --center <x,y,z>   View center point (default: 0,0,0)
  --maxdist <n>      Maximum distance (default: 1000)
  --samples <n>      Supersampling samples (1=none, 4=default, 16=high)

  EXR-specific options:
  --exr-compression <type>  Compression: none, zip, zips (default: zip)
  --exr-pixeltype <type>    Pixel type: half, float (default: half)

  --sphere <json>    Add sphere light (JSON: {position, radius, color, intensity})
  --area <json>      Add area light (JSON: {position, normal, tangent, width, height, color, intensity})
  --disk <json>      Add disk light (JSON: {position, normal, radius, color, intensity})

  --demo             Generate demo HDRI with sample lights
  --help             Show this help message

Light JSON Format:
  position: {x, y, z}     - Light position in 3D space
  color: {r, g, b}        - Light color (0-1 range) or hex number
  intensity: number       - Light intensity/brightness

  Sphere Light:
    radius: number        - Physical radius of the sphere

  Area Light:
    normal: {x, y, z}     - Direction the light faces
    tangent: {x, y, z}    - Width direction of the light
    width: number         - Light width
    height: number        - Light height
    twoSided: boolean     - Emit from both sides (default: false)

  Disk Light:
    normal: {x, y, z}     - Direction the light faces
    radius: number        - Disk radius
    twoSided: boolean     - Emit from both sides (default: false)

Examples:
  # Generate demo with default settings
  node light-hdri-projection-cli.js --demo --output lights.raw

  # Single sphere light
  node light-hdri-projection-cli.js \\
    --sphere '{"position":{"x":0,"y":2,"z":0},"radius":0.5,"intensity":10}' \\
    --output light.raw

  # Area light with custom resolution
  node light-hdri-projection-cli.js --width 2048 --height 1024 \\
    --area '{"position":{"x":0,"y":3,"z":0},"width":2,"height":1,"intensity":5}' \\
    --output area.raw

  # Multiple lights with supersampling
  node light-hdri-projection-cli.js --samples 16 \\
    --sphere '{"position":{"x":2,"y":3,"z":0},"radius":0.3,"intensity":10,"color":{"r":1,"g":0.9,"b":0.8}}' \\
    --area '{"position":{"x":-2,"y":4,"z":2},"width":2,"height":1,"intensity":5}' \\
    --output multi.hdr --format hdr

  # Generate EXR with ZIP compression (half-float)
  node light-hdri-projection-cli.js --demo --format exr --output lights.exr

  # Generate EXR with float32 pixels and no compression
  node light-hdri-projection-cli.js --demo --format exr --exr-pixeltype float --exr-compression none --output lights_f32.exr
`;

if (args.includes('--help') || args.includes('-h') || args.length === 0) {
  console.log(usage);
  process.exit(0);
}

// Default options
let width = DEFAULT_WIDTH;
let height = DEFAULT_HEIGHT;
let output = 'output.raw';
let format = 'raw';
let center = { x: 0, y: 0, z: 0 };
let maxDistance = 1000;
let samples = 1;
const lights = [];
let demo = false;

// EXR options
let exrCompression = 'zip';
let exrPixelType = 'half';

// Parse arguments
for (let i = 0; i < args.length; i++) {
  const arg = args[i];
  const next = args[i + 1];

  switch (arg) {
    case '--width':
      width = parseInt(next, 10);
      i++;
      break;
    case '--height':
      height = parseInt(next, 10);
      i++;
      break;
    case '--output':
      output = next;
      i++;
      break;
    case '--format':
      format = next.toLowerCase();
      i++;
      break;
    case '--center':
      const parts = next.split(',').map(parseFloat);
      center = { x: parts[0] || 0, y: parts[1] || 0, z: parts[2] || 0 };
      i++;
      break;
    case '--maxdist':
      maxDistance = parseFloat(next);
      i++;
      break;
    case '--samples':
      samples = parseInt(next, 10);
      i++;
      break;
    case '--sphere':
      try {
        const config = JSON.parse(next);
        config.type = 'sphere';
        lights.push(config);
      } catch (e) {
        console.error('Invalid sphere light JSON:', e.message);
        process.exit(1);
      }
      i++;
      break;
    case '--area':
      try {
        const config = JSON.parse(next);
        config.type = 'area';
        lights.push(config);
      } catch (e) {
        console.error('Invalid area light JSON:', e.message);
        process.exit(1);
      }
      i++;
      break;
    case '--disk':
      try {
        const config = JSON.parse(next);
        config.type = 'disk';
        lights.push(config);
      } catch (e) {
        console.error('Invalid disk light JSON:', e.message);
        process.exit(1);
      }
      i++;
      break;
    case '--demo':
      demo = true;
      break;
    case '--exr-compression':
      exrCompression = next.toLowerCase();
      if (!['none', 'zip', 'zips'].includes(exrCompression)) {
        console.error('Invalid EXR compression type. Use: none, zip, zips');
        process.exit(1);
      }
      i++;
      break;
    case '--exr-pixeltype':
      exrPixelType = next.toLowerCase();
      if (!['half', 'float'].includes(exrPixelType)) {
        console.error('Invalid EXR pixel type. Use: half, float');
        process.exit(1);
      }
      i++;
      break;
  }
}

// Add demo lights if requested
if (demo) {
  lights.push({
    type: 'sphere',
    position: { x: 2, y: 3, z: 0 },
    radius: 0.3,
    color: { r: 1, g: 0.9, b: 0.8 },
    intensity: 15
  });
  lights.push({
    type: 'area',
    position: { x: -2, y: 4, z: 2 },
    normal: { x: 0.3, y: -1, z: -0.2 },
    tangent: { x: 1, y: 0, z: 0 },
    width: 2,
    height: 1.5,
    color: { r: 0.9, g: 0.95, b: 1 },
    intensity: 8
  });
  lights.push({
    type: 'disk',
    position: { x: 0, y: 5, z: -3 },
    normal: { x: 0, y: -1, z: 0.5 },
    radius: 0.8,
    color: { r: 1, g: 0.7, b: 0.5 },
    intensity: 12
  });
}

if (lights.length === 0) {
  console.log('No lights specified. Use --demo for sample lights or add lights with --sphere, --area, --disk.');
  console.log('Run with --help for usage information.');
  process.exit(1);
}

console.log('='.repeat(50));
console.log('Light HDRI Projection');
console.log('='.repeat(50));
console.log(`Output resolution: ${width} x ${height}`);
console.log(`Lights: ${lights.length}`);
console.log(`Center: (${center.x}, ${center.y}, ${center.z})`);
console.log(`Max distance: ${maxDistance}`);
console.log(`Supersampling: ${samples > 1 ? samples + ' samples' : 'none'}`);
console.log('-'.repeat(50));

// List lights
lights.forEach((light, idx) => {
  const pos = light.position || { x: 0, y: 0, z: 0 };
  const col = light.color || { r: 1, g: 1, b: 1 };
  console.log(`Light ${idx + 1}: ${light.type}`);
  console.log(`  Position: (${pos.x}, ${pos.y}, ${pos.z})`);
  console.log(`  Color: (${col.r?.toFixed(2) || col}, ${col.g?.toFixed(2) || ''}, ${col.b?.toFixed(2) || ''})`);
  console.log(`  Intensity: ${light.intensity || 1}`);
  if (light.type === 'sphere') {
    console.log(`  Radius: ${light.radius || 0.1}`);
  } else if (light.type === 'area') {
    console.log(`  Size: ${light.width || 1} x ${light.height || 1}`);
  } else if (light.type === 'disk') {
    console.log(`  Radius: ${light.radius || 0.5}`);
  }
});
console.log('-'.repeat(50));

// Create projection engine
const engine = new LightHDRIProjection({
  width,
  height,
  center,
  maxDistance
});

// Add lights
for (const light of lights) {
  engine.addLight(light);
}

// Generate HDRI
console.log('Generating HDRI...');
const startTime = Date.now();
let hdri;
if (samples > 1) {
  hdri = engine.generateSupersampled(samples);
} else {
  hdri = engine.generate();
}
const elapsed = Date.now() - startTime;
console.log(`Generated in ${elapsed}ms`);

// Analyze result
let minVal = Infinity, maxVal = -Infinity, nonZero = 0;
for (let i = 0; i < hdri.data.length; i++) {
  const v = hdri.data[i];
  if (v > 0) {
    nonZero++;
    minVal = Math.min(minVal, v);
    maxVal = Math.max(maxVal, v);
  }
}
console.log(`Non-zero values: ${nonZero} / ${hdri.data.length}`);
if (nonZero > 0) {
  console.log(`Value range: ${minVal.toExponential(3)} - ${maxVal.toExponential(3)}`);
}

// Write output (async for EXR support)
const outputPath = path.resolve(output);

(async () => {
switch (format) {
  case 'pfm': {
    // Write PFM format (Portable Float Map)
    const header = `PF\n${width} ${height}\n-1.0\n`;
    const headerBuf = Buffer.from(header, 'ascii');
    const dataBuf = Buffer.alloc(width * height * 3 * 4);

    // PFM is bottom-to-top, RGB float
    for (let y = 0; y < height; y++) {
      const srcY = height - 1 - y;
      for (let x = 0; x < width; x++) {
        const srcIdx = (srcY * width + x) * 3;
        const dstIdx = (y * width + x) * 3 * 4;
        dataBuf.writeFloatLE(hdri.data[srcIdx], dstIdx);
        dataBuf.writeFloatLE(hdri.data[srcIdx + 1], dstIdx + 4);
        dataBuf.writeFloatLE(hdri.data[srcIdx + 2], dstIdx + 8);
      }
    }

    const outputBuf = Buffer.concat([headerBuf, dataBuf]);
    const pfmPath = outputPath.replace(/\.[^.]+$/, '.pfm');
    fs.writeFileSync(pfmPath, outputBuf);
    console.log(`Written: ${pfmPath} (${outputBuf.length} bytes)`);
    break;
  }

  case 'hdr': {
    // Write simple Radiance HDR format (RGBE)
    const rgbe = new Uint8Array(width * height * 4);

    for (let i = 0; i < width * height; i++) {
      const r = hdri.data[i * 3];
      const g = hdri.data[i * 3 + 1];
      const b = hdri.data[i * 3 + 2];

      const v = Math.max(r, g, b);
      if (v < 1e-32) {
        rgbe[i * 4] = 0;
        rgbe[i * 4 + 1] = 0;
        rgbe[i * 4 + 2] = 0;
        rgbe[i * 4 + 3] = 0;
      } else {
        const e = Math.ceil(Math.log2(v));
        const scale = Math.pow(2, -e + 8);
        rgbe[i * 4] = Math.min(255, Math.floor(r * scale));
        rgbe[i * 4 + 1] = Math.min(255, Math.floor(g * scale));
        rgbe[i * 4 + 2] = Math.min(255, Math.floor(b * scale));
        rgbe[i * 4 + 3] = e + 128;
      }
    }

    const header = `#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y ${height} +X ${width}\n`;
    const headerBuf = Buffer.from(header, 'ascii');
    const dataBuf = Buffer.from(rgbe.buffer);

    const outputBuf = Buffer.concat([headerBuf, dataBuf]);
    const hdrPath = outputPath.replace(/\.[^.]+$/, '.hdr');
    fs.writeFileSync(hdrPath, outputBuf);
    console.log(`Written: ${hdrPath} (${outputBuf.length} bytes)`);
    break;
  }

  case 'exr': {
    // Write OpenEXR format with ZIP compression
    console.log(`Writing EXR (compression: ${exrCompression}, pixel type: ${exrPixelType})...`);
    const exrStartTime = Date.now();

    const exrData = await writeEXR(hdri, {
      compression: exrCompression,
      pixelType: exrPixelType
    });

    const exrPath = outputPath.replace(/\.[^.]+$/, '.exr');
    fs.writeFileSync(exrPath, Buffer.from(exrData));

    const exrElapsed = Date.now() - exrStartTime;
    console.log(`Written: ${exrPath} (${exrData.length} bytes) in ${exrElapsed}ms`);

    // Calculate compression ratio if compression is enabled
    const uncompressedSize = width * height * 3 * (exrPixelType === 'half' ? 2 : 4);
    const ratio = (exrData.length / uncompressedSize * 100).toFixed(1);
    console.log(`Compression ratio: ${ratio}% of uncompressed size`);
    break;
  }

  case 'raw':
  default: {
    // Write raw float32 RGB data
    const dataBuf = Buffer.from(hdri.data.buffer);
    fs.writeFileSync(outputPath, dataBuf);
    console.log(`Written: ${outputPath} (${dataBuf.length} bytes)`);

    // Write metadata file
    const meta = {
      width,
      height,
      channels: 3,
      format: 'float32',
      byteOrder: 'little-endian',
      lights: lights.length,
      generated: new Date().toISOString()
    };
    const metaPath = outputPath + '.json';
    fs.writeFileSync(metaPath, JSON.stringify(meta, null, 2));
    console.log(`Written: ${metaPath}`);
    break;
  }
}

console.log('='.repeat(50));
console.log('Done!');
})().catch(err => {
  console.error('Error:', err);
  process.exit(1);
});
