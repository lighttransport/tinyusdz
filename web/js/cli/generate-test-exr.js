#!/usr/bin/env node
// SPDX-License-Identifier: Apache 2.0
// Copyright 2024-Present Light Transport Entertainment, Inc.
//
// Generate synthetic EXR files for testing
// Creates EXR files with various compression types supported by TinyEXR

import fs from 'node:fs';
import path from 'node:path';

// EXR compression types
const COMPRESSION = {
  NO_COMPRESSION: 0,
  RLE_COMPRESSION: 1,
  ZIPS_COMPRESSION: 2,  // ZIP single scanline
  ZIP_COMPRESSION: 3,   // ZIP 16 scanlines
  PIZ_COMPRESSION: 4,
};

// Helper to write various data types
class BinaryWriter {
  constructor() {
    this.chunks = [];
    this.size = 0;
  }

  writeUint8(value) {
    const buf = Buffer.alloc(1);
    buf.writeUInt8(value, 0);
    this.chunks.push(buf);
    this.size += 1;
  }

  writeUint32(value) {
    const buf = Buffer.alloc(4);
    buf.writeUInt32LE(value, 0);
    this.chunks.push(buf);
    this.size += 4;
  }

  writeInt32(value) {
    const buf = Buffer.alloc(4);
    buf.writeInt32LE(value, 0);
    this.chunks.push(buf);
    this.size += 4;
  }

  writeUint64(value) {
    const buf = Buffer.alloc(8);
    buf.writeBigUInt64LE(BigInt(value), 0);
    this.chunks.push(buf);
    this.size += 8;
  }

  writeFloat(value) {
    const buf = Buffer.alloc(4);
    buf.writeFloatLE(value, 0);
    this.chunks.push(buf);
    this.size += 4;
  }

  writeHalf(value) {
    // Convert float32 to float16 (IEEE 754 half-precision)
    const buf = Buffer.alloc(2);
    buf.writeUInt16LE(float32ToFloat16(value), 0);
    this.chunks.push(buf);
    this.size += 2;
  }

  writeString(str) {
    const buf = Buffer.from(str + '\0', 'utf8');
    this.chunks.push(buf);
    this.size += buf.length;
  }

  writeBytes(bytes) {
    const buf = Buffer.from(bytes);
    this.chunks.push(buf);
    this.size += buf.length;
  }

  writeBuffer(buffer) {
    this.chunks.push(buffer);
    this.size += buffer.length;
  }

  toBuffer() {
    return Buffer.concat(this.chunks);
  }
}

// Convert float32 to float16
function float32ToFloat16(value) {
  const floatView = new Float32Array(1);
  const int32View = new Int32Array(floatView.buffer);

  floatView[0] = value;
  const x = int32View[0];

  const sign = (x >> 16) & 0x8000;
  let exponent = ((x >> 23) & 0xff) - 127 + 15;
  let mantissa = (x >> 13) & 0x3ff;

  if (exponent <= 0) {
    if (exponent < -10) {
      return sign;
    }
    mantissa = (mantissa | 0x400) >> (1 - exponent);
    return sign | mantissa;
  } else if (exponent === 0xff - 127 + 15) {
    if (mantissa) {
      return sign | 0x7fff; // NaN
    }
    return sign | 0x7c00; // Inf
  } else if (exponent > 30) {
    return sign | 0x7c00; // Overflow to Inf
  }

  return sign | (exponent << 10) | mantissa;
}

