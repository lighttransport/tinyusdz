#!/usr/bin/env node
/**
 * HDRGen - Synthetic HDR/EXR Environment Map Generator
 *
 * Generates procedural environment maps for IBL testing and visualization
 * Supports: HDR (Radiance RGBE), EXR (OpenEXR), lat-long and cubemap projections
 *
 * Copyright 2024 - Present, Light Transport Entertainment Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

import * as fs from 'fs';
import * as path from 'path';

// ============================================================================
// Math Utilities
// ============================================================================

class Vec3 {
  constructor(x = 0, y = 0, z = 0) {
    this.x = x;
    this.y = y;
    this.z = z;
  }

  static add(a, b) {
    return new Vec3(a.x + b.x, a.y + b.y, a.z + b.z);
  }

  static sub(a, b) {
    return new Vec3(a.x - b.x, a.y - b.y, a.z - b.z);
  }

  static mul(v, s) {
    return new Vec3(v.x * s, v.y * s, v.z * s);
  }

  static dot(a, b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
  }

  static cross(a, b) {
    return new Vec3(
      a.y * b.z - a.z * b.y,
      a.z * b.x - a.x * b.z,
      a.x * b.y - a.y * b.x
    );
  }

  length() {
    return Math.sqrt(this.x * this.x + this.y * this.y + this.z * this.z);
  }

  normalize() {
    const len = this.length();
    if (len > 0) {
      return new Vec3(this.x / len, this.y / len, this.z / len);
    }
    return new Vec3(0, 0, 0);
  }

  static lerp(a, b, t) {
    return new Vec3(
      a.x + (b.x - a.x) * t,
      a.y + (b.y - a.y) * t,
      a.z + (b.z - a.z) * t
    );
  }
}

// ============================================================================
// HDR Image Buffer
// ============================================================================

class HDRImage {
  constructor(width, height) {
    this.width = width;
    this.height = height;
    // Store as float32 RGB (linear color space)
    this.data = new Float32Array(width * height * 3);
  }

  setPixel(x, y, r, g, b) {
    const idx = (y * this.width + x) * 3;
    this.data[idx + 0] = r;
    this.data[idx + 1] = g;
    this.data[idx + 2] = b;
  }

  getPixel(x, y) {
    const idx = (y * this.width + x) * 3;
    return {
      r: this.data[idx + 0],
      g: this.data[idx + 1],
      b: this.data[idx + 2]
    };
  }

  // Convert lat-long (u,v) to direction vector
  static latLongToDir(u, v) {
    const phi = u * Math.PI * 2.0;   // 0 to 2π
    const theta = v * Math.PI;        // 0 to π
    const sinTheta = Math.sin(theta);
    return new Vec3(
      sinTheta * Math.cos(phi),
      Math.cos(theta),
      sinTheta * Math.sin(phi)
    );
  }

  // Convert direction vector to lat-long (u,v)
  static dirToLatLong(dir) {
    const theta = Math.acos(Math.max(-1, Math.min(1, dir.y)));
    const phi = Math.atan2(dir.z, dir.x);
    return {
      u: (phi + Math.PI) / (Math.PI * 2.0),
      v: theta / Math.PI
    };
  }
}

// ============================================================================
// Image Transformation Utilities
// ============================================================================

class ImageTransform {
  /**
   * Rotate environment map around Y axis
   * @param {HDRImage} image - Source image
   * @param {number} angleDegrees - Rotation angle in degrees (positive = counterclockwise)
   * @returns {HDRImage} - Rotated image
   */
  static rotate(image, angleDegrees) {
    console.log(`Rotating environment map by ${angleDegrees}°...`);

    const rotated = new HDRImage(image.width, image.height);
    const angleRad = (angleDegrees * Math.PI) / 180.0;

    for (let y = 0; y < image.height; y++) {
      for (let x = 0; x < image.width; x++) {
        // Get current UV
        let u = x / image.width;
        const v = y / image.height;

        // Rotate U coordinate
        u = u + (angleRad / (Math.PI * 2.0));
        u = u - Math.floor(u); // Wrap to [0, 1]

        // Sample from source image with bilinear filtering
        const fx = u * (image.width - 1);
        const fy = v * (image.height - 1);

        const x0 = Math.floor(fx);
        const y0 = Math.floor(fy);
        const x1 = (x0 + 1) % image.width; // Wrap horizontally
        const y1 = Math.min(y0 + 1, image.height - 1);

        const tx = fx - x0;
        const ty = fy - y0;

        const c00 = image.getPixel(x0, y0);
        const c10 = image.getPixel(x1, y0);
        const c01 = image.getPixel(x0, y1);
        const c11 = image.getPixel(x1, y1);

        const r = (1 - tx) * (1 - ty) * c00.r + tx * (1 - ty) * c10.r +
                  (1 - tx) * ty * c01.r + tx * ty * c11.r;
        const g = (1 - tx) * (1 - ty) * c00.g + tx * (1 - ty) * c10.g +
                  (1 - tx) * ty * c01.g + tx * ty * c11.g;
        const b = (1 - tx) * (1 - ty) * c00.b + tx * (1 - ty) * c10.b +
                  (1 - tx) * ty * c01.b + tx * ty * c11.b;

        rotated.setPixel(x, y, r, g, b);
      }
    }

    return rotated;
  }

  /**
   * Scale intensity of entire image
   * @param {HDRImage} image - Image to scale (modified in place)
   * @param {number} scale - Intensity multiplier
   */
  static scaleIntensity(image, scale) {
    if (scale === 1.0) return;

    console.log(`Scaling intensity by ${scale}x...`);

    for (let i = 0; i < image.data.length; i++) {
      image.data[i] *= scale;
    }
  }
}