// Generate EXR file with no compression (simplest format)
function generateEXR(width, height, options = {}) {
  const {
    compression = COMPRESSION.NO_COMPRESSION,
    pixelType = 'half', // 'half' or 'float'
    pattern = 'gradient', // 'gradient', 'checker', 'noise', 'solid'
  } = options;

  const writer = new BinaryWriter();

  // Magic number
  writer.writeUint32(20000630); // EXR magic number

  // Version field (2 = single-part scanline)
  writer.writeUint32(2);

  // Header attributes

  // channels attribute
  writer.writeString('channels');
  writer.writeString('chlist');
  // Size of channel list data
  const channelListSize = 4 * (1 + 4 + 4 + 4 + 4 + 1) + 1; // 4 channels + null terminator
  writer.writeUint32(channelListSize);

  // Channel entries: name, pixel type (0=uint, 1=half, 2=float), pLinear, reserved, xSampling, ySampling
  const pixelTypeValue = pixelType === 'half' ? 1 : 2;
  for (const ch of ['A', 'B', 'G', 'R']) {
    writer.writeString(ch);
    writer.writeUint32(pixelTypeValue); // pixel type
    writer.writeUint8(0); // pLinear
    writer.writeBytes([0, 0, 0]); // reserved
    writer.writeInt32(1); // xSampling
    writer.writeInt32(1); // ySampling
  }
  writer.writeUint8(0); // null terminator for channel list

  // compression attribute
  writer.writeString('compression');
  writer.writeString('compression');
  writer.writeUint32(1);
  writer.writeUint8(compression);

  // dataWindow attribute
  writer.writeString('dataWindow');
  writer.writeString('box2i');
  writer.writeUint32(16);
  writer.writeInt32(0); // xMin
  writer.writeInt32(0); // yMin
  writer.writeInt32(width - 1); // xMax
  writer.writeInt32(height - 1); // yMax

  // displayWindow attribute
  writer.writeString('displayWindow');
  writer.writeString('box2i');
  writer.writeUint32(16);
  writer.writeInt32(0);
  writer.writeInt32(0);
  writer.writeInt32(width - 1);
  writer.writeInt32(height - 1);

  // lineOrder attribute
  writer.writeString('lineOrder');
  writer.writeString('lineOrder');
  writer.writeUint32(1);
  writer.writeUint8(0); // INCREASING_Y

  // pixelAspectRatio attribute
  writer.writeString('pixelAspectRatio');
  writer.writeString('float');
  writer.writeUint32(4);
  writer.writeFloat(1.0);

  // screenWindowCenter attribute
  writer.writeString('screenWindowCenter');
  writer.writeString('v2f');
  writer.writeUint32(8);
  writer.writeFloat(0.0);
  writer.writeFloat(0.0);

  // screenWindowWidth attribute
  writer.writeString('screenWindowWidth');
  writer.writeString('float');
  writer.writeUint32(4);
  writer.writeFloat(1.0);

  // End of header
  writer.writeUint8(0);

  // Generate pixel data
  const bytesPerPixel = pixelType === 'half' ? 2 : 4;
  const scanlineSize = width * 4 * bytesPerPixel; // 4 channels (ABGR)

  // Scanline offset table
  const headerSize = writer.size;
  const offsetTableSize = height * 8; // 64-bit offsets
  let currentOffset = headerSize + offsetTableSize;

  // Write offset table
  for (let y = 0; y < height; y++) {
    writer.writeUint64(currentOffset);
    currentOffset += 4 + 4 + scanlineSize; // y coord (4) + size (4) + data
  }

  // Write scanlines
  for (let y = 0; y < height; y++) {
    writer.writeInt32(y); // y coordinate
    writer.writeUint32(scanlineSize); // data size

    // Generate pixel data for this scanline
    const scanlineData = new ArrayBuffer(scanlineSize);
    const view = pixelType === 'half'
      ? new Uint16Array(scanlineData)
      : new Float32Array(scanlineData);

    for (let x = 0; x < width; x++) {
      let r, g, b, a;

      switch (pattern) {
        case 'gradient':
          r = x / width;
          g = y / height;
          b = 1.0 - (x / width);
          a = 1.0;
          break;
        case 'checker':
          const checker = ((Math.floor(x / 32) + Math.floor(y / 32)) % 2) === 0;
          r = checker ? 1.0 : 0.2;
          g = checker ? 1.0 : 0.2;
          b = checker ? 1.0 : 0.2;
          a = 1.0;
          break;
        case 'noise':
          r = Math.random();
          g = Math.random();
          b = Math.random();
          a = 1.0;
          break;
        case 'solid':
          r = 0.5;
          g = 0.7;
          b = 0.3;
          a = 1.0;
          break;
        case 'hdr':
          // HDR values that exceed 1.0
          r = (x / width) * 10.0;
          g = (y / height) * 5.0;
          b = Math.sin(x * 0.1) * 2.0 + 2.0;
          a = 1.0;
          break;
        default:
          r = g = b = a = 1.0;
      }

      // EXR stores channels in alphabetical order: A, B, G, R
      const idx = x * 4;
      if (pixelType === 'half') {
        view[idx + 0] = float32ToFloat16(a);
        view[idx + 1] = float32ToFloat16(b);
        view[idx + 2] = float32ToFloat16(g);
        view[idx + 3] = float32ToFloat16(r);
      } else {
        view[idx + 0] = a;
        view[idx + 1] = b;
        view[idx + 2] = g;
        view[idx + 3] = r;
      }
    }

    writer.writeBuffer(Buffer.from(scanlineData));
  }

  return writer.toBuffer();
}

// Main
const args = process.argv.slice(2);
const width = parseInt(args[0]) || 512;
const height = parseInt(args[1]) || 512;
const outputPath = args[2] || 'test-synthetic.exr';
const pattern = args[3] || 'gradient';

console.log(`Generating ${width}x${height} EXR with pattern: ${pattern}`);

const exrBuffer = generateEXR(width, height, {
  compression: COMPRESSION.NO_COMPRESSION,
  pixelType: 'half',
  pattern: pattern,
});

fs.writeFileSync(outputPath, exrBuffer);
console.log(`Written: ${outputPath} (${exrBuffer.length} bytes)`);

// Also generate some test files if running without args
if (args.length === 0) {
  // 256x256 gradient
  const small = generateEXR(256, 256, { pattern: 'gradient', pixelType: 'half' });
  fs.writeFileSync('test-256-gradient.exr', small);
  console.log(`Written: test-256-gradient.exr (${small.length} bytes)`);

  // 512x512 HDR pattern
  const hdr = generateEXR(512, 512, { pattern: 'hdr', pixelType: 'half' });
  fs.writeFileSync('test-512-hdr.exr', hdr);
  console.log(`Written: test-512-hdr.exr (${hdr.length} bytes)`);

  // 1024x512 (typical HDRI aspect ratio)
  const hdri = generateEXR(1024, 512, { pattern: 'gradient', pixelType: 'half' });
  fs.writeFileSync('test-1024x512-gradient.exr', hdri);
  console.log(`Written: test-1024x512-gradient.exr (${hdri.length} bytes)`);
}