// ============================================================================
// Tone Mapping and LDR Conversion
// ============================================================================

class ToneMapper {
  /**
   * Apply tone mapping to HDR image for LDR display
   * @param {HDRImage} hdrImage - Source HDR image
   * @param {Object} options - Tone mapping options
   * @returns {Uint8ClampedArray} - 8-bit RGB data
   */
  static tonemapToLDR(hdrImage, options = {}) {
    const {
      exposure = 1.0,      // Exposure adjustment (EV)
      gamma = 2.2,         // Gamma correction for display
      method = 'reinhard'  // Tone mapping method: 'simple', 'reinhard', 'aces'
    } = options;

    console.log(`Tone mapping: method=${method}, exposure=${exposure}, gamma=${gamma}`);

    const { width, height, data } = hdrImage;
    const ldrData = new Uint8ClampedArray(width * height * 3);

    const exposureScale = Math.pow(2.0, exposure);
    const invGamma = 1.0 / gamma;

    for (let i = 0; i < data.length; i += 3) {
      let r = data[i + 0] * exposureScale;
      let g = data[i + 1] * exposureScale;
      let b = data[i + 2] * exposureScale;

      // Apply tone mapping operator
      switch (method) {
        case 'simple':
          // Simple exposure + clamp
          r = Math.min(r, 1.0);
          g = Math.min(g, 1.0);
          b = Math.min(b, 1.0);
          break;

        case 'reinhard':
          // Reinhard tone mapping: x / (1 + x)
          r = r / (1.0 + r);
          g = g / (1.0 + g);
          b = b / (1.0 + b);
          break;

        case 'aces':
          // ACES filmic tone mapping (approximation)
          r = ToneMapper.acesToneMap(r);
          g = ToneMapper.acesToneMap(g);
          b = ToneMapper.acesToneMap(b);
          break;

        default:
          r = Math.min(r, 1.0);
          g = Math.min(g, 1.0);
          b = Math.min(b, 1.0);
      }

      // Apply gamma correction
      r = Math.pow(Math.max(0, r), invGamma);
      g = Math.pow(Math.max(0, g), invGamma);
      b = Math.pow(Math.max(0, b), invGamma);

      // Convert to 8-bit
      ldrData[i + 0] = Math.round(Math.min(255, r * 255));
      ldrData[i + 1] = Math.round(Math.min(255, g * 255));
      ldrData[i + 2] = Math.round(Math.min(255, b * 255));
    }

    return ldrData;
  }

  /**
   * ACES filmic tone mapping curve
   */
  static acesToneMap(x) {
    const a = 2.51;
    const b = 0.03;
    const c = 2.43;
    const d = 0.59;
    const e = 0.14;
    return Math.min(1.0, Math.max(0.0, (x * (a * x + b)) / (x * (c * x + d) + e)));
  }
}

// ============================================================================
// LDR File Format Writers
// ============================================================================

class LDRWriter {
  /**
   * Write BMP format (24-bit RGB, uncompressed)
   */
  static writeBMP(ldrData, width, height, filepath) {
    // BMP requires rows to be padded to 4-byte boundary
    const rowSize = Math.floor((24 * width + 31) / 32) * 4;
    const pixelDataSize = rowSize * height;
    const fileSize = 54 + pixelDataSize; // 14-byte header + 40-byte DIB header + pixel data

    const buffer = Buffer.alloc(fileSize);

    // BMP Header (14 bytes)
    buffer.write('BM', 0); // Signature
    buffer.writeUInt32LE(fileSize, 2); // File size
    buffer.writeUInt32LE(0, 6); // Reserved
    buffer.writeUInt32LE(54, 10); // Pixel data offset

    // DIB Header (BITMAPINFOHEADER, 40 bytes)
    buffer.writeUInt32LE(40, 14); // DIB header size
    buffer.writeInt32LE(width, 18); // Width
    buffer.writeInt32LE(height, 22); // Height
    buffer.writeUInt16LE(1, 26); // Planes
    buffer.writeUInt16LE(24, 28); // Bits per pixel
    buffer.writeUInt32LE(0, 30); // Compression (0 = none)
    buffer.writeUInt32LE(pixelDataSize, 34); // Image size
    buffer.writeInt32LE(2835, 38); // X pixels per meter (72 DPI)
    buffer.writeInt32LE(2835, 42); // Y pixels per meter
    buffer.writeUInt32LE(0, 46); // Colors in palette
    buffer.writeUInt32LE(0, 50); // Important colors

    // Pixel data (bottom-up, BGR format)
    let offset = 54;
    for (let y = height - 1; y >= 0; y--) {
      for (let x = 0; x < width; x++) {
        const idx = (y * width + x) * 3;
        buffer[offset++] = ldrData[idx + 2]; // B
        buffer[offset++] = ldrData[idx + 1]; // G
        buffer[offset++] = ldrData[idx + 0]; // R
      }
      // Padding to 4-byte boundary
      while (offset % 4 !== 0) {
        buffer[offset++] = 0;
      }
    }

    fs.writeFileSync(filepath, buffer);
    console.log(`✓ Wrote BMP file: ${filepath}`);
  }

  /**
   * Write PNG format (8-bit RGB, uncompressed)
   * Simple implementation without compression
   */
  static writePNG(ldrData, width, height, filepath) {
    // For production, use a PNG library. This is a simplified implementation.
    // We'll write an uncompressed PNG using filter type 0 (None)

    const pngSignature = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);

    // IHDR chunk
    const ihdr = Buffer.alloc(13);
    ihdr.writeUInt32BE(width, 0);
    ihdr.writeUInt32BE(height, 4);
    ihdr.writeUInt8(8, 8); // Bit depth
    ihdr.writeUInt8(2, 9); // Color type (2 = RGB)
    ihdr.writeUInt8(0, 10); // Compression
    ihdr.writeUInt8(0, 11); // Filter
    ihdr.writeUInt8(0, 12); // Interlace

    // IDAT chunk (pixel data with filter bytes)
    // Each scanline: filter byte (0) + RGB data
    const scanlineSize = 1 + width * 3;
    const idatRaw = Buffer.alloc(scanlineSize * height);

    for (let y = 0; y < height; y++) {
      idatRaw[y * scanlineSize] = 0; // Filter type: None
      for (let x = 0; x < width; x++) {
        const srcIdx = (y * width + x) * 3;
        const dstIdx = y * scanlineSize + 1 + x * 3;
        idatRaw[dstIdx + 0] = ldrData[srcIdx + 0]; // R
        idatRaw[dstIdx + 1] = ldrData[srcIdx + 1]; // G
        idatRaw[dstIdx + 2] = ldrData[srcIdx + 2]; // B
      }
    }

    // Simple zlib compression would go here, but for now use uncompressed
    // For production, use zlib or a PNG library
    console.warn('PNG: Using simplified format (consider using sharp/pngjs for production)');

    // Build PNG file
    const chunks = [];
    chunks.push(pngSignature);
    chunks.push(LDRWriter.createPNGChunk('IHDR', ihdr));
    chunks.push(LDRWriter.createPNGChunk('IDAT', idatRaw));
    chunks.push(LDRWriter.createPNGChunk('IEND', Buffer.alloc(0)));

    const pngBuffer = Buffer.concat(chunks);
    fs.writeFileSync(filepath, pngBuffer);
    console.log(`✓ Wrote PNG file: ${filepath}`);
  }

  /**
   * Create PNG chunk with length, type, data, and CRC
   */
  static createPNGChunk(type, data) {
    const length = Buffer.alloc(4);
    length.writeUInt32BE(data.length, 0);

    const typeBuffer = Buffer.from(type, 'ascii');
    const crcData = Buffer.concat([typeBuffer, data]);
    const crc = Buffer.alloc(4);
    crc.writeUInt32BE(LDRWriter.crc32(crcData), 0);

    return Buffer.concat([length, typeBuffer, data, crc]);
  }

  /**
   * CRC32 calculation for PNG
   */
  static crc32(buffer) {
    let crc = 0xFFFFFFFF;
    for (let i = 0; i < buffer.length; i++) {
      crc = crc ^ buffer[i];
      for (let j = 0; j < 8; j++) {
        crc = (crc >>> 1) ^ (0xEDB88320 & -(crc & 1));
      }
    }
    return (crc ^ 0xFFFFFFFF) >>> 0; // Force unsigned 32-bit
  }

  /**
   * Write JPEG format
   * Note: Requires external library for production use
   */
  static writeJPEG(ldrData, width, height, filepath, quality = 90) {
    console.warn('JPEG writing requires external library (e.g., jpeg-js)');
    console.warn('Converting to BMP instead');
    const bmpPath = filepath.replace(/\.jpe?g$/i, '.bmp');
    LDRWriter.writeBMP(ldrData, width, height, bmpPath);
  }
}

// ============================================================================
// HDR File Format Writers
// ============================================================================

class HDRWriter {
  /**
   * Write Radiance RGBE (.hdr) format
   * https://en.wikipedia.org/wiki/RGBE_image_format
   */
  static writeRGBE(image, filepath) {
    const { width, height, data } = image;

    // RGBE encoding function
    function encodeRGBE(r, g, b) {
      const maxComp = Math.max(r, g, b);
      if (maxComp < 1e-32) {
        return Buffer.from([0, 0, 0, 0]);
      }

      const exponent = Math.floor(Math.log2(maxComp)) + 128;
      const scale = Math.pow(2, exponent - 128);

      const re = Math.floor((r / scale) * 255.0 + 0.5);
      const ge = Math.floor((g / scale) * 255.0 + 0.5);
      const be = Math.floor((b / scale) * 255.0 + 0.5);

      return Buffer.from([
        Math.min(255, re),
        Math.min(255, ge),
        Math.min(255, be),
        exponent
      ]);
    }

    // Build HDR header
    const header = [
      '#?RADIANCE',
      'FORMAT=32-bit_rle_rgbe',
      `EXPOSURE=1.0`,
      '',
      `-Y ${height} +X ${width}`,
      ''
    ].join('\n');

    const headerBuf = Buffer.from(header, 'ascii');

    // Encode pixel data
    const pixelBuf = Buffer.alloc(width * height * 4);
    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) {
        const idx = (y * width + x) * 3;
        const r = data[idx + 0];
        const g = data[idx + 1];
        const b = data[idx + 2];
        const rgbe = encodeRGBE(r, g, b);
        rgbe.copy(pixelBuf, (y * width + x) * 4);
      }
    }

    // Write file
    const fullBuf = Buffer.concat([headerBuf, pixelBuf]);
    fs.writeFileSync(filepath, fullBuf);
    console.log(`✓ Wrote HDR file: ${filepath}`);
  }

  /**
   * Write OpenEXR format (simplified, uncompressed scanline)
   * For production use, consider using openexr npm package
   */
  static writeEXR(image, filepath) {
    // For now, write as HDR since proper EXR requires external library
    // In production, use @openexr/node or similar
    console.warn('EXR writing requires external library, writing as HDR instead');
    const hdrPath = filepath.replace(/\.exr$/i, '.hdr');
    HDRWriter.writeRGBE(image, hdrPath);
  }
}

// ============================================================================
// Environment Map Presets
// ============================================================================

class EnvMapPresets {
  /**
   * White Furnace - Uniform white environment for energy conservation testing
   * Perfect for validating that BRDF integrates to 1.0
   */
  static whiteFurnace(image, intensity = 1.0) {
    console.log(`Generating White Furnace (${image.width}x${image.height})...`);

    for (let y = 0; y < image.height; y++) {
      for (let x = 0; x < image.width; x++) {
        image.setPixel(x, y, intensity, intensity, intensity);
      }
    }
  }

  /**
   * Sun & Sky - Procedural Hosek-Wilkie sky model approximation
   * Simplified version with sun disk and gradient sky
   */
  static sunSky(image, options = {}) {
    const {
      sunElevation = 45,      // Sun elevation in degrees (0 = horizon, 90 = zenith)
      sunAzimuth = 135,       // Sun azimuth in degrees (0 = north, 90 = east)
      sunIntensity = 100.0,   // Sun disk intensity
      sunRadius = 0.02,       // Sun angular radius (radians)
      skyIntensity = 0.5,     // Base sky intensity
      horizonColor = new Vec3(0.8, 0.9, 1.0),  // Horizon tint
      zenithColor = new Vec3(0.3, 0.5, 0.9),   // Zenith color
    } = options;

    console.log(`Generating Sun & Sky (${image.width}x${image.height})...`);
    console.log(`  Sun: elevation=${sunElevation}°, azimuth=${sunAzimuth}°, intensity=${sunIntensity}`);

    // Convert sun angles to direction
    const elevRad = sunElevation * Math.PI / 180;
    const azimRad = sunAzimuth * Math.PI / 180;
    const sunDir = new Vec3(
      Math.cos(elevRad) * Math.cos(azimRad),
      Math.sin(elevRad),
      Math.cos(elevRad) * Math.sin(azimRad)
    ).normalize();

    for (let y = 0; y < image.height; y++) {
      for (let x = 0; x < image.width; x++) {
        const u = x / image.width;
        const v = y / image.height;

        const dir = HDRImage.latLongToDir(u, v);

        // Sky gradient based on elevation
        const elevation = Math.asin(Math.max(-1, Math.min(1, dir.y)));
        const elevNorm = (elevation + Math.PI / 2) / Math.PI; // 0 at bottom, 1 at top

        // Interpolate between horizon and zenith
        const skyColor = Vec3.lerp(horizonColor, zenithColor, elevNorm);
        let r = skyColor.x * skyIntensity;
        let g = skyColor.y * skyIntensity;
        let b = skyColor.z * skyIntensity;

        // Add sun disk
        const angleToCosun = Vec3.dot(dir, sunDir);
        const angleToSun = Math.acos(Math.max(-1, Math.min(1, angleToCosun)));

        if (angleToSun < sunRadius) {
          // Inside sun disk
          const falloff = 1.0 - (angleToSun / sunRadius);
          const sunCol = Vec3.mul(new Vec3(1, 0.95, 0.8), sunIntensity);
          r += sunCol.x * falloff;
          g += sunCol.y * falloff;
          b += sunCol.z * falloff;
        } else if (angleToSun < sunRadius * 3) {
          // Sun glow
          const falloff = 1.0 - ((angleToSun - sunRadius) / (sunRadius * 2));
          const glowIntensity = sunIntensity * 0.1 * falloff * falloff;
          r += glowIntensity;
          g += glowIntensity * 0.9;
          b += glowIntensity * 0.7;
        }

        image.setPixel(x, y, r, g, b);
      }
    }
  }

  /**
   * Studio Lighting - 3-point lighting setup
   * Key light (main), fill light (shadows), rim/back light
   */
  static studioLighting(image, options = {}) {
    const {
      keyIntensity = 50.0,
      fillIntensity = 10.0,
      rimIntensity = 20.0,
      ambientIntensity = 0.5,
      keyColor = new Vec3(1.0, 0.98, 0.95),     // Warm key
      fillColor = new Vec3(0.8, 0.85, 1.0),     // Cool fill
      rimColor = new Vec3(1.0, 1.0, 1.0),       // White rim
      ambientColor = new Vec3(0.5, 0.5, 0.5),   // Neutral ambient
    } = options;

    console.log(`Generating Studio Lighting (${image.width}x${image.height})...`);
    console.log(`  Key=${keyIntensity}, Fill=${fillIntensity}, Rim=${rimIntensity}`);

    // Light positions (as directions)
    const keyLight = new Vec3(0.7, 0.5, 0.5).normalize();      // Front-right, elevated
    const fillLight = new Vec3(-0.5, 0.3, 0.3).normalize();    // Front-left, lower
    const rimLight = new Vec3(0, 0.4, -0.9).normalize();       // Back, elevated

    // Light spreads (angular size in radians)
    const keySpread = 0.3;
    const fillSpread = 0.5;
    const rimSpread = 0.2;

    for (let y = 0; y < image.height; y++) {
      for (let x = 0; x < image.width; x++) {
        const u = x / image.width;
        const v = y / image.height;

        const dir = HDRImage.latLongToDir(u, v);

        // Start with ambient
        let r = ambientColor.x * ambientIntensity;
        let g = ambientColor.y * ambientIntensity;
        let b = ambientColor.z * ambientIntensity;

        // Add key light
        const keyDot = Math.max(0, Vec3.dot(dir, keyLight));
        const keyAngle = Math.acos(Math.max(0, Math.min(1, keyDot)));
        if (keyAngle < keySpread) {
          const falloff = Math.pow(1.0 - (keyAngle / keySpread), 2);
          r += keyColor.x * keyIntensity * falloff;
          g += keyColor.y * keyIntensity * falloff;
          b += keyColor.z * keyIntensity * falloff;
        }

        // Add fill light
        const fillDot = Math.max(0, Vec3.dot(dir, fillLight));
        const fillAngle = Math.acos(Math.max(0, Math.min(1, fillDot)));
        if (fillAngle < fillSpread) {
          const falloff = Math.pow(1.0 - (fillAngle / fillSpread), 2);
          r += fillColor.x * fillIntensity * falloff;
          g += fillColor.y * fillIntensity * falloff;
          b += fillColor.z * fillIntensity * falloff;
        }

        // Add rim light
        const rimDot = Math.max(0, Vec3.dot(dir, rimLight));
        const rimAngle = Math.acos(Math.max(0, Math.min(1, rimDot)));
        if (rimAngle < rimSpread) {
          const falloff = Math.pow(1.0 - (rimAngle / rimSpread), 3);
          r += rimColor.x * rimIntensity * falloff;
          g += rimColor.y * rimIntensity * falloff;
          b += rimColor.z * rimIntensity * falloff;
        }

        image.setPixel(x, y, r, g, b);
      }
    }
  }
}

// ============================================================================
// Cubemap Generator
// ============================================================================

class CubemapGenerator {
  /**
   * Generate 6 cubemap faces from an equirectangular environment map
   * Face order: +X, -X, +Y, -Y, +Z, -Z (standard OpenGL order)
   */
  static fromLatLong(latLongImage, faceSize = 512) {
    console.log(`Converting lat-long to cubemap (face size: ${faceSize}x${faceSize})...`);

    const faces = [];
    const faceNames = ['+X', '-X', '+Y', '-Y', '+Z', '-Z'];

    // Cubemap face directions
    const faceData = [
      // +X (right)
      { right: new Vec3(0, 0, -1), up: new Vec3(0, 1, 0), forward: new Vec3(1, 0, 0) },
      // -X (left)
      { right: new Vec3(0, 0, 1), up: new Vec3(0, 1, 0), forward: new Vec3(-1, 0, 0) },
      // +Y (top)
      { right: new Vec3(1, 0, 0), up: new Vec3(0, 0, -1), forward: new Vec3(0, 1, 0) },
      // -Y (bottom)
      { right: new Vec3(1, 0, 0), up: new Vec3(0, 0, 1), forward: new Vec3(0, -1, 0) },
      // +Z (front)
      { right: new Vec3(1, 0, 0), up: new Vec3(0, 1, 0), forward: new Vec3(0, 0, 1) },
      // -Z (back)
      { right: new Vec3(-1, 0, 0), up: new Vec3(0, 1, 0), forward: new Vec3(0, 0, -1) },
    ];

    for (let faceIdx = 0; faceIdx < 6; faceIdx++) {
      const face = new HDRImage(faceSize, faceSize);
      const { right, up, forward } = faceData[faceIdx];

      for (let y = 0; y < faceSize; y++) {
        for (let x = 0; x < faceSize; x++) {
          // Map to [-1, 1] range
          const u = (x / (faceSize - 1)) * 2.0 - 1.0;
          const v = (y / (faceSize - 1)) * 2.0 - 1.0;

          // Get direction for this texel
          const dir = Vec3.add(
            Vec3.add(Vec3.mul(right, u), Vec3.mul(up, -v)),
            forward
          ).normalize();

          // Convert to lat-long coords and sample
          const { u: latU, v: latV } = HDRImage.dirToLatLong(dir);
          const color = CubemapGenerator.sampleBilinear(latLongImage, latU, latV);

          face.setPixel(x, y, color.r, color.g, color.b);
        }
      }

      faces.push({ image: face, name: faceNames[faceIdx] });
    }

    return faces;
  }

  /**
   * Bilinear sampling from lat-long image
   */
  static sampleBilinear(image, u, v) {
    // Wrap u, clamp v
    u = u - Math.floor(u);
    v = Math.max(0, Math.min(1, v));

    const fx = u * (image.width - 1);
    const fy = v * (image.height - 1);

    const x0 = Math.floor(fx);
    const y0 = Math.floor(fy);
    const x1 = Math.min(x0 + 1, image.width - 1);
    const y1 = Math.min(y0 + 1, image.height - 1);

    const tx = fx - x0;
    const ty = fy - y0;

    const c00 = image.getPixel(x0, y0);
    const c10 = image.getPixel(x1, y0);
    const c01 = image.getPixel(x0, y1);
    const c11 = image.getPixel(x1, y1);

    const r = (1 - tx) * (1 - ty) * c00.r + tx * (1 - ty) * c10.r +
              (1 - tx) * ty * c01.r + tx * ty * c11.r;
    const g = (1 - tx) * (1 - ty) * c00.g + tx * (1 - ty) * c10.g +
              (1 - tx) * ty * c01.g + tx * ty * c11.g;
    const b = (1 - tx) * (1 - ty) * c00.b + tx * (1 - ty) * c10.b +
              (1 - tx) * ty * c01.b + tx * ty * c11.b;

    return { r, g, b };
  }
}

// ============================================================================
// Public API
// ============================================================================

export class HDRGenerator {
  /**
   * Generate environment map with specified preset
   *
   * @param {Object} options - Generation options
   * @param {string} options.preset - Preset name: 'white-furnace', 'sun-sky', 'studio'
   * @param {number} options.width - Width in pixels (default: 2048 for latlong, 512 for cubemap)
   * @param {number} options.height - Height in pixels (default: 1024 for latlong)
   * @param {string} options.projection - 'latlong' or 'cubemap'
   * @param {string} options.format - 'hdr', 'exr', 'png', 'bmp', 'jpg'/'jpeg'
   * @param {string} options.output - Output file path
   * @param {Object} options.presetOptions - Preset-specific options
   * @param {number} options.rotation - Rotation angle in degrees (default: 0)
   * @param {number} options.intensityScale - Intensity multiplier (default: 1.0)
   * @param {Object} options.tonemapOptions - Tone mapping options for LDR output
   */
  static generate(options) {
    const {
      preset = 'white-furnace',
      width = 2048,
      height = 1024,
      projection = 'latlong',
      format = 'hdr',
      output = null,
      presetOptions = {},
      rotation = 0,
      intensityScale = 1.0,
      tonemapOptions = {}
    } = options;

    console.log('\n=== HDR Environment Map Generator ===');
    console.log(`Preset: ${preset}`);
    console.log(`Resolution: ${width}x${height}`);
    console.log(`Projection: ${projection}`);
    console.log(`Format: ${format.toUpperCase()}`);
    if (rotation !== 0) console.log(`Rotation: ${rotation}°`);
    if (intensityScale !== 1.0) console.log(`Intensity Scale: ${intensityScale}x`);

    // Generate lat-long image first
    let latLongImage = new HDRImage(width, height);

    // Apply preset
    switch (preset) {
      case 'white-furnace':
        EnvMapPresets.whiteFurnace(latLongImage, presetOptions.intensity || 1.0);
        break;
      case 'sun-sky':
        EnvMapPresets.sunSky(latLongImage, presetOptions);
        break;
      case 'studio':
        EnvMapPresets.studioLighting(latLongImage, presetOptions);
        break;
      default:
        throw new Error(`Unknown preset: ${preset}`);
    }

    // Apply transformations
    if (rotation !== 0) {
      latLongImage = ImageTransform.rotate(latLongImage, rotation);
    }

    if (intensityScale !== 1.0) {
      ImageTransform.scaleIntensity(latLongImage, intensityScale);
    }

    // Determine if output is LDR or HDR
    const isLDR = ['png', 'bmp', 'jpg', 'jpeg'].includes(format.toLowerCase());

    // Generate output
    if (projection === 'latlong') {
      // Direct lat-long output
      if (output) {
        const filepath = output.endsWith(`.${format}`) ? output : `${output}.${format}`;
        HDRGenerator._writeImage(latLongImage, format, filepath, isLDR, tonemapOptions);
      }
      return { latLongImage };
    } else if (projection === 'cubemap') {
      // Convert to cubemap
      const faceSize = Math.min(width, height); // Use smaller dimension for cube face
      const faces = CubemapGenerator.fromLatLong(latLongImage, faceSize);

      if (output) {
        const dir = path.dirname(output);
        const base = path.basename(output, path.extname(output));

        for (const face of faces) {
          const facePath = path.join(dir, `${base}_${face.name}.${format}`);
          HDRGenerator._writeImage(face.image, format, facePath, isLDR, tonemapOptions);
        }
      }
      return { faces };
    }
  }

  /**
   * Internal helper to write image in appropriate format
   */
  static _writeImage(image, format, filepath, isLDR, tonemapOptions) {
    if (isLDR) {
      // Convert HDR to LDR via tone mapping
      const ldrData = ToneMapper.tonemapToLDR(image, tonemapOptions);
      const fmt = format.toLowerCase();

      switch (fmt) {
        case 'png':
          LDRWriter.writePNG(ldrData, image.width, image.height, filepath);
          break;
        case 'bmp':
          LDRWriter.writeBMP(ldrData, image.width, image.height, filepath);
          break;
        case 'jpg':
        case 'jpeg':
          LDRWriter.writeJPEG(ldrData, image.width, image.height, filepath);
          break;
        default:
          throw new Error(`Unknown LDR format: ${format}`);
      }
    } else {
      // HDR output
      if (format === 'hdr') {
        HDRWriter.writeRGBE(image, filepath);
      } else if (format === 'exr') {
        HDRWriter.writeEXR(image, filepath);
      } else {
        throw new Error(`Unknown HDR format: ${format}`);
      }
    }
  }
}

export {
  EnvMapPresets,
  HDRImage,
  CubemapGenerator,
  HDRWriter,
  LDRWriter,
  ToneMapper,
  ImageTransform,
  Vec3
};
