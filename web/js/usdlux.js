/**
 * TinyUSDZ UsdLux Light Demo
 * Visualizes USD lights using Three.js
 */

import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { TransformControls } from 'three/examples/jsm/controls/TransformControls.js';
import { RectAreaLightHelper } from 'three/examples/jsm/helpers/RectAreaLightHelper.js';
import { RectAreaLightUniformsLib } from 'three/examples/jsm/lights/RectAreaLightUniformsLib.js';
import { EffectComposer } from 'three/examples/jsm/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/examples/jsm/postprocessing/RenderPass.js';
import { ShaderPass } from 'three/examples/jsm/postprocessing/ShaderPass.js';
import { OutputPass } from 'three/examples/jsm/postprocessing/OutputPass.js';
import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';

// Light-to-HDRI projection (no TinyUSDZ dependency)
import {
  LightHDRIProjection,
  SphereLight,
  AreaLight,
  DiskLight,
  PointLight,
  DistantLight,
  writeEXR
} from './light-hdri-projection.js';

// ============================================
// CIE 1931 2-degree Standard Observer Color Matching Functions
// Wavelength range: 380nm - 780nm, 5nm steps
// ============================================

const CIE_WAVELENGTHS = [
  380, 385, 390, 395, 400, 405, 410, 415, 420, 425, 430, 435, 440, 445, 450,
  455, 460, 465, 470, 475, 480, 485, 490, 495, 500, 505, 510, 515, 520, 525,
  530, 535, 540, 545, 550, 555, 560, 565, 570, 575, 580, 585, 590, 595, 600,
  605, 610, 615, 620, 625, 630, 635, 640, 645, 650, 655, 660, 665, 670, 675,
  680, 685, 690, 695, 700, 705, 710, 715, 720, 725, 730, 735, 740, 745, 750,
  755, 760, 765, 770, 775, 780
];

// CIE 1931 x-bar values
const CIE_X = [
  0.001368, 0.002236, 0.004243, 0.007650, 0.014310, 0.023190, 0.043510, 0.077630,
  0.134380, 0.214770, 0.283900, 0.328500, 0.348280, 0.348060, 0.336200, 0.318700,
  0.290800, 0.251100, 0.195360, 0.142100, 0.095640, 0.058010, 0.032010, 0.014700,
  0.004900, 0.002400, 0.009300, 0.029100, 0.063270, 0.109600, 0.165500, 0.225750,
  0.290400, 0.359700, 0.433450, 0.512050, 0.594500, 0.678400, 0.762100, 0.842500,
  0.916300, 0.978600, 1.026300, 1.056700, 1.062200, 1.045600, 1.002600, 0.938400,
  0.854450, 0.751400, 0.642400, 0.541900, 0.447900, 0.360800, 0.283500, 0.218700,
  0.164900, 0.121200, 0.087400, 0.063600, 0.046770, 0.032900, 0.022700, 0.015840,
  0.011359, 0.008111, 0.005790, 0.004109, 0.002899, 0.002049, 0.001440, 0.001000,
  0.000690, 0.000476, 0.000332, 0.000235, 0.000166, 0.000117, 0.000083, 0.000059,
  0.000042
];

// CIE 1931 y-bar values
const CIE_Y = [
  0.000039, 0.000064, 0.000120, 0.000217, 0.000396, 0.000640, 0.001210, 0.002180,
  0.004000, 0.007300, 0.011600, 0.016840, 0.023000, 0.029800, 0.038000, 0.048000,
  0.060000, 0.073900, 0.090980, 0.112600, 0.139020, 0.169300, 0.208020, 0.258600,
  0.323000, 0.407300, 0.503000, 0.608200, 0.710000, 0.793200, 0.862000, 0.914850,
  0.954000, 0.980300, 0.994950, 1.000000, 0.995000, 0.978600, 0.952000, 0.915400,
  0.870000, 0.816300, 0.757000, 0.694900, 0.631000, 0.566800, 0.503000, 0.441200,
  0.381000, 0.321000, 0.265000, 0.217000, 0.175000, 0.138200, 0.107000, 0.081600,
  0.061000, 0.044580, 0.032000, 0.023200, 0.017000, 0.011920, 0.008210, 0.005723,
  0.004102, 0.002929, 0.002091, 0.001484, 0.001047, 0.000740, 0.000520, 0.000361,
  0.000249, 0.000172, 0.000120, 0.000085, 0.000060, 0.000042, 0.000030, 0.000021,
  0.000015
];

// CIE 1931 z-bar values
const CIE_Z = [
  0.006450, 0.010550, 0.020050, 0.036210, 0.067850, 0.110200, 0.207400, 0.371300,
  0.645600, 1.039050, 1.385600, 1.622960, 1.747060, 1.782600, 1.772110, 1.744100,
  1.669200, 1.528100, 1.287640, 1.041900, 0.812950, 0.616200, 0.465180, 0.353300,
  0.272000, 0.212300, 0.158200, 0.111700, 0.078250, 0.057250, 0.042160, 0.029840,
  0.020300, 0.013400, 0.008750, 0.005750, 0.003900, 0.002750, 0.002100, 0.001800,
  0.001650, 0.001400, 0.001100, 0.001000, 0.000800, 0.000600, 0.000340, 0.000240,
  0.000190, 0.000100, 0.000050, 0.000030, 0.000020, 0.000010, 0.000000, 0.000000,
  0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000,
  0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000,
  0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000,
  0.000000
];

// D65 Standard Illuminant (normalized)
const D65_SPD = [
  49.9755, 52.3118, 54.6482, 68.7015, 82.7549, 87.1204, 91.486, 92.4589, 93.4318,
  90.057, 86.6823, 95.7736, 104.865, 110.936, 117.008, 117.41, 117.812, 116.336,
  114.861, 115.392, 115.923, 112.367, 108.811, 109.082, 109.354, 108.578, 107.802,
  106.296, 104.79, 106.239, 107.689, 106.047, 104.405, 104.225, 104.046, 102.023,
  100.0, 98.1671, 96.3342, 96.0611, 95.788, 92.2368, 88.6856, 89.3459, 90.0062,
  89.8026, 89.5991, 88.6489, 87.6987, 85.4936, 83.2886, 83.4939, 83.6992, 81.863,
  80.0268, 80.1207, 80.2146, 81.2462, 82.2778, 80.281, 78.2842, 74.0027, 69.7213,
  70.6652, 71.6091, 72.979, 74.349, 67.9765, 61.604, 65.7448, 69.8856, 72.4863,
  75.087, 69.3398, 63.5927, 55.0054, 46.4182, 56.6118, 66.8054, 65.0941, 63.3828
];

// Standard illuminant presets
const ILLUMINANT_PRESETS = {
  'd65': D65_SPD,
  'a': null,   // Will be generated using Planckian formula
  'd50': null, // Will be generated
  'e': null,   // Equal energy - flat spectrum
};

/**
 * Linear interpolation helper
 */
function lerp(a, b, t) {
  return a + (b - a) * t;
}

/**
 * Interpolate CIE color matching function at given wavelength
 */
function interpolateCMF(wavelength, data) {
  if (wavelength < 380) return data[0];
  if (wavelength > 780) return data[data.length - 1];

  const idx = (wavelength - 380) / 5;
  const i0 = Math.floor(idx);
  const i1 = Math.min(i0 + 1, data.length - 1);
  const t = idx - i0;

  return lerp(data[i0], data[i1], t);
}

/**
 * Convert a single wavelength to XYZ
 */
function wavelengthToXYZ(wavelength) {
  return {
    x: interpolateCMF(wavelength, CIE_X),
    y: interpolateCMF(wavelength, CIE_Y),
    z: interpolateCMF(wavelength, CIE_Z)
  };
}

/**
 * Convert XYZ to linear sRGB
 */
function xyzToLinearRGB(X, Y, Z) {
  // sRGB D65 matrix
  const r =  3.2404542 * X - 1.5371385 * Y - 0.4985314 * Z;
  const g = -0.9692660 * X + 1.8760108 * Y + 0.0415560 * Z;
  const b =  0.0556434 * X - 0.2040259 * Y + 1.0572252 * Z;
  return { r, g, b };
}

/**
 * Apply sRGB gamma correction
 */
function linearToSRGB(c) {
  if (c <= 0.0031308) {
    return 12.92 * c;
  }
  return 1.055 * Math.pow(c, 1 / 2.4) - 0.055;
}

/**
 * Convert a single wavelength to sRGB color
 * @param {number} wavelength - Wavelength in nanometers (380-780)
 * @param {number} intensity - Intensity multiplier (default 1.0)
 * @returns {Object} {r, g, b} values in 0-1 range
 */
function wavelengthToRGB(wavelength, intensity = 1.0) {
  const xyz = wavelengthToXYZ(wavelength);
  const linear = xyzToLinearRGB(xyz.x * intensity, xyz.y * intensity, xyz.z * intensity);

  // Normalize to prevent clipping while preserving hue
  const maxVal = Math.max(linear.r, linear.g, linear.b, 0.0001);
  const scale = maxVal > 1.0 ? 1.0 / maxVal : 1.0;

  return {
    r: Math.max(0, Math.min(1, linearToSRGB(linear.r * scale))),
    g: Math.max(0, Math.min(1, linearToSRGB(linear.g * scale))),
    b: Math.max(0, Math.min(1, linearToSRGB(linear.b * scale)))
  };
}

/**
 * Convert spectral power distribution (SPD) to XYZ
 * @param {Array} samples - Array of [wavelength, value] pairs
 * @param {string} interpolation - Interpolation method ('linear', 'held', 'cubic')
 * @returns {Object} {X, Y, Z} tristimulus values
 */
function spdToXYZ(samples, interpolation = 'linear') {
  if (!samples || samples.length === 0) {
    return { X: 0, Y: 0, Z: 0 };
  }

  // Sort samples by wavelength
  const sorted = [...samples].sort((a, b) => a[0] - b[0]);

  let X = 0, Y = 0, Z = 0;
  const deltaLambda = 5; // Integration step

  // Integrate over visible spectrum
  for (let lambda = 380; lambda <= 780; lambda += deltaLambda) {
    // Interpolate SPD value at this wavelength
    let spdValue = 0;

    if (lambda <= sorted[0][0]) {
      spdValue = sorted[0][1];
    } else if (lambda >= sorted[sorted.length - 1][0]) {
      spdValue = sorted[sorted.length - 1][1];
    } else {
      // Find surrounding samples
      for (let i = 0; i < sorted.length - 1; i++) {
        if (lambda >= sorted[i][0] && lambda <= sorted[i + 1][0]) {
          const t = (lambda - sorted[i][0]) / (sorted[i + 1][0] - sorted[i][0]);
          if (interpolation === 'held') {
            spdValue = sorted[i][1];
          } else {
            spdValue = lerp(sorted[i][1], sorted[i + 1][1], t);
          }
          break;
        }
      }
    }

    // Get color matching functions
    const xBar = interpolateCMF(lambda, CIE_X);
    const yBar = interpolateCMF(lambda, CIE_Y);
    const zBar = interpolateCMF(lambda, CIE_Z);

    // Riemann sum integration
    X += spdValue * xBar * deltaLambda;
    Y += spdValue * yBar * deltaLambda;
    Z += spdValue * zBar * deltaLambda;
  }

  return { X, Y, Z };
}

/**
 * Convert spectral data to RGB
 * @param {Object} spectralEmission - Spectral emission data from RenderLight
 * @returns {Object} {r, g, b} values in 0-1 range, plus {X, Y, Z} for reference
 */
function spectralToRGB(spectralEmission) {
  if (!spectralEmission) {
    return { r: 1, g: 1, b: 1, X: 0.95047, Y: 1.0, Z: 1.08883 };
  }

  let samples = spectralEmission.samples || [];

  // Handle illuminant presets
  if (spectralEmission.preset && spectralEmission.preset !== 'none') {
    samples = generatePresetSPD(spectralEmission.preset);
  }

  if (samples.length === 0) {
    return { r: 1, g: 1, b: 1, X: 0.95047, Y: 1.0, Z: 1.08883 };
  }

  // Convert unit to nanometers if needed
  if (spectralEmission.unit === 'micrometers') {
    samples = samples.map(s => [s[0] * 1000, s[1]]);
  }

  const xyz = spdToXYZ(samples, spectralEmission.interpolation);
  const linear = xyzToLinearRGB(xyz.X, xyz.Y, xyz.Z);

  // Normalize by Y (luminance) to get chromaticity, then scale
  const normFactor = xyz.Y > 0 ? 1.0 / xyz.Y : 1.0;
  const normalizedLinear = {
    r: linear.r * normFactor,
    g: linear.g * normFactor,
    b: linear.b * normFactor
  };

  // Find max component for gamut mapping
  const maxVal = Math.max(normalizedLinear.r, normalizedLinear.g, normalizedLinear.b, 0.0001);
  const scale = maxVal > 1.0 ? 1.0 / maxVal : 1.0;

  return {
    r: Math.max(0, Math.min(1, linearToSRGB(normalizedLinear.r * scale))),
    g: Math.max(0, Math.min(1, linearToSRGB(normalizedLinear.g * scale))),
    b: Math.max(0, Math.min(1, linearToSRGB(normalizedLinear.b * scale))),
    X: xyz.X,
    Y: xyz.Y,
    Z: xyz.Z
  };
}

/**
 * Generate SPD for standard illuminant presets
 */
function generatePresetSPD(preset) {
  const samples = [];

  switch (preset.toLowerCase()) {
    case 'd65':
      for (let i = 0; i < CIE_WAVELENGTHS.length; i++) {
        samples.push([CIE_WAVELENGTHS[i], D65_SPD[i] / 100]); // Normalize
      }
      break;

    case 'a': // Incandescent (2856K blackbody)
      for (let lambda = 380; lambda <= 780; lambda += 5) {
        samples.push([lambda, planckianSPD(lambda, 2856)]);
      }
      break;

    case 'd50': // Daylight 5000K
      for (let lambda = 380; lambda <= 780; lambda += 5) {
        samples.push([lambda, daylightSPD(lambda, 5000)]);
      }
      break;

    case 'e': // Equal energy
      for (let lambda = 380; lambda <= 780; lambda += 5) {
        samples.push([lambda, 1.0]);
      }
      break;

    case 'f1':
    case 'f2':
    case 'f7':
    case 'f11':
      // Fluorescent - approximate with daylight + line emissions
      for (let lambda = 380; lambda <= 780; lambda += 5) {
        let val = daylightSPD(lambda, 4000) * 0.7;
        // Add fluorescent line peaks
        if (Math.abs(lambda - 436) < 10) val += 0.5;
        if (Math.abs(lambda - 546) < 10) val += 0.6;
        if (Math.abs(lambda - 611) < 10) val += 0.3;
        samples.push([lambda, val]);
      }
      break;

    default:
      // Return flat spectrum
      for (let lambda = 380; lambda <= 780; lambda += 5) {
        samples.push([lambda, 1.0]);
      }
  }

  return samples;
}

/**
 * Planckian (blackbody) spectral power distribution
 */
function planckianSPD(wavelength, temperature) {
  const h = 6.62607015e-34; // Planck constant
  const c = 299792458;       // Speed of light
  const k = 1.380649e-23;    // Boltzmann constant

  const lambda = wavelength * 1e-9; // nm to m
  const c1 = 2 * Math.PI * h * c * c;
  const c2 = h * c / k;

  const numerator = c1 / Math.pow(lambda, 5);
  const denominator = Math.exp(c2 / (lambda * temperature)) - 1;

  // Normalize to peak at 1.0
  const peakLambda = 2898000 / temperature; // Wien's displacement in nm
  const peakLambdaM = peakLambda * 1e-9;
  const peakValue = c1 / Math.pow(peakLambdaM, 5) / (Math.exp(c2 / (peakLambdaM * temperature)) - 1);

  return (numerator / denominator) / peakValue;
}

/**
 * Approximate daylight SPD using CIE daylight model
 */
function daylightSPD(wavelength, temperature) {
  // Simplified daylight model
  const xD = temperature <= 7000
    ? 0.244063 + 0.09911 * (1000 / temperature) + 2.9678 * Math.pow(1000 / temperature, 2) - 4.6070 * Math.pow(1000 / temperature, 3)
    : 0.237040 + 0.24748 * (1000 / temperature) + 1.9018 * Math.pow(1000 / temperature, 2) - 2.0064 * Math.pow(1000 / temperature, 3);

  // Base it on D65 shape scaled by temperature
  const idx = Math.round((wavelength - 380) / 5);
  if (idx >= 0 && idx < D65_SPD.length) {
    const tempRatio = 6500 / temperature;
    return (D65_SPD[idx] / 100) * Math.pow(tempRatio, 0.3);
  }
  return 1.0;
}

// ============================================
// Spectral Mode State
// ============================================

let spectralMode = 'rgb';  // 'rgb', 'spectral', 'monochrome'
let monochromeWavelength = 550; // Default to green
let selectedLightIndex = -1;
let spectralCanvas = null;
let spectralCtx = null;

/**
 * Initialize spectral curve canvas
 */
function initSpectralCanvas() {
  spectralCanvas = document.getElementById('spectral-canvas');
  if (spectralCanvas) {
    spectralCtx = spectralCanvas.getContext('2d');
    // Draw default empty state
    drawSpectralCurve(null);
  }
}

/**
 * Draw spectral curve visualization
 * @param {Object} spectralEmission - Spectral emission data or null
 */
function drawSpectralCurve(spectralEmission) {
  if (!spectralCtx || !spectralCanvas) return;

  const width = spectralCanvas.width;
  const height = spectralCanvas.height;
  const padding = { left: 35, right: 10, top: 10, bottom: 25 };
  const plotWidth = width - padding.left - padding.right;
  const plotHeight = height - padding.top - padding.bottom;

  // Clear canvas
  spectralCtx.fillStyle = '#1a1a2e';
  spectralCtx.fillRect(0, 0, width, height);

  // Draw wavelength rainbow background
  drawSpectrumGradient(spectralCtx, padding.left, height - padding.bottom, plotWidth, 8);

  // Draw grid
  spectralCtx.strokeStyle = 'rgba(255, 255, 255, 0.1)';
  spectralCtx.lineWidth = 1;

  // Vertical grid lines (wavelength)
  for (let lambda = 400; lambda <= 700; lambda += 50) {
    const x = padding.left + ((lambda - 380) / 400) * plotWidth;
    spectralCtx.beginPath();
    spectralCtx.moveTo(x, padding.top);
    spectralCtx.lineTo(x, height - padding.bottom);
    spectralCtx.stroke();

    // Wavelength labels
    spectralCtx.fillStyle = '#666';
    spectralCtx.font = '9px monospace';
    spectralCtx.textAlign = 'center';
    spectralCtx.fillText(lambda + '', x, height - 5);
  }

  // Draw axes labels
  spectralCtx.fillStyle = '#888';
  spectralCtx.font = '10px sans-serif';
  spectralCtx.textAlign = 'center';
  spectralCtx.fillText('Wavelength (nm)', padding.left + plotWidth / 2, height - 2);

  spectralCtx.save();
  spectralCtx.translate(8, padding.top + plotHeight / 2);
  spectralCtx.rotate(-Math.PI / 2);
  spectralCtx.fillText('Intensity', 0, 0);
  spectralCtx.restore();

  // Get samples
  let samples = [];
  let maxValue = 1;

  if (spectralEmission) {
    if (spectralEmission.samples && spectralEmission.samples.length > 0) {
      samples = [...spectralEmission.samples];
      if (spectralEmission.unit === 'micrometers') {
        samples = samples.map(s => [s[0] * 1000, s[1]]);
      }
    } else if (spectralEmission.preset && spectralEmission.preset !== 'none') {
      samples = generatePresetSPD(spectralEmission.preset);
    }

    if (samples.length > 0) {
      maxValue = Math.max(...samples.map(s => s[1]), 0.001);
    }
  }

  if (samples.length === 0) {
    // Draw "No spectral data" message
    spectralCtx.fillStyle = '#666';
    spectralCtx.font = '12px sans-serif';
    spectralCtx.textAlign = 'center';
    spectralCtx.fillText('No spectral data', width / 2, height / 2);
    return;
  }

  // Sort samples by wavelength
  samples.sort((a, b) => a[0] - b[0]);

  // Draw filled area under curve
  spectralCtx.beginPath();
  spectralCtx.moveTo(padding.left, height - padding.bottom);

  for (const [lambda, value] of samples) {
    if (lambda >= 380 && lambda <= 780) {
      const x = padding.left + ((lambda - 380) / 400) * plotWidth;
      const y = height - padding.bottom - (value / maxValue) * plotHeight;
      spectralCtx.lineTo(x, y);
    }
  }

  // Close the path
  const lastLambda = samples[samples.length - 1][0];
  const lastX = padding.left + ((Math.min(lastLambda, 780) - 380) / 400) * plotWidth;
  spectralCtx.lineTo(lastX, height - padding.bottom);
  spectralCtx.closePath();

  // Fill with gradient based on spectrum
  const gradient = spectralCtx.createLinearGradient(padding.left, 0, padding.left + plotWidth, 0);
  for (let i = 0; i <= 10; i++) {
    const lambda = 380 + (i / 10) * 400;
    const rgb = wavelengthToRGB(lambda);
    gradient.addColorStop(i / 10, `rgba(${Math.round(rgb.r * 255)}, ${Math.round(rgb.g * 255)}, ${Math.round(rgb.b * 255)}, 0.3)`);
  }
  spectralCtx.fillStyle = gradient;
  spectralCtx.fill();

  // Draw curve line
  spectralCtx.beginPath();
  let firstPoint = true;
  for (const [lambda, value] of samples) {
    if (lambda >= 380 && lambda <= 780) {
      const x = padding.left + ((lambda - 380) / 400) * plotWidth;
      const y = height - padding.bottom - (value / maxValue) * plotHeight;
      if (firstPoint) {
        spectralCtx.moveTo(x, y);
        firstPoint = false;
      } else {
        spectralCtx.lineTo(x, y);
      }
    }
  }
  spectralCtx.strokeStyle = '#ffffff';
  spectralCtx.lineWidth = 2;
  spectralCtx.stroke();

  // Draw sample points
  spectralCtx.fillStyle = '#ffd700';
  for (const [lambda, value] of samples) {
    if (lambda >= 380 && lambda <= 780) {
      const x = padding.left + ((lambda - 380) / 400) * plotWidth;
      const y = height - padding.bottom - (value / maxValue) * plotHeight;
      spectralCtx.beginPath();
      spectralCtx.arc(x, y, 3, 0, Math.PI * 2);
      spectralCtx.fill();
    }
  }

  // Draw monochrome wavelength indicator if in monochrome mode
  if (spectralMode === 'monochrome') {
    const x = padding.left + ((monochromeWavelength - 380) / 400) * plotWidth;
    spectralCtx.strokeStyle = '#ff0000';
    spectralCtx.lineWidth = 2;
    spectralCtx.setLineDash([4, 4]);
    spectralCtx.beginPath();
    spectralCtx.moveTo(x, padding.top);
    spectralCtx.lineTo(x, height - padding.bottom);
    spectralCtx.stroke();
    spectralCtx.setLineDash([]);

    // Draw wavelength label
    const rgb = wavelengthToRGB(monochromeWavelength);
    spectralCtx.fillStyle = `rgb(${Math.round(rgb.r * 255)}, ${Math.round(rgb.g * 255)}, ${Math.round(rgb.b * 255)})`;
    spectralCtx.font = 'bold 11px monospace';
    spectralCtx.textAlign = 'center';
    spectralCtx.fillText(`${monochromeWavelength}nm`, x, padding.top + 12);
  }

  // Draw computed RGB color swatch
  const computedRGB = spectralToRGB(spectralEmission);
  const swatchSize = 20;
  spectralCtx.fillStyle = `rgb(${Math.round(computedRGB.r * 255)}, ${Math.round(computedRGB.g * 255)}, ${Math.round(computedRGB.b * 255)})`;
  spectralCtx.fillRect(width - swatchSize - 5, 5, swatchSize, swatchSize);
  spectralCtx.strokeStyle = '#fff';
  spectralCtx.lineWidth = 1;
  spectralCtx.strokeRect(width - swatchSize - 5, 5, swatchSize, swatchSize);
}

/**
 * Draw spectrum gradient bar
 */
function drawSpectrumGradient(ctx, x, y, width, height) {
  for (let i = 0; i < width; i++) {
    const lambda = 380 + (i / width) * 400;
    const rgb = wavelengthToRGB(lambda);
    ctx.fillStyle = `rgb(${Math.round(rgb.r * 255)}, ${Math.round(rgb.g * 255)}, ${Math.round(rgb.b * 255)})`;
    ctx.fillRect(x + i, y, 1, height);
  }
}

/**
 * Set spectral display mode
 * @param {string} mode - 'rgb', 'spectral', or 'monochrome'
 */
function setSpectralMode(mode) {
  spectralMode = mode;
  console.log(`Spectral mode set to: ${mode}`);

  // Update all lights based on new mode
  updateLightColors();

  // Redraw spectral curve
  if (selectedLightIndex >= 0 && selectedLightIndex < lightData.length) {
    drawSpectralCurve(lightData[selectedLightIndex].spectralEmission);
  }
}

/**
 * Set monochrome wavelength
 * @param {number} wavelength - Wavelength in nanometers (380-780)
 */
function setMonochromeWavelength(wavelength) {
  monochromeWavelength = Math.max(380, Math.min(780, wavelength));
  console.log(`Monochrome wavelength set to: ${monochromeWavelength}nm`);

  // Update light colors if in monochrome mode
  if (spectralMode === 'monochrome') {
    updateLightColors();
  }

  // Redraw spectral curve with new indicator
  if (selectedLightIndex >= 0 && selectedLightIndex < lightData.length) {
    drawSpectralCurve(lightData[selectedLightIndex].spectralEmission);
  }
}

/**
 * Update all light colors based on current spectral mode
 */
function updateLightColors() {
  for (let i = 0; i < threeLights.length; i++) {
    const lightObj = threeLights[i];
    const usdLight = lightData[i];

    if (!lightObj || !usdLight) continue;

    // Find the actual Three.js light
    let light = lightObj;
    if (lightObj.isGroup) {
      light = lightObj.children.find(c => c.isLight);
    }
    if (!light) continue;

    // Calculate color based on mode
    let color;
    switch (spectralMode) {
      case 'monochrome':
        color = wavelengthToRGB(monochromeWavelength);
        break;

      case 'spectral':
        if (usdLight.spectralEmission) {
          color = spectralToRGB(usdLight.spectralEmission);
        } else {
          // Fall back to USD color
          color = {
            r: usdLight.color?.[0] || 1,
            g: usdLight.color?.[1] || 1,
            b: usdLight.color?.[2] || 1
          };
        }
        break;

      case 'rgb':
      default:
        color = {
          r: usdLight.color?.[0] || 1,
          g: usdLight.color?.[1] || 1,
          b: usdLight.color?.[2] || 1
        };
        break;
    }

    // Apply color to light
    if (light.color) {
      light.color.setRGB(color.r, color.g, color.b);
    }

    // Update helper color if exists
    if (lightHelpers[i]) {
      const helper = lightHelpers[i];
      if (helper.material && helper.material.color) {
        helper.material.color.setRGB(color.r, color.g, color.b);
      }
    }
  }
}

/**
 * Select a light for spectral curve display
 * @param {number} index - Light index
 */
function selectLightForSpectral(index) {
  selectedLightIndex = index;
  if (index >= 0 && index < lightData.length) {
    const light = lightData[index];
    drawSpectralCurve(light.spectralEmission);

    // Update spectral info display
    updateSpectralInfo(light);
  } else {
    drawSpectralCurve(null);
  }
}

/**
 * Update spectral info panel
 */
function updateSpectralInfo(light) {
  const infoEl = document.getElementById('spectral-info');
  if (!infoEl) return;

  if (!light.spectralEmission) {
    infoEl.innerHTML = '<span style="color: #666;">No spectral data</span>';
    return;
  }

  const spd = light.spectralEmission;
  const rgb = spectralToRGB(spd);

  let html = '';
  if (spd.preset && spd.preset !== 'none') {
    html += `<div>Preset: <strong>${spd.preset.toUpperCase()}</strong></div>`;
  }
  if (spd.samples && spd.samples.length > 0) {
    html += `<div>Samples: <strong>${spd.samples.length}</strong></div>`;
    const minWl = Math.min(...spd.samples.map(s => s[0]));
    const maxWl = Math.max(...spd.samples.map(s => s[0]));
    html += `<div>Range: <strong>${minWl.toFixed(0)}-${maxWl.toFixed(0)}nm</strong></div>`;
  }
  html += `<div>Interp: <strong>${spd.interpolation || 'linear'}</strong></div>`;
  html += `<div>XYZ: <strong>${rgb.X.toFixed(3)}, ${rgb.Y.toFixed(3)}, ${rgb.Z.toFixed(3)}</strong></div>`;

  infoEl.innerHTML = html;
}

/**
 * Apply demo spectral data to loaded lights
 * This adds synthetic spectral emission data for demonstration purposes
 */
function applyDemoSpectralData() {
  if (lightData.length === 0) {
    console.warn('No lights loaded - cannot apply spectral data');
    return;
  }

  const spectralPresets = [
    // D65 daylight
    { preset: 'd65', interpolation: 'linear', unit: 'nanometers', samples: [] },
    // Tungsten incandescent (Illuminant A)
    { preset: 'a', interpolation: 'linear', unit: 'nanometers', samples: [] },
    // Custom red LED-like spectrum
    {
      preset: 'none',
      interpolation: 'linear',
      unit: 'nanometers',
      samples: [
        [580, 0.05], [600, 0.2], [620, 0.8], [630, 1.0], [640, 0.9],
        [650, 0.7], [660, 0.4], [680, 0.1], [700, 0.02]
      ]
    },
    // Custom blue LED-like spectrum
    {
      preset: 'none',
      interpolation: 'linear',
      unit: 'nanometers',
      samples: [
        [420, 0.1], [440, 0.5], [450, 0.9], [460, 1.0], [470, 0.95],
        [480, 0.7], [490, 0.3], [500, 0.1], [520, 0.02]
      ]
    },
    // Custom green phosphor-like spectrum
    {
      preset: 'none',
      interpolation: 'linear',
      unit: 'nanometers',
      samples: [
        [480, 0.05], [500, 0.2], [520, 0.6], [530, 0.9], [540, 1.0],
        [550, 0.95], [560, 0.8], [570, 0.5], [580, 0.2], [600, 0.05]
      ]
    },
    // Sodium lamp (narrow band)
    {
      preset: 'none',
      interpolation: 'linear',
      unit: 'nanometers',
      samples: [
        [580, 0.1], [585, 0.5], [589, 1.0], [590, 0.95], [595, 0.3], [600, 0.05]
      ]
    }
  ];

  // Apply spectral data to each light
  for (let i = 0; i < lightData.length; i++) {
    const presetIdx = i % spectralPresets.length;
    lightData[i].spectralEmission = { ...spectralPresets[presetIdx] };
  }

  // Auto-select first light if none selected
  if (selectedLightIndex < 0 && lightData.length > 0) {
    selectedLightIndex = 0;
  }

  // Update display
  if (selectedLightIndex >= 0 && selectedLightIndex < lightData.length) {
    drawSpectralCurve(lightData[selectedLightIndex].spectralEmission);
    updateSpectralInfo(lightData[selectedLightIndex]);
  }

  // Update colors if in spectral mode
  if (spectralMode === 'spectral') {
    updateLightColors();
  }

  console.log(`Demo spectral data applied to ${lightData.length} lights`);
}

/**
 * Generate blackbody spectrum for a given color temperature
 * @param {number} temperature - Color temperature in Kelvin
 * @returns {Object} Spectral emission data
 */
function generateBlackbodySpectrum(temperature) {
  const samples = [];
  for (let lambda = 380; lambda <= 780; lambda += 10) {
    samples.push([lambda, planckianSPD(lambda, temperature)]);
  }
  return {
    preset: 'none',
    interpolation: 'linear',
    unit: 'nanometers',
    samples: samples
  };
}

/**
 * Apply blackbody spectrum to selected light
 * @param {number} temperature - Color temperature in Kelvin
 */
function applyBlackbodyToSelected(temperature) {
  if (selectedLightIndex < 0 || selectedLightIndex >= lightData.length) {
    console.warn('No light selected');
    return;
  }

  lightData[selectedLightIndex].spectralEmission = generateBlackbodySpectrum(temperature);
  drawSpectralCurve(lightData[selectedLightIndex].spectralEmission);
  updateSpectralInfo(lightData[selectedLightIndex]);

  if (spectralMode === 'spectral') {
    updateLightColors();
  }

  console.log(`Applied ${temperature}K blackbody spectrum to light ${selectedLightIndex}`);
}

// ============================================
// ACES 2.0 Tone Mapping Shader
// Based on the ACES 2.0 Output Transform
// ============================================

const ACES2ToneMappingShader = {
  name: 'ACES2ToneMappingShader',

  uniforms: {
    'tDiffuse': { value: null },
    'exposure': { value: 1.0 },
    'gamma': { value: 2.2 }
  },

  vertexShader: /* glsl */`
    varying vec2 vUv;
    void main() {
      vUv = uv;
      gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
    }
  `,

  fragmentShader: /* glsl */`
    uniform sampler2D tDiffuse;
    uniform float exposure;
    uniform float gamma;
    varying vec2 vUv;

    // ACES 2.0 matrices
    const mat3 sRGB_to_AP0 = mat3(
      0.4397010, 0.0897923, 0.0175440,
      0.3829780, 0.8134230, 0.1115440,
      0.1773350, 0.0967616, 0.8707040
    );

    const mat3 AP0_to_AP1 = mat3(
       1.4514393161, -0.0765537734,  0.0083161484,
      -0.2365107469,  1.1762296998, -0.0060324498,
      -0.2149285693, -0.0996759264,  0.9977163014
    );

    const mat3 AP1_to_sRGB = mat3(
       1.7050509, -0.1302564, -0.0240033,
      -0.6217921,  1.1408048, -0.1289690,
      -0.0832588, -0.0105485,  1.1529722
    );

    // RRT (Reference Rendering Transform) parameters for ACES 2.0
    vec3 RRTAndODTFit(vec3 v) {
      vec3 a = v * (v + 0.0245786) - 0.000090537;
      vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
      return a / b;
    }

    // Highlight desaturation (part of ACES 2.0)
    vec3 highlightDesaturation(vec3 color) {
      float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
      float saturation = 1.0 - smoothstep(0.5, 4.0, luminance);
      return mix(vec3(luminance), color, saturation);
    }

    // Gamut mapping (simplified)
    vec3 gamutMap(vec3 color) {
      // Soft clip to avoid harsh clipping
      vec3 mapped = color / (color + 1.0);
      return mix(color, mapped, smoothstep(0.8, 1.2, max(max(color.r, color.g), color.b)));
    }

    void main() {
      vec4 texel = texture2D(tDiffuse, vUv);
      vec3 color = texel.rgb * exposure;

      // Convert sRGB to ACES AP0
      color = sRGB_to_AP0 * color;

      // Convert AP0 to AP1 (working space)
      color = AP0_to_AP1 * color;

      // Apply highlight desaturation
      color = highlightDesaturation(color);

      // Apply gamut mapping
      color = gamutMap(color);

      // Apply RRT+ODT curve
      color = RRTAndODTFit(color);

      // Convert back to sRGB
      color = AP1_to_sRGB * color;

      // Clamp to valid range
      color = clamp(color, 0.0, 1.0);

      // Apply gamma
      color = pow(color, vec3(1.0 / gamma));

      gl_FragColor = vec4(color, texel.a);
    }
  `
};

// ============================================
// Embedded USDA Scenes
// ============================================

const EMBEDDED_SCENES = {
  basic: `#usda 1.0
(
    defaultPrim = "World"
    metersPerUnit = 1
    upAxis = "Y"
)

def Xform "World"
{
    def SphereLight "PointLight1"
    {
        float inputs:intensity = 500
        color3f inputs:color = (1, 0.9, 0.8)
        float inputs:radius = 0.1
        double3 xformOp:translate = (3, 4, 2)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }

    def SphereLight "PointLight2"
    {
        float inputs:intensity = 300
        color3f inputs:color = (0.8, 0.9, 1)
        float inputs:radius = 0.05
        double3 xformOp:translate = (-3, 3, -2)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }

    def DistantLight "SunLight"
    {
        float inputs:intensity = 1.5
        color3f inputs:color = (1, 0.98, 0.95)
        float inputs:angle = 0.53
        double3 xformOp:rotateXYZ = (-45, 30, 0)
        uniform token[] xformOpOrder = ["xformOp:rotateXYZ"]
    }
}
`,

  spotlight: `#usda 1.0
(
    defaultPrim = "World"
    metersPerUnit = 1
    upAxis = "Y"
)

def Xform "World"
{
    def SphereLight "SpotLight1"
    {
        float inputs:intensity = 1000
        color3f inputs:color = (1, 0.95, 0.9)
        float inputs:radius = 0.1
        float inputs:shaping:cone:angle = 30
        float inputs:shaping:cone:softness = 0.2
        float inputs:shaping:focus = 0.5
        double3 xformOp:translate = (0, 5, 3)
        double3 xformOp:rotateXYZ = (-60, 0, 0)
        uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateXYZ"]
    }

    def SphereLight "SpotLight2"
    {
        float inputs:intensity = 800
        color3f inputs:color = (0.9, 0.95, 1)
        float inputs:radius = 0.08
        float inputs:shaping:cone:angle = 45
        float inputs:shaping:cone:softness = 0.3
        double3 xformOp:translate = (-3, 4, -2)
        double3 xformOp:rotateXYZ = (-50, 30, 0)
        uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateXYZ"]
    }

    def SphereLight "SpotLight3"
    {
        float inputs:intensity = 600
        color3f inputs:color = (1, 0.8, 0.6)
        float inputs:radius = 0.05
        float inputs:shaping:cone:angle = 20
        float inputs:shaping:cone:softness = 0.1
        float inputs:shaping:focus = 1.0
        double3 xformOp:translate = (4, 3, 0)
        double3 xformOp:rotateXYZ = (-40, -60, 0)
        uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateXYZ"]
    }
}
`,

  area: `#usda 1.0
(
    defaultPrim = "World"
    metersPerUnit = 1
    upAxis = "Y"
)

def Xform "World"
{
    def RectLight "RectLight1"
    {
        float inputs:intensity = 15
        color3f inputs:color = (1, 1, 1)
        float inputs:width = 2
        float inputs:height = 1.5
        double3 xformOp:translate = (0, 4, 3)
        double3 xformOp:rotateXYZ = (-45, 0, 0)
        uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateXYZ"]
    }

    def DiskLight "DiskLight1"
    {
        float inputs:intensity = 400
        color3f inputs:color = (1, 0.9, 0.8)
        float inputs:radius = 0.5
        double3 xformOp:translate = (-3, 3, 0)
        double3 xformOp:rotateXYZ = (0, 90, 0)
        uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateXYZ"]
    }

    def CylinderLight "CylinderLight1"
    {
        float inputs:intensity = 200
        color3f inputs:color = (0.8, 0.9, 1)
        float inputs:radius = 0.1
        float inputs:length = 3
        double3 xformOp:translate = (3, 2, -2)
        double3 xformOp:rotateXYZ = (0, 0, 45)
        uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateXYZ"]
    }
}
`,

  dome: `#usda 1.0
(
    defaultPrim = "World"
    metersPerUnit = 1
    upAxis = "Y"
)

def Xform "World"
{
    def DomeLight "EnvironmentLight"
    {
        float inputs:intensity = 1.0
        color3f inputs:color = (0.8, 0.85, 1)
        float inputs:exposure = 0
    }

    def SphereLight "FillLight"
    {
        float inputs:intensity = 100
        color3f inputs:color = (1, 0.95, 0.9)
        float inputs:radius = 0.2
        double3 xformOp:translate = (2, 3, 2)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }
}
`,

  complete: `#usda 1.0
(
    defaultPrim = "World"
    metersPerUnit = 1
    upAxis = "Y"
    doc = "Complete lighting scene with multiple light types"
)

def Xform "World"
{
    def DistantLight "Sun"
    {
        float inputs:intensity = 2.0
        color3f inputs:color = (1, 0.98, 0.95)
        float inputs:angle = 0.53
        bool inputs:shadow:enable = true
        double3 xformOp:rotateXYZ = (-50, 35, 0)
        uniform token[] xformOpOrder = ["xformOp:rotateXYZ"]
    }

    def SphereLight "KeyLight"
    {
        float inputs:intensity = 800
        color3f inputs:color = (1, 0.95, 0.9)
        float inputs:radius = 0.15
        float inputs:shaping:cone:angle = 60
        float inputs:shaping:cone:softness = 0.25
        double3 xformOp:translate = (3, 5, 4)
        double3 xformOp:rotateXYZ = (-40, 35, 0)
        uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateXYZ"]
    }

    def SphereLight "FillLight"
    {
        float inputs:intensity = 200
        color3f inputs:color = (0.85, 0.9, 1)
        float inputs:radius = 0.3
        double3 xformOp:translate = (-4, 3, 2)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }

    def SphereLight "RimLight"
    {
        float inputs:intensity = 400
        color3f inputs:color = (1, 0.9, 0.8)
        float inputs:radius = 0.1
        float inputs:shaping:cone:angle = 45
        float inputs:shaping:cone:softness = 0.15
        double3 xformOp:translate = (-2, 4, -4)
        double3 xformOp:rotateXYZ = (-30, -145, 0)
        uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateXYZ"]
    }

    def RectLight "SoftBox"
    {
        float inputs:intensity = 10
        color3f inputs:color = (1, 1, 1)
        float inputs:width = 2
        float inputs:height = 2
        double3 xformOp:translate = (10, 10, 0)
        double3 xformOp:rotateXYZ = (-45, 90, 0)
        uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateXYZ"]
    }

    def DiskLight "AccentLight"
    {
        float inputs:intensity = 150
        color3f inputs:color = (0.7, 0.8, 1)
        float inputs:radius = 0.4
        double3 xformOp:translate = (5, 2, 0)
        double3 xformOp:rotateXYZ = (0, -90, 0)
        uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateXYZ"]
    }
}
`
};

// ============================================
// Scene Setup
// ============================================

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x1a1a2e);

// Camera
const camera = new THREE.PerspectiveCamera(
  60,
  window.innerWidth / window.innerHeight,
  0.1,
  1000
);
camera.position.set(8, 6, 10);
camera.lookAt(0, 0, 0);

// Renderer
const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setSize(window.innerWidth, window.innerHeight);
renderer.setPixelRatio(window.devicePixelRatio);
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.0;
renderer.outputColorSpace = THREE.SRGBColorSpace;
document.body.appendChild(renderer.domElement);

// ============================================
// Tone Mapping Setup
// ============================================

// Current tone mapping mode
let currentToneMapping = 'aces1';
let usePostProcessing = false;
let currentGamma = 2.2;

// Effect Composer for custom tone mapping (ACES 2.0)
const composer = new EffectComposer(renderer);
const renderPass = new RenderPass(scene, camera);
composer.addPass(renderPass);

// ACES 2.0 shader pass
const aces2Pass = new ShaderPass(ACES2ToneMappingShader);
aces2Pass.uniforms.exposure.value = 1.0;
aces2Pass.uniforms.gamma.value = 2.2;
composer.addPass(aces2Pass);

// Output pass for proper color space handling
const outputPass = new OutputPass();
composer.addPass(outputPass);

/**
 * Set tone mapping mode
 * @param {string} mode - 'raw', 'reinhard', 'aces1', 'aces2', 'agx', 'neutral'
 */
function setToneMapping(mode) {
  currentToneMapping = mode;

  switch (mode) {
    case 'raw':
      usePostProcessing = false;
      renderer.toneMapping = THREE.NoToneMapping;
      break;

    case 'reinhard':
      usePostProcessing = false;
      renderer.toneMapping = THREE.ReinhardToneMapping;
      break;

    case 'aces1':
      usePostProcessing = false;
      renderer.toneMapping = THREE.ACESFilmicToneMapping;
      break;

    case 'aces2':
      // Use custom ACES 2.0 via post-processing
      usePostProcessing = true;
      renderer.toneMapping = THREE.NoToneMapping;
      break;

    case 'agx':
      usePostProcessing = false;
      // AgX tone mapping (Three.js r152+)
      renderer.toneMapping = THREE.AgXToneMapping;
      break;

    case 'neutral':
      usePostProcessing = false;
      // Neutral tone mapping (Three.js r164+)
      renderer.toneMapping = THREE.NeutralToneMapping;
      break;

    default:
      usePostProcessing = false;
      renderer.toneMapping = THREE.ACESFilmicToneMapping;
  }

  console.log(`Tone mapping set to: ${mode}, usePostProcessing: ${usePostProcessing}`);
}

/**
 * Set exposure (EV stops)
 * @param {number} ev - Exposure value in stops (-3 to +3)
 */
function setExposure(ev) {
  const exposureMultiplier = Math.pow(2, ev);
  renderer.toneMappingExposure = exposureMultiplier;
  aces2Pass.uniforms.exposure.value = exposureMultiplier;
  console.log(`Exposure set to: ${ev} EV (multiplier: ${exposureMultiplier.toFixed(3)})`);
}

/**
 * Set gamma correction value
 * @param {number} gamma - Gamma value (typically 2.2)
 */
function setGamma(gamma) {
  currentGamma = gamma;
  aces2Pass.uniforms.gamma.value = gamma;
  console.log(`Gamma set to: ${gamma}`);
}

// Initialize RectAreaLight support
RectAreaLightUniformsLib.init();

// Orbit controls
const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
controls.dampingFactor = 0.05;
controls.minDistance = 2;
controls.maxDistance = 50;

// ============================================
// HDRI Position Locator
// ============================================

// HDRI locator mesh and transform controls
let hdriLocator = null;
let hdriLocatorTransformControls = null;
let hdriLocatorVisible = false;

/**
 * Create the HDRI position locator mesh
 */
function createHDRILocator() {
  if (hdriLocator) return hdriLocator;

  // Create a group to hold the locator visual elements
  hdriLocator = new THREE.Group();
  hdriLocator.name = 'HDRILocator';

  // Central sphere (position indicator)
  const sphereGeom = new THREE.SphereGeometry(0.15, 16, 16);
  const sphereMat = new THREE.MeshBasicMaterial({
    color: 0x00ffff,
    transparent: true,
    opacity: 0.8,
    depthTest: false
  });
  const sphere = new THREE.Mesh(sphereGeom, sphereMat);
  sphere.renderOrder = 999;
  hdriLocator.add(sphere);

  // Create axes helper showing projection directions
  const axesSize = 0.5;
  const axesHelper = new THREE.AxesHelper(axesSize);
  axesHelper.renderOrder = 999;
  hdriLocator.add(axesHelper);

  // Create small wireframe sphere showing approximate projection range
  const rangeGeom = new THREE.SphereGeometry(1, 16, 8);
  const rangeMat = new THREE.MeshBasicMaterial({
    color: 0x00ffff,
    wireframe: true,
    transparent: true,
    opacity: 0.2,
    depthTest: false
  });
  const rangeIndicator = new THREE.Mesh(rangeGeom, rangeMat);
  rangeIndicator.name = 'RangeIndicator';
  rangeIndicator.renderOrder = 998;
  hdriLocator.add(rangeIndicator);

  // Set initial position from hdriSettings
  hdriLocator.position.set(
    hdriSettings.center.x,
    hdriSettings.center.y,
    hdriSettings.center.z
  );

  // Create TransformControls
  hdriLocatorTransformControls = new TransformControls(camera, renderer.domElement);
  hdriLocatorTransformControls.setMode('translate');
  hdriLocatorTransformControls.setSize(1.0);

  // Disable orbit controls while dragging the locator
  hdriLocatorTransformControls.addEventListener('dragging-changed', (event) => {
    controls.enabled = !event.value;
  });

  // Update hdriSettings.center when locator is moved
  hdriLocatorTransformControls.addEventListener('objectChange', () => {
    if (hdriLocator) {
      hdriSettings.center.x = hdriLocator.position.x;
      hdriSettings.center.y = hdriLocator.position.y;
      hdriSettings.center.z = hdriLocator.position.z;

      // Update UI position inputs if they exist
      if (window.updateHDRIPositionUI) {
        window.updateHDRIPositionUI(hdriSettings.center);
      }

      // Schedule HDRI refresh if live update is enabled
      scheduleHDRIRefresh();
    }
  });

  // Add TransformControls to scene - handle both old and new API
  if (hdriLocatorTransformControls.isObject3D) {
    scene.add(hdriLocatorTransformControls);
    configureTransformControlsForOverlay(hdriLocatorTransformControls);
  } else if (typeof hdriLocatorTransformControls.getHelper === 'function') {
    const helper = hdriLocatorTransformControls.getHelper();
    scene.add(helper);
    configureTransformControlsForOverlay(helper);
  }
  // If neither, new API handles its own rendering

  // Add locator to scene
  scene.add(hdriLocator);

  // Attach after adding to scene
  hdriLocatorTransformControls.attach(hdriLocator);

  // Initially hidden - use enabled property and detach
  hdriLocator.visible = false;
  hdriLocatorTransformControls.detach();
  hdriLocatorTransformControls.enabled = false;

  return hdriLocator;
}

/**
 * Show/hide the HDRI position locator
 */
function toggleHDRILocator(visible) {
  // Create locator if it doesn't exist
  if (!hdriLocator) {
    createHDRILocator();
  }

  if (visible === undefined) {
    hdriLocatorVisible = !hdriLocatorVisible;
  } else {
    hdriLocatorVisible = visible;
  }

  hdriLocator.visible = hdriLocatorVisible;

  if (hdriLocatorVisible) {
    // Show gizmo - attach and enable
    hdriLocatorTransformControls.attach(hdriLocator);
    hdriLocatorTransformControls.enabled = true;
  } else {
    // Hide gizmo - detach and disable
    hdriLocatorTransformControls.detach();
    hdriLocatorTransformControls.enabled = false;
  }

  return hdriLocatorVisible;
}

/**
 * Set the HDRI locator position
 */
function setHDRILocatorPosition(x, y, z) {
  // Update settings
  hdriSettings.center.x = x;
  hdriSettings.center.y = y;
  hdriSettings.center.z = z;

  // Update locator if it exists
  if (hdriLocator) {
    hdriLocator.position.set(x, y, z);
  }

  // Update UI if callback exists
  if (window.updateHDRIPositionUI) {
    window.updateHDRIPositionUI({ x, y, z });
  }

  return hdriSettings.center;
}

/**
 * Get the current HDRI center position
 */
function getHDRICenter() {
  return { ...hdriSettings.center };
}

/**
 * Update range indicator size based on maxDistance
 */
function updateHDRIRangeIndicator(distance) {
  if (hdriLocator) {
    const rangeIndicator = hdriLocator.getObjectByName('RangeIndicator');
    if (rangeIndicator) {
      const scale = Math.min(distance * 0.1, 5); // Cap visual scale
      rangeIndicator.scale.set(scale, scale, scale);
    }
  }
}

// ============================================
// Scene Objects
// ============================================

// Ground plane
const groundGeometry = new THREE.PlaneGeometry(20, 20);
const groundMaterial = new THREE.MeshStandardMaterial({
  color: 0x333344,
  roughness: 0.9,
  metalness: 0.1
});
const ground = new THREE.Mesh(groundGeometry, groundMaterial);
ground.rotation.x = -Math.PI / 2;
ground.position.y = -1;
ground.receiveShadow = true;
scene.add(ground);

// Grid helper
const gridHelper = new THREE.GridHelper(20, 20, 0x444455, 0x333344);
gridHelper.position.y = -0.99;
scene.add(gridHelper);

// Central sphere with MeshPhysicalMaterial
const sphereGeometry = new THREE.SphereGeometry(1, 64, 64);
const sphereMaterial = new THREE.MeshPhysicalMaterial({
  color: 0xffffff,
  metalness: 0.1,
  roughness: 0.2,
  clearcoat: 0.3,
  clearcoatRoughness: 0.2,
  reflectivity: 0.5,
  ior: 1.5
});
const centralSphere = new THREE.Mesh(sphereGeometry, sphereMaterial);
centralSphere.castShadow = true;
centralSphere.receiveShadow = true;
centralSphere.name = 'CentralSphere';
scene.add(centralSphere);

// Additional geometry for better light visualization
const torusGeometry = new THREE.TorusKnotGeometry(0.3, 0.1, 100, 16);
const torusMaterial = new THREE.MeshPhysicalMaterial({
  color: 0x6688cc,
  metalness: 0.6,
  roughness: 0.3
});
const torus = new THREE.Mesh(torusGeometry, torusMaterial);
torus.position.set(2, 0, 0);
torus.castShadow = true;
torus.receiveShadow = true;
torus.name = 'Torus';
scene.add(torus);

const boxGeometry = new THREE.BoxGeometry(0.8, 0.8, 0.8);
const boxMaterial = new THREE.MeshPhysicalMaterial({
  color: 0xcc8866,
  metalness: 0.2,
  roughness: 0.5
});
const box = new THREE.Mesh(boxGeometry, boxMaterial);
box.position.set(-2, -0.6, 0);
box.rotation.y = Math.PI / 4;
box.castShadow = true;
box.receiveShadow = true;
box.name = 'Box';
scene.add(box);

// Array of selectable mesh objects
const selectableMeshes = [centralSphere, torus, box];
// Add ground as selectable but not transformable
ground.name = 'Ground';

// Ambient light (low intensity as fallback)
const ambientLight = new THREE.AmbientLight(0x404050, 0.3);
scene.add(ambientLight);

// ============================================
// Scene Object Visibility
// ============================================

/**
 * Set visibility of a scene object
 * @param {string} objectName - Name of the object ('ground', 'grid', 'sphere', 'torus', 'box', 'helpers')
 * @param {boolean} visible - Visibility state
 */
function setObjectVisible(objectName, visible) {
  switch (objectName) {
    case 'ground':
      ground.visible = visible;
      break;
    case 'grid':
      gridHelper.visible = visible;
      break;
    case 'sphere':
      centralSphere.visible = visible;
      break;
    case 'torus':
      torus.visible = visible;
      break;
    case 'box':
      box.visible = visible;
      break;
    case 'helpers':
      // Toggle all light helpers
      for (const helper of lightHelpers) {
        helper.visible = visible;
      }
      break;
    case 'ambient':
      ambientLight.visible = visible;
      break;
    default:
      console.warn(`Unknown object: ${objectName}`);
  }
}

/**
 * Get visibility state of a scene object
 * @param {string} objectName - Name of the object
 * @returns {boolean} Visibility state
 */
function getObjectVisible(objectName) {
  switch (objectName) {
    case 'ground': return ground.visible;
    case 'grid': return gridHelper.visible;
    case 'sphere': return centralSphere.visible;
    case 'torus': return torus.visible;
    case 'box': return box.visible;
    case 'helpers': return lightHelpers.length > 0 ? lightHelpers[0].visible : true;
    case 'ambient': return ambientLight.visible;
    default: return true;
  }
}

/**
 * Set visibility of all mesh objects at once
 * @param {boolean} visible - Visibility state
 */
function setAllMeshesVisible(visible) {
  ground.visible = visible;
  centralSphere.visible = visible;
  torus.visible = visible;
  box.visible = visible;
}

// Expose to window for UI
window.setObjectVisible = setObjectVisible;
window.getObjectVisible = getObjectVisible;
window.setAllMeshesVisible = setAllMeshesVisible;

// ============================================
// Light Management
// ============================================

// Store created Three.js lights and helpers
const threeLights = [];
const lightHelpers = [];
const lightData = []; // Store RenderLight data

// ============================================
// Selection Mode and Transform Controls
// ============================================

// Selection mode: 'all', 'lights', 'meshes'
let selectionMode = 'all';

// Light selection
let selectedLight3DIndex = -1;
let lightTransformControls = null;

// Mesh selection
let selectedMesh = null;
let meshTransformControls = null;

const raycaster = new THREE.Raycaster();
const mouse = new THREE.Vector2();

/**
 * Configure TransformControls to render on top of scene geometry
 * This fixes clipping issues by disabling depth test and setting high render order
 * @param {TransformControls} transformControls - The transform controls to configure
 */
function configureTransformControlsForOverlay(transformControls) {
  if (!transformControls) return;

  // Configure immediately and also after first render
  const configure = () => {
    // Check if traverse method exists (it should on Object3D)
    if (typeof transformControls.traverse !== 'function') {
      // Fallback: try to access gizmo children directly
      const gizmo = transformControls.getHelper?.() || transformControls._gizmo || transformControls.gizmo;
      if (gizmo && typeof gizmo.traverse === 'function') {
        gizmo.traverse((child) => {
          if (child.isMesh || child.isLine) {
            child.renderOrder = 1000;
            if (child.material) {
              const materials = Array.isArray(child.material) ? child.material : [child.material];
              materials.forEach((mat) => {
                mat.depthTest = false;
                mat.depthWrite = false;
                mat.transparent = true;
              });
            }
          }
        });
      }
      return;
    }

    transformControls.traverse((child) => {
      if (child.isMesh || child.isLine) {
        child.renderOrder = 1000;
        if (child.material) {
          // Handle both single materials and material arrays
          const materials = Array.isArray(child.material) ? child.material : [child.material];
          materials.forEach((mat) => {
            mat.depthTest = false;
            mat.depthWrite = false;
            mat.transparent = true;
          });
        }
      }
    });
  };

  // Configure immediately
  configure();

  // Also configure after next frame in case gizmo is created asynchronously
  requestAnimationFrame(configure);
}

/**
 * Initialize light transform controls
 */
// Store reference to the helper if using new API
let lightTransformHelper = null;

function initLightTransformControls() {
  if (lightTransformControls) {
    // Already initialized
    return;
  }

  console.log('Initializing light transform controls...');

  lightTransformControls = new TransformControls(camera, renderer.domElement);
  console.log('Created lightTransformControls:', lightTransformControls);
  console.log('Is Object3D?', lightTransformControls.isObject3D);

  lightTransformControls.setSize(1.0);
  lightTransformControls.setSpace('world');

  // Disable orbit controls while dragging
  lightTransformControls.addEventListener('dragging-changed', (event) => {
    controls.enabled = !event.value;
  });

  // Update light data when transformed
  lightTransformControls.addEventListener('objectChange', () => {
    if (selectedLight3DIndex >= 0 && selectedLight3DIndex < threeLights.length) {
      updateLightDataFromTransform(selectedLight3DIndex);
    }
  });

  // Add to scene - handle both old and new TransformControls API
  if (lightTransformControls.isObject3D) {
    // Old API: TransformControls is an Object3D
    scene.add(lightTransformControls);
    lightTransformHelper = lightTransformControls;
    configureTransformControlsForOverlay(lightTransformControls);
  } else if (typeof lightTransformControls.getHelper === 'function') {
    // New API: use getHelper() to get the visual gizmo
    lightTransformHelper = lightTransformControls.getHelper();
    scene.add(lightTransformHelper);
    configureTransformControlsForOverlay(lightTransformHelper);
  } else {
    // Very new API or unknown - TransformControls manages its own rendering
    console.log('TransformControls does not need to be added to scene (new API)');
    lightTransformHelper = null;
  }

  console.log('Light transform controls initialized');
}

// ============================================
// Mesh Selection and Transform Controls
// ============================================

let meshTransformHelper = null;

/**
 * Initialize mesh transform controls
 */
function initMeshTransformControls() {
  if (meshTransformControls) {
    return;
  }

  console.log('Initializing mesh transform controls...');

  meshTransformControls = new TransformControls(camera, renderer.domElement);
  meshTransformControls.setSize(1.0);
  meshTransformControls.setSpace('world');

  // Disable orbit controls while dragging
  meshTransformControls.addEventListener('dragging-changed', (event) => {
    controls.enabled = !event.value;
  });

  // Add to scene - handle both old and new TransformControls API
  if (meshTransformControls.isObject3D) {
    scene.add(meshTransformControls);
    meshTransformHelper = meshTransformControls;
    configureTransformControlsForOverlay(meshTransformControls);
  } else if (typeof meshTransformControls.getHelper === 'function') {
    meshTransformHelper = meshTransformControls.getHelper();
    scene.add(meshTransformHelper);
    configureTransformControlsForOverlay(meshTransformHelper);
  } else {
    console.log('Mesh TransformControls does not need to be added to scene (new API)');
    meshTransformHelper = null;
  }

  console.log('Mesh transform controls initialized');
}

/**
 * Select a mesh object for transformation
 */
function selectMesh(mesh) {
  // Deselect previous mesh
  if (selectedMesh) {
    deselectMesh();
  }

  // Deselect any selected light
  if (selectedLight3DIndex >= 0) {
    selectLight3D(-1);
  }

  if (!mesh) {
    return;
  }

  selectedMesh = mesh;

  // Initialize transform controls if needed
  initMeshTransformControls();

  // Attach transform controls
  meshTransformControls.attach(mesh);
  meshTransformControls.enabled = true;
  meshTransformControls.visible = true;
  meshTransformControls.setMode('translate');

  // Add selection highlight
  if (mesh.material) {
    mesh.userData.originalEmissive = mesh.material.emissive?.clone();
    mesh.userData.originalEmissiveIntensity = mesh.material.emissiveIntensity || 0;
    if (mesh.material.emissive) {
      mesh.material.emissive.setHex(0x333333);
      mesh.material.emissiveIntensity = 0.3;
    }
  }

  // Update UI
  if (window.showMeshProperties) {
    window.showMeshProperties(mesh);
  }

  console.log(`Selected mesh: ${mesh.name || 'unnamed'}`);
}

/**
 * Deselect currently selected mesh
 */
function deselectMesh() {
  if (!selectedMesh) return;

  // Remove selection highlight
  if (selectedMesh.material && selectedMesh.userData.originalEmissive !== undefined) {
    selectedMesh.material.emissive?.copy(selectedMesh.userData.originalEmissive);
    selectedMesh.material.emissiveIntensity = selectedMesh.userData.originalEmissiveIntensity || 0;
    delete selectedMesh.userData.originalEmissive;
    delete selectedMesh.userData.originalEmissiveIntensity;
  }

  // Detach transform controls
  if (meshTransformControls) {
    meshTransformControls.detach();
  }

  selectedMesh = null;

  // Hide mesh properties UI
  if (window.hideMeshProperties) {
    window.hideMeshProperties();
  }

  console.log('Mesh deselected');
}

/**
 * Set mesh transform mode
 */
function setMeshTransformMode(mode) {
  if (!meshTransformControls || !selectedMesh) {
    return;
  }

  meshTransformControls.setMode(mode);
  meshTransformControls.enabled = true;
  meshTransformControls.visible = true;

  console.log(`Mesh transform mode set to: ${mode}`);
}

/**
 * Set selection mode
 */
function setSelectionMode(mode) {
  if (mode !== 'all' && mode !== 'lights' && mode !== 'meshes') {
    console.warn(`Invalid selection mode: ${mode}`);
    return;
  }

  selectionMode = mode;

  // Deselect based on new mode
  if (mode === 'lights' && selectedMesh) {
    deselectMesh();
  } else if (mode === 'meshes' && selectedLight3DIndex >= 0) {
    selectLight3D(-1);
  }

  // Update UI
  if (window.updateSelectionModeUI) {
    window.updateSelectionModeUI(mode);
  }

  console.log(`Selection mode set to: ${mode}`);
}

/**
 * Update lightData from the transformed Three.js light
 */
function updateLightDataFromTransform(lightIndex) {
  const threeLight = threeLights[lightIndex];
  const usdLight = lightData[lightIndex];
  if (!threeLight || !usdLight) return;

  // Get the actual light (may be inside a group)
  const actualLight = getActualLight(threeLight);

  // Get world position and quaternion from the actual light
  const position = new THREE.Vector3();
  const quaternion = new THREE.Quaternion();
  const scale = new THREE.Vector3(1, 1, 1);

  if (actualLight) {
    actualLight.getWorldPosition(position);
    actualLight.getWorldQuaternion(quaternion);
  } else if (threeLight.isGroup) {
    threeLight.getWorldPosition(position);
    threeLight.getWorldQuaternion(quaternion);
  } else {
    position.copy(threeLight.position);
    quaternion.copy(threeLight.quaternion);
  }

  // Update position in lightData
  usdLight.position = [position.x, position.y, position.z];

  // Build new transform matrix
  const matrix = new THREE.Matrix4();
  matrix.compose(position, quaternion, scale);
  usdLight.transform = matrix.toArray();

  // Sync helper position
  const helper = lightHelpers[lightIndex];
  if (helper) {
    if (actualLight && actualLight.isSpotLight) {
      // Update the sphere mesh position (it's at world coordinates)
      helper.traverse((child) => {
        if (child.isMesh && child.geometry.type === 'SphereGeometry') {
          child.position.copy(position);
        }
      });
      // SpotLightHelper updates itself automatically
    } else {
      // For other lights, transform the whole helper group
      helper.position.copy(position);
      helper.quaternion.copy(quaternion);
    }
  }

  // Update UI if callback exists
  if (window.updateLightListItem) {
    window.updateLightListItem(lightIndex, usdLight);
  }

  // Schedule HDRI refresh if live update is enabled
  scheduleHDRIRefresh();
}

/**
 * Select a light in 3D scene for editing
 */
function selectLight3D(lightIndex) {
  // Deselect previous
  if (selectedLight3DIndex >= 0 && lightHelpers[selectedLight3DIndex]) {
    setHelperSelected(lightHelpers[selectedLight3DIndex], false);
  }

  selectedLight3DIndex = lightIndex;

  if (lightIndex < 0 || lightIndex >= threeLights.length) {
    // Deselect
    if (lightTransformControls) {
      lightTransformControls.detach();
    }
    selectedLight3DIndex = -1;
    // Hide light properties panel
    if (window.hideLightProperties) {
      window.hideLightProperties();
    }
    return;
  }

  // Initialize transform controls if needed
  initLightTransformControls();

  const threeLight = threeLights[lightIndex];
  const usdLight = lightData[lightIndex];
  const helper = lightHelpers[lightIndex];

  // Highlight selected helper
  if (helper) {
    setHelperSelected(helper, true);
  }

  // Ensure the light has updated world matrix
  threeLight.updateMatrixWorld(true);

  // Find the actual light object to attach gizmo to
  // For groups containing lights, attach to the actual light so gizmo appears at light position
  let attachTarget = threeLight;
  if (threeLight.isGroup) {
    const actualLight = threeLight.children.find(c => c.isLight);
    if (actualLight) {
      attachTarget = actualLight;
    }
  }

  // Attach transform controls to the actual light (not the group)
  lightTransformControls.attach(attachTarget);
  lightTransformControls.enabled = true;
  lightTransformControls.visible = true;

  // Set transform mode based on light type
  const lightType = usdLight?.type || 'point';
  if (lightType === 'distant' || lightType === 'dome') {
    // Infinite lights - rotate only
    lightTransformControls.setMode('rotate');
  } else {
    // Finite lights - translate by default
    lightTransformControls.setMode('translate');
  }
  lightTransformControls.showX = true;
  lightTransformControls.showY = true;
  lightTransformControls.showZ = true;

  // Reconfigure overlay rendering after attach (gizmo elements may be created on first use)
  configureTransformControlsForOverlay(lightTransformControls);

  console.log(`Transform controls attached to light ${lightIndex}:`, {
    mode: lightTransformControls.mode,
    enabled: lightTransformControls.enabled,
    visible: lightTransformControls.visible,
    objectType: threeLight?.type || threeLight?.constructor?.name,
    objectPosition: threeLight.position ? [threeLight.position.x, threeLight.position.y, threeLight.position.z] : null
  });

  // Also select in spectral view
  selectLightForSpectral(lightIndex);

  // Show light properties panel with current light data
  if (window.showLightProperties) {
    window.showLightProperties(usdLight);
  }

  console.log(`Selected light ${lightIndex}: ${usdLight?.name || usdLight?.type || 'unnamed'}`);
}

/**
 * Set light transform mode
 */
function setLightTransformMode(mode) {
  if (!lightTransformControls) {
    console.warn('Light transform controls not initialized');
    return;
  }
  if (selectedLight3DIndex < 0) {
    console.warn('No light selected');
    return;
  }

  // Ensure controls are attached to the selected light
  const threeLight = threeLights[selectedLight3DIndex];
  if (threeLight && lightTransformControls.object !== threeLight) {
    lightTransformControls.attach(threeLight);
  }

  lightTransformControls.setMode(mode);
  lightTransformControls.enabled = true;
  lightTransformControls.visible = true;

  console.log(`Transform mode set to: ${mode}, attached to:`, lightTransformControls.object);
}

/**
 * Set helper visual selected state
 */
function setHelperSelected(helper, selected) {
  helper.traverse((child) => {
    if (child.material) {
      if (selected) {
        child.material._originalColor = child.material.color?.clone();
        child.material.color?.setHex(0x00ffff);
        child.material._originalOpacity = child.material.opacity;
        child.material.opacity = Math.min(1, (child.material.opacity || 0.5) + 0.3);
      } else if (child.material._originalColor) {
        child.material.color?.copy(child.material._originalColor);
        child.material.opacity = child.material._originalOpacity || 0.5;
      }
    }
  });
}

/**
 * Handle click on 3D canvas to select lights or meshes
 */
function onCanvasClick(event) {
  // Ignore if transform controls is being used
  if (lightTransformControls && lightTransformControls.dragging) return;
  if (meshTransformControls && meshTransformControls.dragging) return;

  // Calculate mouse position in normalized device coordinates
  const rect = renderer.domElement.getBoundingClientRect();
  mouse.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
  mouse.y = -((event.clientY - rect.top) / rect.height) * 2 + 1;

  // Update raycaster
  raycaster.setFromCamera(mouse, camera);

  // Check for light selection (if not in meshes-only mode)
  if (selectionMode !== 'meshes') {
    const lightIntersects = raycaster.intersectObjects(lightHelpers, true);

    if (lightIntersects.length > 0) {
      // Find the light helper group
      let helperGroup = lightIntersects[0].object;
      while (helperGroup && !helperGroup.userData.isLightHelper) {
        helperGroup = helperGroup.parent;
      }

      if (helperGroup && helperGroup.userData.lightIndex !== undefined) {
        selectLight3D(helperGroup.userData.lightIndex);

        // Also update UI selection
        if (window.highlightLightInList) {
          window.highlightLightInList(helperGroup.userData.lightIndex);
        }
        return;
      }
    }
  }

  // Check for mesh selection (if not in lights-only mode)
  if (selectionMode !== 'lights') {
    const meshIntersects = raycaster.intersectObjects(selectableMeshes, false);

    if (meshIntersects.length > 0) {
      const mesh = meshIntersects[0].object;
      selectMesh(mesh);
      return;
    }
  }

  // Click on empty space - deselect all
  if (selectedLight3DIndex >= 0) {
    selectLight3D(-1);
  }
  if (selectedMesh) {
    deselectMesh();
  }
}

// Add click event listener to canvas
renderer.domElement.addEventListener('click', onCanvasClick);

// Add keyboard shortcuts for transform modes
document.addEventListener('keydown', (event) => {
  // Don't handle if typing in an input field
  if (event.target.tagName === 'INPUT' || event.target.tagName === 'TEXTAREA') return;

  const key = event.key.toLowerCase();

  // Selection mode shortcuts
  if (key === 'm') {
    const newMode = selectionMode === 'meshes' ? 'all' : 'meshes';
    setSelectionMode(newMode);
    console.log(`Selection mode: ${newMode}`);
    if (window.updateSelectionModeUI) window.updateSelectionModeUI(newMode);
    return;
  }
  if (key === 'l') {
    const newMode = selectionMode === 'lights' ? 'all' : 'lights';
    setSelectionMode(newMode);
    console.log(`Selection mode: ${newMode}`);
    if (window.updateSelectionModeUI) window.updateSelectionModeUI(newMode);
    return;
  }

  // Handle transform mode shortcuts for lights
  if (key === 'w' || key === 'e' || key === 'r' || key === 'escape') {
    console.log(`Key pressed: '${key}', selectedLight3DIndex: ${selectedLight3DIndex}, selectedMesh: ${selectedMesh?.name}`);
  }

  // Handle mesh transform shortcuts
  if (selectedMesh) {
    switch (key) {
      case 'w': // Translate (move)
        setMeshTransformMode('translate');
        return;
      case 'e': // Rotate
        setMeshTransformMode('rotate');
        return;
      case 'r': // Scale
        setMeshTransformMode('scale');
        return;
      case 'escape':
        deselectMesh();
        return;
    }
  }

  // Handle light transform shortcuts
  if (selectedLight3DIndex < 0) {
    if (key === 'w' || key === 'e' || key === 'r') {
      console.log('No light or mesh selected - click on an object first');
    }
    return;
  }

  switch (key) {
    case 'w': // Translate (move)
      setLightTransformMode('translate');
      break;
    case 'e': // Rotate
      setLightTransformMode('rotate');
      break;
    case 'r': // Scale (for area lights)
      setLightTransformMode('scale');
      break;
    case 'escape':
      selectLight3D(-1);
      break;
  }
});

/**
 * Clear all USD lights from the scene
 */
function clearLights() {
  // Deselect and detach transform controls
  if (lightTransformControls) {
    lightTransformControls.detach();
  }
  selectedLight3DIndex = -1;

  // Remove lights
  for (const light of threeLights) {
    scene.remove(light);
    if (light.dispose) light.dispose();
  }
  threeLights.length = 0;

  // Remove helpers
  for (const helper of lightHelpers) {
    scene.remove(helper);
    if (helper.dispose) helper.dispose();
  }
  lightHelpers.length = 0;

  // Clear data
  lightData.length = 0;

  // Update UI
  if (window.updateLightList) {
    window.updateLightList([]);
  }

  // Hide light properties panel
  if (window.hideLightProperties) {
    window.hideLightProperties();
  }

  // Hide envmap section
  hideEnvmapSection();
}

/**
 * Create a light helper/visualizer for finite lights
 * @param {THREE.Light} light - Three.js light
 * @param {Object} usdLight - USD light data
 * @param {number} lightIndex - Index in lightData array
 * @returns {THREE.Object3D|null} Helper object
 */
function createLightHelper(light, usdLight, lightIndex) {
  const type = usdLight.type || 'unknown';
  const lightColor = new THREE.Color(usdLight.color?.[0] || 1, usdLight.color?.[1] || 1, usdLight.color?.[2] || 1);

  // Create a group to hold all helper elements
  const helperGroup = new THREE.Group();
  helperGroup.userData.lightIndex = lightIndex;
  helperGroup.userData.lightType = type;
  helperGroup.userData.isLightHelper = true;

  // Check actual Three.js light type first (SpotLight for SphereLights with shaping)
  if (light.isSpotLight) {
    // SpotLight helper for SphereLights with shaping properties
    // SpotLightHelper positions itself using the light's world position internally
    const spotHelper = new THREE.SpotLightHelper(light);
    helperGroup.add(spotHelper);

    // Add sphere at light's world position for clickability
    const sphereGeom = new THREE.SphereGeometry(usdLight.radius || 0.15, 16, 16);
    const sphereMat = new THREE.MeshBasicMaterial({
      color: lightColor,
      transparent: true,
      opacity: 0.6,
      wireframe: true
    });
    const sphereMesh = new THREE.Mesh(sphereGeom, sphereMat);

    // Get the light's world position and place the sphere there
    const worldPos = new THREE.Vector3();
    light.getWorldPosition(worldPos);
    sphereMesh.position.copy(worldPos);

    helperGroup.add(sphereMesh);

    // Don't transform helperGroup - SpotLightHelper manages its own world position
    // The sphere is already positioned at the light's world position
    return helperGroup;
  }

  // PointLight helper for SphereLights without shaping
  if (light.isPointLight) {
    const helperGeom = new THREE.SphereGeometry(usdLight.radius || 0.1, 16, 16);
    const helperMat = new THREE.MeshBasicMaterial({
      color: lightColor,
      transparent: true,
      opacity: 0.6,
      wireframe: true
    });
    const sphereMesh = new THREE.Mesh(helperGeom, helperMat);
    helperGroup.add(sphereMesh);
    helperGroup.position.copy(light.position);
    return helperGroup;
  }

  if (type === 'point' || type === 'sphere') {
    // Fallback sphere helper (shouldn't normally reach here)
    const helperGeom = new THREE.SphereGeometry(usdLight.radius || 0.1, 16, 16);
    const helperMat = new THREE.MeshBasicMaterial({
      color: lightColor,
      transparent: true,
      opacity: 0.6,
      wireframe: true
    });
    const sphereMesh = new THREE.Mesh(helperGeom, helperMat);
    helperGroup.add(sphereMesh);
    if (light.position) helperGroup.position.copy(light.position);
    return helperGroup;
  }

  if (type === 'disk') {
    const radius = usdLight.radius || 0.5;
    const helperGeom = new THREE.CircleGeometry(radius, 32);
    const helperMat = new THREE.MeshBasicMaterial({
      color: lightColor,
      transparent: true,
      opacity: 0.5,
      side: THREE.DoubleSide
    });
    const diskMesh = new THREE.Mesh(helperGeom, helperMat);
    helperGroup.add(diskMesh);

    // Add direction arrow (pointing in -Z local, which is light direction)
    const arrow = new THREE.ArrowHelper(
      new THREE.Vector3(0, 0, -1),
      new THREE.Vector3(0, 0, 0),
      radius * 1.5,
      0xffff00, 0.15, 0.1
    );
    helperGroup.add(arrow);

    if (light.parent) {
      helperGroup.position.copy(light.parent.position);
      helperGroup.quaternion.copy(light.parent.quaternion);
    }
    return helperGroup;
  }

  if (type === 'rect') {
    const width = usdLight.width || 1;
    const height = usdLight.height || 1;

    // Create rectangle outline
    const rectGeom = new THREE.PlaneGeometry(width, height);
    const rectMat = new THREE.MeshBasicMaterial({
      color: lightColor,
      transparent: true,
      opacity: 0.3,
      side: THREE.DoubleSide
    });
    const rectMesh = new THREE.Mesh(rectGeom, rectMat);
    helperGroup.add(rectMesh);

    // Add wireframe edge
    const edgeGeom = new THREE.EdgesGeometry(rectGeom);
    const edgeMat = new THREE.LineBasicMaterial({ color: lightColor });
    const edge = new THREE.LineSegments(edgeGeom, edgeMat);
    helperGroup.add(edge);

    // Add direction arrow (pointing in -Z local, which is light direction)
    const arrowLength = Math.max(width, height) * 0.8;
    const arrow = new THREE.ArrowHelper(
      new THREE.Vector3(0, 0, -1),
      new THREE.Vector3(0, 0, 0),
      arrowLength,
      0xffff00, 0.2, 0.15
    );
    helperGroup.add(arrow);

    if (light.parent) {
      helperGroup.position.copy(light.parent.position);
      helperGroup.quaternion.copy(light.parent.quaternion);
    } else if (light.isRectAreaLight) {
      helperGroup.position.copy(light.position);
      helperGroup.quaternion.copy(light.quaternion);
    }
    return helperGroup;
  }

  if (type === 'cylinder') {
    const radius = usdLight.radius || 0.1;
    const length = usdLight.length || 1;
    const helperGeom = new THREE.CylinderGeometry(radius, radius, length, 16);
    const helperMat = new THREE.MeshBasicMaterial({
      color: lightColor,
      transparent: true,
      opacity: 0.5,
      wireframe: true
    });
    const cylMesh = new THREE.Mesh(helperGeom, helperMat);
    helperGroup.add(cylMesh);

    if (light.parent) {
      helperGroup.position.copy(light.parent.position);
      helperGroup.quaternion.copy(light.parent.quaternion);
    }
    return helperGroup;
  }

  if (type === 'distant') {
    // Arrow helper for directional light - show direction
    const dir = new THREE.Vector3(0, 0, -1);
    if (light.parent) {
      dir.applyQuaternion(light.parent.quaternion);
    }
    const arrow = new THREE.ArrowHelper(dir, new THREE.Vector3(0, 0, 0), 3, lightColor.getHex(), 0.5, 0.3);
    helperGroup.add(arrow);

    // Add a small sphere at the "sun" position for clickability
    const sunGeom = new THREE.SphereGeometry(0.3, 16, 16);
    const sunMat = new THREE.MeshBasicMaterial({ color: lightColor, transparent: true, opacity: 0.8 });
    const sunMesh = new THREE.Mesh(sunGeom, sunMat);
    helperGroup.add(sunMesh);

    if (light.parent) {
      helperGroup.position.copy(light.parent.position);
    }
    return helperGroup;
  }

  // Fallback - return null for unhandled types
  return null;
}

/**
 * Convert USD RenderLight to Three.js Light
 * @param {Object} usdLight - USD light data from TinyUSDZ
 * @returns {THREE.Light|null} Three.js light object
 */
function convertUSDLightToThreeJS(usdLight) {
  const type = usdLight.type || 'unknown';
  const color = new THREE.Color(
    usdLight.color?.[0] || 1,
    usdLight.color?.[1] || 1,
    usdLight.color?.[2] || 1
  );

  // Calculate effective intensity (intensity * 2^exposure)
  let intensity = usdLight.intensity || 1;
  if (usdLight.exposure && usdLight.exposure !== 0) {
    intensity *= Math.pow(2, usdLight.exposure);
  }

  // Extract full transform (position, rotation, scale) from matrix
  const position = new THREE.Vector3(
    usdLight.position?.[0] || 0,
    usdLight.position?.[1] || 0,
    usdLight.position?.[2] || 0
  );
  const quaternion = new THREE.Quaternion();
  const scale = new THREE.Vector3(1, 1, 1);

  // Decompose transform matrix if available
  if (usdLight.transform && usdLight.transform.length === 16) {
    const matrix = new THREE.Matrix4();
    matrix.fromArray(usdLight.transform);
    matrix.decompose(position, quaternion, scale);
  }

  let light = null;
  let lightGroup = null;

  // Helper to apply transform to a light group
  const applyTransformToGroup = (group) => {
    group.position.copy(position);
    group.quaternion.copy(quaternion);
    group.scale.copy(scale);
  };

  switch (type) {
    case 'point':
    case 'sphere': {
      // Check if it has shaping (spotlight-like behavior)
      if (usdLight.shapingConeAngle && usdLight.shapingConeAngle < 90) {
        light = new THREE.SpotLight(color, intensity);
        light.angle = THREE.MathUtils.degToRad(usdLight.shapingConeAngle);
        light.penumbra = usdLight.shapingConeSoftness || 0;
        light.decay = 2;
        light.distance = 0; // Infinite range

        // Create a group to hold the spotlight and its target
        lightGroup = new THREE.Group();
        lightGroup.add(light);

        // USD lights face -Z, so target is in -Z direction
        light.target.position.set(0, 0, -5);
        lightGroup.add(light.target);

        // Apply full transform
        applyTransformToGroup(lightGroup);
      } else {
        // Point light - position only matters (omnidirectional)
        light = new THREE.PointLight(color, intensity);
        light.decay = 2;
        light.distance = 0;
        light.position.copy(position);
      }
      break;
    }

    case 'distant': {
      light = new THREE.DirectionalLight(color, intensity);

      // Create group to apply transform
      lightGroup = new THREE.Group();

      // Light at origin of group, pointing in -Z
      light.position.set(0, 0, 0);
      light.target.position.set(0, 0, -1);
      lightGroup.add(light);
      lightGroup.add(light.target);

      // Apply transform - position doesn't matter for distant light, only rotation
      lightGroup.quaternion.copy(quaternion);

      // Move the group far from origin so shadow mapping works
      const direction = new THREE.Vector3(0, 0, -1).applyQuaternion(quaternion);
      lightGroup.position.copy(direction.multiplyScalar(-50));
      break;
    }

    case 'rect': {
      const width = usdLight.width || 1;
      const height = usdLight.height || 1;
      light = new THREE.RectAreaLight(color, intensity, width, height);

      // RectAreaLight faces -Z by default (same as USD)
      lightGroup = new THREE.Group();
      lightGroup.add(light);

      // Apply full transform
      applyTransformToGroup(lightGroup);
      break;
    }

    case 'disk': {
      // Three.js doesn't have disk light, approximate with RectAreaLight (circular appearance)
      const radius = usdLight.radius || 0.5;
      light = new THREE.RectAreaLight(color, intensity, radius * 2, radius * 2);

      lightGroup = new THREE.Group();
      lightGroup.add(light);

      // Apply full transform
      applyTransformToGroup(lightGroup);
      break;
    }

    case 'cylinder': {
      // Approximate with point light
      light = new THREE.PointLight(color, intensity);
      light.decay = 2;
      light.distance = 0;

      lightGroup = new THREE.Group();
      lightGroup.add(light);

      // Apply full transform
      applyTransformToGroup(lightGroup);
      break;
    }

    case 'dome': {
      // DomeLight - environment/IBL lighting
      // Check if we have an envmap texture
      if (usdLight.envmapTextureId >= 0 && usdLight._envmapTexture) {
        // Use the preloaded envmap texture for environment lighting
        try {
          const pmremGenerator = new THREE.PMREMGenerator(renderer);
          pmremGenerator.compileEquirectangularShader();

          const envMap = pmremGenerator.fromEquirectangular(usdLight._envmapTexture).texture;
          scene.environment = envMap;

          // Optionally use as background
          if (usdLight.guideRadius && usdLight.guideRadius < 1e4) {
            // If guide radius is small, show the environment as background
            scene.background = envMap;
          }

          pmremGenerator.dispose();

          console.log(`Applied envmap from DomeLight: ${usdLight.name}, textureFile: ${usdLight.textureFile}`);

          // Still create a hemisphere light as fallback visualization
          light = new THREE.HemisphereLight(color, new THREE.Color(0x444444), intensity * 0.1);
        } catch (e) {
          console.warn(`Failed to process envmap for DomeLight: ${e.message}`);
          // Fall back to hemisphere light
          light = new THREE.HemisphereLight(color, new THREE.Color(0x444444), intensity);
        }
      } else if (usdLight.textureFile) {
        // Texture file specified but not loaded - create placeholder and log
        console.log(`DomeLight ${usdLight.name} has textureFile: ${usdLight.textureFile} (not loaded)`);
        console.log(`Use envmapTextureId: ${usdLight.envmapTextureId} for preloaded textures`);

        // Use hemisphere light as fallback
        light = new THREE.HemisphereLight(color, new THREE.Color(0x444444), intensity);
      } else {
        // No texture - use hemisphere light for basic environment lighting
        light = new THREE.HemisphereLight(color, new THREE.Color(0x444444), intensity);
      }
      break;
    }

    default:
      console.warn(`Unsupported light type: ${type}`);
      return null;
  }

  if (!light) return null;

  // Configure shadows (only for light types that support them)
  // RectAreaLight and HemisphereLight do NOT support shadows in Three.js
  const supportsShadows = light.isDirectionalLight || light.isSpotLight || light.isPointLight;
  if (supportsShadows && usdLight.shadowEnable !== false) {
    light.castShadow = true;
    if (light.shadow) {
      light.shadow.mapSize.width = 1024;
      light.shadow.mapSize.height = 1024;
      light.shadow.bias = -0.0001;

      if (light.shadow.camera) {
        if (light.isDirectionalLight) {
          light.shadow.camera.left = -10;
          light.shadow.camera.right = 10;
          light.shadow.camera.top = 10;
          light.shadow.camera.bottom = -10;
          light.shadow.camera.near = 0.1;
          light.shadow.camera.far = 50;
        } else if (light.isSpotLight) {
          light.shadow.camera.near = 0.1;
          light.shadow.camera.far = 50;
        }
      }
    }
  }

  // Set name
  const lightObj = lightGroup || light;
  lightObj.name = usdLight.name || `Light_${type}`;
  lightObj.userData.usdLight = usdLight;

  return lightObj;
}

/**
 * Detect image format from magic bytes
 * @param {Uint8Array} data - Raw image data
 * @returns {string} Format name: 'exr', 'png', 'jpeg', 'hdr', or 'unknown'
 */
function detectImageFormat(data) {
  if (!data || data.length < 4) return 'unknown';

  // EXR: magic number 0x76, 0x2f, 0x31, 0x01
  if (data[0] === 0x76 && data[1] === 0x2f && data[2] === 0x31 && data[3] === 0x01) {
    return 'exr';
  }

  // PNG: magic number 0x89, 0x50, 0x4E, 0x47
  if (data[0] === 0x89 && data[1] === 0x50 && data[2] === 0x4E && data[3] === 0x47) {
    return 'png';
  }

  // JPEG: magic number 0xFF, 0xD8, 0xFF
  if (data[0] === 0xFF && data[1] === 0xD8 && data[2] === 0xFF) {
    return 'jpeg';
  }

  // Radiance HDR: starts with "#?"
  if (data[0] === 0x23 && data[1] === 0x3F) {
    return 'hdr';
  }

  return 'unknown';
}

/**
 * Decode image data using browser APIs
 * @param {Uint8Array} data - Raw compressed image data
 * @param {string} format - Image format
 * @returns {Promise<{width: number, height: number, data: Uint8Array|Float32Array, channels: number}|null>}
 */
async function decodeImageData(data, format) {
  if (format === 'png' || format === 'jpeg') {
    // Use browser's built-in image decoding
    return new Promise((resolve) => {
      const blob = new Blob([data], { type: format === 'png' ? 'image/png' : 'image/jpeg' });
      const url = URL.createObjectURL(blob);
      const img = new Image();

      img.onload = () => {
        const canvas = document.createElement('canvas');
        canvas.width = img.width;
        canvas.height = img.height;
        const ctx = canvas.getContext('2d');
        ctx.drawImage(img, 0, 0);
        const imageData = ctx.getImageData(0, 0, img.width, img.height);
        URL.revokeObjectURL(url);

        resolve({
          width: img.width,
          height: img.height,
          data: imageData.data,
          channels: 4,
          decoded: true
        });
      };

      img.onerror = () => {
        URL.revokeObjectURL(url);
        console.warn(`Failed to decode ${format} image`);
        resolve(null);
      };

      img.src = url;
    });
  }

  if (format === 'hdr') {
    // Parse Radiance HDR format
    return parseRadianceHDR(data);
  }

  if (format === 'exr') {
    // For EXR, we'll need external decoder or return raw for now
    console.log('EXR format detected - attempting to parse');
    return parseSimpleEXR(data);
  }

  return null;
}

/**
 * Parse simple Radiance HDR format
 * @param {Uint8Array} data - Raw HDR data
 * @returns {{width: number, height: number, data: Float32Array, channels: number}|null}
 */
function parseRadianceHDR(data) {
  try {
    const text = new TextDecoder('ascii').decode(data);
    const lines = text.split('\n');

    let width = 0, height = 0;
    let dataStart = 0;

    // Find dimensions and data start
    for (let i = 0; i < lines.length; i++) {
      const line = lines[i].trim();
      if (line.match(/-Y\s+(\d+)\s+\+X\s+(\d+)/)) {
        const match = line.match(/-Y\s+(\d+)\s+\+X\s+(\d+)/);
        height = parseInt(match[1]);
        width = parseInt(match[2]);
        // Calculate byte offset to data
        let offset = 0;
        for (let j = 0; j <= i; j++) {
          offset += lines[j].length + 1; // +1 for newline
        }
        dataStart = offset;
        break;
      }
    }

    if (width <= 0 || height <= 0) {
      console.warn('Could not parse HDR dimensions');
      return null;
    }

    // Parse RGBE data (simplified - no RLE)
    const rgbeData = data.slice(dataStart);
    const floatData = new Float32Array(width * height * 3);

    for (let i = 0; i < width * height && i * 4 + 3 < rgbeData.length; i++) {
      const r = rgbeData[i * 4];
      const g = rgbeData[i * 4 + 1];
      const b = rgbeData[i * 4 + 2];
      const e = rgbeData[i * 4 + 3];

      if (e === 0) {
        floatData[i * 3] = 0;
        floatData[i * 3 + 1] = 0;
        floatData[i * 3 + 2] = 0;
      } else {
        const scale = Math.pow(2, e - 128 - 8);
        floatData[i * 3] = r * scale;
        floatData[i * 3 + 1] = g * scale;
        floatData[i * 3 + 2] = b * scale;
      }
    }

    return {
      width,
      height,
      data: floatData,
      channels: 3,
      decoded: true
    };
  } catch (e) {
    console.warn('Failed to parse HDR:', e);
    return null;
  }
}

/**
 * Parse simple EXR format (scanline, uncompressed or ZIP)
 * This is a minimal parser - complex EXR files may not work
 * @param {Uint8Array} data - Raw EXR data
 * @returns {{width: number, height: number, data: Float32Array, channels: number}|null}
 */
function parseSimpleEXR(data) {
  try {
    // EXR header parsing
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);

    // Check magic number
    if (view.getUint32(0, true) !== 0x01312f76) {
      console.warn('Invalid EXR magic number');
      return null;
    }

    // Version (byte 4)
    const version = view.getUint8(4);
    console.log(`EXR version: ${version}`);

    // Parse header attributes
    let offset = 8;
    let width = 0, height = 0;
    let compression = 0;
    let channels = [];

    while (offset < data.length - 1) {
      // Read attribute name
      let nameEnd = offset;
      while (data[nameEnd] !== 0 && nameEnd < data.length) nameEnd++;
      if (nameEnd === offset) break; // Empty name = end of header

      const name = new TextDecoder().decode(data.slice(offset, nameEnd));
      offset = nameEnd + 1;

      // Read type name
      let typeEnd = offset;
      while (data[typeEnd] !== 0 && typeEnd < data.length) typeEnd++;
      const typeName = new TextDecoder().decode(data.slice(offset, typeEnd));
      offset = typeEnd + 1;

      // Read attribute size
      const attrSize = view.getInt32(offset, true);
      offset += 4;

      // Parse known attributes
      if (name === 'displayWindow' || name === 'dataWindow') {
        const xMin = view.getInt32(offset, true);
        const yMin = view.getInt32(offset + 4, true);
        const xMax = view.getInt32(offset + 8, true);
        const yMax = view.getInt32(offset + 12, true);
        if (name === 'dataWindow') {
          width = xMax - xMin + 1;
          height = yMax - yMin + 1;
        }
      } else if (name === 'compression') {
        compression = data[offset];
      } else if (name === 'channels') {
        // Parse channel list
        let chOffset = offset;
        while (chOffset < offset + attrSize - 1) {
          let chNameEnd = chOffset;
          while (data[chNameEnd] !== 0 && chNameEnd < offset + attrSize) chNameEnd++;
          if (chNameEnd === chOffset) break;

          const chName = new TextDecoder().decode(data.slice(chOffset, chNameEnd));
          chOffset = chNameEnd + 1;

          const pixelType = view.getInt32(chOffset, true); // 0=uint, 1=half, 2=float
          chOffset += 16; // Skip rest of channel info

          channels.push({ name: chName, type: pixelType });
        }
      }

      offset += attrSize;
    }

    console.log(`EXR: ${width}x${height}, compression: ${compression}, channels: ${channels.length}`);

    if (width <= 0 || height <= 0) {
      console.warn('Could not parse EXR dimensions');
      return null;
    }

    // For now, return basic info even if we can't decode the pixel data
    // Full EXR decoding requires handling compression (PIZ, ZIP, etc.)
    console.log('EXR parsing: dimensions found, pixel decoding not fully implemented');

    // Try to find and decode uncompressed data
    // Skip to end of header (null byte)
    while (offset < data.length && data[offset] !== 0) offset++;
    offset++; // Skip null byte

    // Check if we have enough data for uncompressed scanlines
    const expectedSize = width * height * channels.length * 2; // Assuming half float
    const remainingData = data.length - offset;

    if (compression === 0 && remainingData >= expectedSize) {
      // Uncompressed - try to read half float data
      const floatData = new Float32Array(width * height * 3);

      // Read scanline offsets table
      const numScanlines = height;
      offset += numScanlines * 8; // Skip offset table

      // Read scanlines (simplified)
      for (let y = 0; y < height; y++) {
        // Each scanline: y-coord (4 bytes) + size (4 bytes) + data
        const scanY = view.getInt32(offset, true);
        const scanSize = view.getInt32(offset + 4, true);
        offset += 8;

        // Read pixel data
        for (let x = 0; x < width && offset + channels.length * 2 <= data.length; x++) {
          for (let c = 0; c < Math.min(3, channels.length); c++) {
            const halfBits = view.getUint16(offset + c * 2, true);
            floatData[(y * width + x) * 3 + c] = halfToFloat(halfBits);
          }
          offset += channels.length * 2;
        }
      }

      return {
        width,
        height,
        data: floatData,
        channels: 3,
        decoded: true
      };
    }

    // Return dimensions at least, even without pixel data
    return {
      width,
      height,
      data: new Float32Array(width * height * 3),
      channels: 3,
      decoded: false,
      partial: true
    };
  } catch (e) {
    console.warn('Failed to parse EXR:', e);
    return null;
  }
}

/**
 * Convert half-float (16-bit) to float (32-bit)
 */
function halfToFloat(h) {
  const s = (h & 0x8000) >> 15;
  const e = (h & 0x7C00) >> 10;
  const f = h & 0x03FF;

  if (e === 0) {
    return (s ? -1 : 1) * Math.pow(2, -14) * (f / 1024);
  } else if (e === 0x1F) {
    return f ? NaN : ((s ? -1 : 1) * Infinity);
  }

  return (s ? -1 : 1) * Math.pow(2, e - 15) * (1 + f / 1024);
}

/**
 * Create Three.js texture from image data
 * @param {Object} imageData - Image data from getImage()
 * @returns {THREE.Texture|null} Three.js texture or null
 */
function createTextureFromImageData(imageData) {
  if (!imageData) {
    console.warn('No image data provided');
    return null;
  }

  const width = imageData.width;
  const height = imageData.height;
  const channels = imageData.channels;
  const decoded = imageData.decoded;

  console.log(`Creating texture from image: ${width}x${height}, ${channels} channels, decoded: ${decoded}`);

  // Validate dimensions
  if (width <= 0 || height <= 0) {
    console.warn(`Invalid image dimensions: ${width}x${height}`);
    return null;
  }

  // Check if we have raw pixel data
  if (!imageData.data || imageData.data.length === 0) {
    console.warn('No pixel data available in image');
    return null;
  }

  // Determine if this is HDR data (float) or LDR data (uint8)
  // HDR images like EXR are typically decoded as float32
  const bytesPerPixel = imageData.data.length / (width * height);
  const isHDR = bytesPerPixel >= channels * 4; // 4 bytes per float per channel

  console.log(`Image bytes per pixel: ${bytesPerPixel}, isHDR: ${isHDR}`);

  let texture;

  if (isHDR || decoded) {
    // Create float texture for HDR data
    // Assume float32 RGBA data
    const floatData = new Float32Array(width * height * 4);

    if (bytesPerPixel === channels * 4) {
      // Data is already float32
      const srcData = new Float32Array(imageData.data.buffer, imageData.data.byteOffset, width * height * channels);

      for (let i = 0; i < width * height; i++) {
        floatData[i * 4 + 0] = channels > 0 ? srcData[i * channels + 0] : 0;
        floatData[i * 4 + 1] = channels > 1 ? srcData[i * channels + 1] : 0;
        floatData[i * 4 + 2] = channels > 2 ? srcData[i * channels + 2] : 0;
        floatData[i * 4 + 3] = channels > 3 ? srcData[i * channels + 3] : 1;
      }
    } else {
      // Data is uint8, convert to float
      for (let i = 0; i < width * height; i++) {
        floatData[i * 4 + 0] = channels > 0 ? imageData.data[i * channels + 0] / 255 : 0;
        floatData[i * 4 + 1] = channels > 1 ? imageData.data[i * channels + 1] / 255 : 0;
        floatData[i * 4 + 2] = channels > 2 ? imageData.data[i * channels + 2] / 255 : 0;
        floatData[i * 4 + 3] = channels > 3 ? imageData.data[i * channels + 3] / 255 : 1;
      }
    }

    texture = new THREE.DataTexture(
      floatData,
      width,
      height,
      THREE.RGBAFormat,
      THREE.FloatType
    );
    texture.colorSpace = THREE.LinearSRGBColorSpace;
  } else {
    // Create uint8 texture for LDR data
    const rgbaData = new Uint8Array(width * height * 4);

    for (let i = 0; i < width * height; i++) {
      rgbaData[i * 4 + 0] = channels > 0 ? imageData.data[i * channels + 0] : 0;
      rgbaData[i * 4 + 1] = channels > 1 ? imageData.data[i * channels + 1] : 0;
      rgbaData[i * 4 + 2] = channels > 2 ? imageData.data[i * channels + 2] : 0;
      rgbaData[i * 4 + 3] = channels > 3 ? imageData.data[i * channels + 3] : 255;
    }

    texture = new THREE.DataTexture(
      rgbaData,
      width,
      height,
      THREE.RGBAFormat,
      THREE.UnsignedByteType
    );
    texture.colorSpace = THREE.SRGBColorSpace;
  }

  // Configure texture for equirectangular environment map
  texture.mapping = THREE.EquirectangularReflectionMapping;
  texture.wrapS = THREE.RepeatWrapping;
  texture.wrapT = THREE.ClampToEdgeWrapping;
  texture.minFilter = THREE.LinearMipmapLinearFilter;
  texture.magFilter = THREE.LinearFilter;
  texture.generateMipmaps = true;
  texture.needsUpdate = true;

  console.log(`Created envmap texture: ${width}x${height}`);
  return texture;
}

/**
 * Load lights from USD data
 * @param {Object} usdLoader - TinyUSDZ loader instance
 */
async function loadLightsFromUSD(usdLoader) {
  clearLights();

  const numLights = usdLoader.numLights();
  console.log(`Found ${numLights} lights in USD file`);

  for (let i = 0; i < numLights; i++) {
    const usdLight = usdLoader.getLight(i);

    if (usdLight.error) {
      console.error(`Error getting light ${i}:`, usdLight.error);
      continue;
    }

    console.log(`Light ${i}:`, usdLight);

    // For dome lights with envmap texture, try to load the texture
    if (usdLight.type === 'dome') {
      let imageData = null;
      let decodedImageData = null;

      if (usdLight.envmapTextureId >= 0) {
        console.log(`DomeLight has envmap texture ID: ${usdLight.envmapTextureId}`);
        imageData = usdLoader.getImage(usdLight.envmapTextureId);

        if (imageData && !imageData.error) {
          console.log(`Envmap image: ${imageData.width}x${imageData.height}, decoded: ${imageData.decoded}`);

          // If image is not decoded (has invalid dimensions), try to decode it
          if (!imageData.decoded || imageData.width <= 0 || imageData.height <= 0) {
            if (imageData.data && imageData.data.length > 0) {
              const format = detectImageFormat(imageData.data);
              console.log(`Detected image format: ${format}`);

              if (format !== 'unknown') {
                try {
                  decodedImageData = await decodeImageData(imageData.data, format);
                  if (decodedImageData) {
                    console.log(`Decoded image: ${decodedImageData.width}x${decodedImageData.height}`);
                    imageData = decodedImageData;
                  }
                } catch (e) {
                  console.warn('Failed to decode image:', e);
                }
              }
            }
          }

          const envTexture = createTextureFromImageData(imageData);
          if (envTexture) {
            usdLight._envmapTexture = envTexture;
          }
        }
      }
      // Show envmap section with dome light info and image data
      showEnvmapSection(usdLight, imageData);
    }

    lightData.push(usdLight);

    const threeLight = convertUSDLightToThreeJS(usdLight);
    if (threeLight) {
      scene.add(threeLight);
      threeLights.push(threeLight);

      // Create helper for visualization
      const actualLight = threeLight.isGroup ?
        threeLight.children.find(c => c.isLight) : threeLight;

      const lightIndex = lightData.length - 1; // Index of just-pushed light
      if (actualLight) {
        const helper = createLightHelper(actualLight, usdLight, lightIndex);
        if (helper) {
          scene.add(helper);
          lightHelpers.push(helper);
        }
      }
    }
  }

  // Update UI
  if (window.updateLightList) {
    window.updateLightList(lightData);
  }

  // Update stats
  document.getElementById('statMeshes').textContent =
    scene.children.filter(c => c.isMesh).length;
}

// ============================================
// HDRI Projection from Lights
// ============================================

// HDRI state
let hdriProjection = null;
let projectedHDRI = null;
let hdriTexture = null;
let hdriPreviewVisible = false;
let hdriAppliedToScene = false;

// HDRI settings
let hdriSettings = {
  width: 1024,
  height: 512,
  maxDistance: 100,
  supersampling: 1,
  center: { x: 0, y: 0, z: 0 }
};

// Live HDRI update state
let hdriLiveUpdate = false;
let hdriUpdateDebounceTimer = null;
const HDRI_UPDATE_DEBOUNCE_MS = 100;

/**
 * Refresh HDRI projection - re-projects lights and updates preview
 */
function refreshHDRIProjection() {
  // Only refresh if we have previously projected
  if (!projectedHDRI && !hdriProjection) {
    console.log('No HDRI projection to refresh - run Project first');
    return;
  }

  console.log('Refreshing HDRI projection...');
  projectLightsToHDRI();

  // Update preview canvas if visible
  if (hdriPreviewVisible) {
    updateHDRIPreviewCanvas();
  }

  // Re-apply to scene if it was applied
  if (hdriAppliedToScene) {
    applyHDRIToScene();
  }
}

/**
 * Schedule a debounced HDRI refresh (for live updates during dragging)
 */
function scheduleHDRIRefresh() {
  if (!hdriLiveUpdate) return;
  if (!projectedHDRI && !hdriProjection) return;

  // Clear existing timer
  if (hdriUpdateDebounceTimer) {
    clearTimeout(hdriUpdateDebounceTimer);
  }

  // Schedule new refresh
  hdriUpdateDebounceTimer = setTimeout(() => {
    refreshHDRIProjection();
    hdriUpdateDebounceTimer = null;
  }, HDRI_UPDATE_DEBOUNCE_MS);
}

/**
 * Enable/disable live HDRI updates
 */
function setHDRILiveUpdate(enabled) {
  hdriLiveUpdate = enabled;
  console.log(`HDRI live update: ${enabled ? 'enabled' : 'disabled'}`);

  // If enabling and we have a projection, refresh immediately
  if (enabled && (projectedHDRI || hdriProjection)) {
    refreshHDRIProjection();
  }
}

/**
 * Get current live update state
 */
function isHDRILiveUpdateEnabled() {
  return hdriLiveUpdate;
}

/**
 * Extract position and orientation from USD light transform
 * @param {Object} usdLight - USD light data
 * @returns {Object} Extracted position and orientation vectors
 */
function extractLightTransform(usdLight) {
  // Default position from position array
  let position = {
    x: usdLight.position?.[0] || 0,
    y: usdLight.position?.[1] || 0,
    z: usdLight.position?.[2] || 0
  };

  // Default orientation vectors (USD light default faces -Z in local space)
  let normal = { x: 0, y: 0, z: -1 };  // Light facing direction
  let tangent = { x: 1, y: 0, z: 0 };  // Width direction

  // Extract from transform matrix if available (column-major 4x4)
  if (usdLight.transform && usdLight.transform.length === 16) {
    const m = usdLight.transform;

    // Position from translation column (indices 12, 13, 14)
    position.x = m[12];
    position.y = m[13];
    position.z = m[14];

    // Extract rotation columns for orientation
    // Column 0 = X axis (tangent/width direction)
    tangent.x = m[0];
    tangent.y = m[1];
    tangent.z = m[2];

    // Column 2 = Z axis (facing direction, USD lights face -Z)
    // Negate to get the direction the light is pointing
    normal.x = -m[8];
    normal.y = -m[9];
    normal.z = -m[10];

    // Normalize vectors
    const normLen = Math.sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    if (normLen > 0) {
      normal.x /= normLen;
      normal.y /= normLen;
      normal.z /= normLen;
    }

    const tanLen = Math.sqrt(tangent.x * tangent.x + tangent.y * tangent.y + tangent.z * tangent.z);
    if (tanLen > 0) {
      tangent.x /= tanLen;
      tangent.y /= tanLen;
      tangent.z /= tanLen;
    }
  } else if (usdLight.direction) {
    // Use explicit direction if no transform but direction is provided
    normal.x = usdLight.direction[0];
    normal.y = usdLight.direction[1];
    normal.z = usdLight.direction[2];
  }

  return { position, normal, tangent };
}

/**
 * Convert USD light data to projection light format
 */
function convertLightDataToProjectionLight(usdLight) {
  const color = usdLight.color || [1, 1, 1];
  const intensity = usdLight.intensity || 1;
  const exposure = usdLight.exposure || 0;
  const effectiveIntensity = intensity * Math.pow(2, exposure);

  // Extract position and orientation from transform
  const { position, normal, tangent } = extractLightTransform(usdLight);

  switch (usdLight.type) {
    case 'sphere':
      return new SphereLight({
        position: position,
        radius: usdLight.radius || 0.1,
        color: { r: color[0], g: color[1], b: color[2] },
        intensity: effectiveIntensity
      });

    case 'point':
      // Use new PointLight with pseudo-radius for HDRI visualization
      return new PointLight({
        position: position,
        color: { r: color[0], g: color[1], b: color[2] },
        intensity: effectiveIntensity,
        // Allow user-configurable pseudo-radius and intensity multiplier
        pseudoRadius: usdLight.pseudoRadius,
        intensityMultiplier: usdLight.intensityMultiplier
      });

    case 'distant':
      // Use new DistantLight with pseudo-angular-radius for HDRI visualization
      return new DistantLight({
        direction: normal, // normal points in the light direction
        color: { r: color[0], g: color[1], b: color[2] },
        intensity: effectiveIntensity,
        // Allow user-configurable angle and intensity multiplier
        angle: usdLight.angle,
        intensityMultiplier: usdLight.intensityMultiplier
      });

    case 'rect':
      return new AreaLight({
        position: position,
        normal: normal,
        tangent: tangent,
        width: usdLight.width || 1,
        height: usdLight.height || 1,
        color: { r: color[0], g: color[1], b: color[2] },
        intensity: effectiveIntensity
      });

    case 'disk':
      return new DiskLight({
        position: position,
        normal: normal,
        radius: usdLight.radius || 0.5,
        color: { r: color[0], g: color[1], b: color[2] },
        intensity: effectiveIntensity
      });

    case 'spot':
      // Treat spot as point light with pseudo-radius
      return new PointLight({
        position: position,
        color: { r: color[0], g: color[1], b: color[2] },
        intensity: effectiveIntensity,
        pseudoRadius: 0.05,
        intensityMultiplier: usdLight.intensityMultiplier || 1.0
      });

    case 'dome':
      // Dome light doesn't project to HDRI (it IS an HDRI)
      return null;

    default:
      // For cylinder or unknown types - use sphere as fallback
      return new SphereLight({
        position: position,
        radius: usdLight.radius || 0.1,
        color: { r: color[0], g: color[1], b: color[2] },
        intensity: effectiveIntensity
      });
  }
}

/**
 * Project all loaded lights to HDRI
 */
function projectLightsToHDRI(options = {}) {
  const settings = { ...hdriSettings, ...options };

  console.log('Projecting lights to HDRI...');
  console.log(`  Resolution: ${settings.width}x${settings.height}`);
  console.log(`  Lights: ${lightData.length}`);

  // Create projection engine
  hdriProjection = new LightHDRIProjection({
    width: settings.width,
    height: settings.height,
    center: settings.center,
    maxDistance: settings.maxDistance
  });

  // Add lights (only enabled/visible lights)
  let addedLights = 0;
  let skippedLights = 0;
  for (let i = 0; i < lightData.length; i++) {
    // Skip lights that are turned off
    if (threeLights[i] && !threeLights[i].visible) {
      skippedLights++;
      continue;
    }

    const usdLight = lightData[i];
    const projLight = convertLightDataToProjectionLight(usdLight);
    if (projLight) {
      hdriProjection.addLight(projLight);
      addedLights++;
    }
  }

  if (skippedLights > 0) {
    console.log(`  Skipped ${skippedLights} disabled lights`);
  }

  console.log(`  Added ${addedLights} lights to projection`);

  if (addedLights === 0) {
    console.warn('No lights to project');
    return null;
  }

  // Generate HDRI
  const startTime = performance.now();
  if (settings.supersampling > 1) {
    projectedHDRI = hdriProjection.generateSupersampled(settings.supersampling);
  } else {
    projectedHDRI = hdriProjection.generate();
  }
  const elapsed = (performance.now() - startTime).toFixed(1);

  console.log(`  Generated in ${elapsed}ms`);

  // Analyze result
  let minVal = Infinity, maxVal = 0, nonZero = 0;
  for (let i = 0; i < projectedHDRI.data.length; i++) {
    const v = projectedHDRI.data[i];
    if (v > 0) {
      nonZero++;
      minVal = Math.min(minVal, v);
      maxVal = Math.max(maxVal, v);
    }
  }
  console.log(`  Non-zero: ${nonZero} / ${projectedHDRI.data.length}`);
  console.log(`  Value range: ${minVal.toExponential(2)} - ${maxVal.toExponential(2)}`);

  // Update UI
  if (window.updateHDRIStatus) {
    window.updateHDRIStatus({
      generated: true,
      width: settings.width,
      height: settings.height,
      lights: addedLights,
      maxValue: maxVal
    });
  }

  return projectedHDRI;
}

/**
 * Create Three.js texture from projected HDRI
 */
function createHDRITexture() {
  if (!projectedHDRI) {
    console.warn('No HDRI data to create texture from');
    return null;
  }

  const width = projectedHDRI.width;
  const height = projectedHDRI.height;

  // Expand RGB to RGBA and flip vertically for correct Three.js orientation
  // Three.js equirectangular expects top of image = looking up (+Y)
  // Our projection generates top = looking up, but we need to flip for WebGL
  const rgbaData = new Float32Array(width * height * 4);
  for (let y = 0; y < height; y++) {
    const srcY = height - 1 - y;  // Flip Y
    for (let x = 0; x < width; x++) {
      const srcIdx = (srcY * width + x) * 3;
      const dstIdx = (y * width + x) * 4;
      rgbaData[dstIdx] = projectedHDRI.data[srcIdx];
      rgbaData[dstIdx + 1] = projectedHDRI.data[srcIdx + 1];
      rgbaData[dstIdx + 2] = projectedHDRI.data[srcIdx + 2];
      rgbaData[dstIdx + 3] = 1.0;
    }
  }

  // Create texture
  if (hdriTexture) {
    hdriTexture.dispose();
  }

  hdriTexture = new THREE.DataTexture(
    rgbaData,
    projectedHDRI.width,
    projectedHDRI.height,
    THREE.RGBAFormat,
    THREE.FloatType
  );

  hdriTexture.mapping = THREE.EquirectangularReflectionMapping;
  hdriTexture.wrapS = THREE.RepeatWrapping;
  hdriTexture.wrapT = THREE.ClampToEdgeWrapping;
  hdriTexture.magFilter = THREE.LinearFilter;
  hdriTexture.minFilter = THREE.LinearMipmapLinearFilter;
  hdriTexture.generateMipmaps = true;
  hdriTexture.colorSpace = THREE.LinearSRGBColorSpace;
  hdriTexture.flipY = false;  // Don't flip - projection data is already in correct orientation
  hdriTexture.needsUpdate = true;

  return hdriTexture;
}

/**
 * Apply projected HDRI to scene environment
 */
// Whether to show envmap as background
let showEnvmapBackground = false;
const defaultBackgroundColor = new THREE.Color(0x1a1a2e);

function applyHDRIToScene() {
  if (!projectedHDRI) {
    console.warn('Project lights first');
    return;
  }

  const texture = createHDRITexture();
  if (!texture) return;

  scene.environment = texture;

  // Set background if enabled
  if (showEnvmapBackground) {
    scene.background = texture;
  }

  hdriAppliedToScene = true;
  console.log('HDRI applied to scene environment');

  if (window.updateHDRIStatus) {
    window.updateHDRIStatus({ applied: true });
  }
}

/**
 * Remove HDRI from scene
 */
function removeHDRIFromScene() {
  scene.environment = null;
  scene.background = defaultBackgroundColor;
  hdriAppliedToScene = false;
  console.log('HDRI removed from scene');

  if (window.updateHDRIStatus) {
    window.updateHDRIStatus({ applied: false });
  }
}

/**
 * Set whether to show envmap as background
 * @param {boolean} show - Whether to show envmap as background
 */
function setShowEnvmapBackground(show) {
  showEnvmapBackground = show;

  if (hdriAppliedToScene && hdriTexture) {
    if (show) {
      scene.background = hdriTexture;
    } else {
      scene.background = defaultBackgroundColor;
    }
  }

  console.log(`Envmap background: ${show ? 'enabled' : 'disabled'}`);
}

/**
 * Get whether envmap background is enabled
 * @returns {boolean}
 */
function getShowEnvmapBackground() {
  return showEnvmapBackground;
}

// ============================================
// Lighting Mode: Lights vs Environment Map
// ============================================

// Lighting mode: 'lights' (use actual lights) or 'envmap' (use HDRI environment)
let lightingMode = 'lights';

/**
 * Set the lighting mode
 * @param {string} mode - 'lights' or 'envmap'
 */
function setLightingMode(mode) {
  if (mode !== 'lights' && mode !== 'envmap') {
    console.warn(`Invalid lighting mode: ${mode}`);
    return;
  }

  if (mode === lightingMode) return;

  lightingMode = mode;
  console.log(`Lighting mode: ${mode}`);

  if (mode === 'envmap') {
    // Switch to environment map mode
    // First, project HDRI if not already done
    if (!projectedHDRI) {
      console.log('Auto-projecting HDRI for envmap mode...');
      projectLightsToHDRI();
    }

    // Apply HDRI to scene
    applyHDRIToScene();

    // Disable all lights (but keep helpers visible for reference)
    for (let i = 0; i < threeLights.length; i++) {
      const lightObj = threeLights[i];
      // Find the actual light inside groups
      if (lightObj.isGroup) {
        lightObj.traverse((child) => {
          if (child.isLight) {
            child.visible = false;
          }
        });
      } else if (lightObj.isLight) {
        lightObj.visible = false;
      }
    }
    console.log('Lights disabled, using HDRI environment');
  } else {
    // Switch back to lights mode
    // Remove HDRI from scene
    removeHDRIFromScene();

    // Re-enable all lights
    for (let i = 0; i < threeLights.length; i++) {
      const lightObj = threeLights[i];
      if (lightObj.isGroup) {
        lightObj.traverse((child) => {
          if (child.isLight) {
            child.visible = true;
          }
        });
      } else if (lightObj.isLight) {
        lightObj.visible = true;
      }
    }
    console.log('Lights enabled, using direct lighting');
  }

  // Update UI
  if (window.updateLightingModeUI) {
    window.updateLightingModeUI(mode);
  }
}

/**
 * Get current lighting mode
 * @returns {string} Current lighting mode ('lights' or 'envmap')
 */
function getLightingMode() {
  return lightingMode;
}

/**
 * Toggle between lights and envmap modes
 */
function toggleLightingMode() {
  setLightingMode(lightingMode === 'lights' ? 'envmap' : 'lights');
}

/**
 * Toggle HDRI preview panel
 */
function toggleHDRIPreview() {
  hdriPreviewVisible = !hdriPreviewVisible;

  if (window.setHDRIPreviewVisible) {
    window.setHDRIPreviewVisible(hdriPreviewVisible);
  }

  if (hdriPreviewVisible && projectedHDRI) {
    updateHDRIPreviewCanvas();
  }
}

/**
 * Update HDRI preview canvas
 */
function updateHDRIPreviewCanvas() {
  if (!projectedHDRI) return;

  const canvas = document.getElementById('hdri-preview-canvas');
  if (!canvas) return;

  const ctx = canvas.getContext('2d');
  const width = projectedHDRI.width;
  const height = projectedHDRI.height;

  // Set canvas size
  canvas.width = width;
  canvas.height = height;

  // Check normalize option
  const normalizeCheckbox = document.getElementById('hdri-normalize-checkbox');
  const normalize = normalizeCheckbox ? normalizeCheckbox.checked : false;

  // Find max value for normalization
  let maxVal = 1;
  if (normalize) {
    for (let i = 0; i < projectedHDRI.data.length; i++) {
      maxVal = Math.max(maxVal, projectedHDRI.data[i]);
    }
  }

  // Create image data
  const imageData = ctx.createImageData(width, height);
  const data = imageData.data;

  // Get exposure from settings
  const exposure = Math.pow(2, window.currentExposure || 0);

  // Tone map and convert to sRGB
  for (let i = 0; i < width * height; i++) {
    let r = projectedHDRI.data[i * 3] * exposure;
    let g = projectedHDRI.data[i * 3 + 1] * exposure;
    let b = projectedHDRI.data[i * 3 + 2] * exposure;

    // Apply normalization if enabled
    if (normalize && maxVal > 0) {
      r = r / maxVal;
      g = g / maxVal;
      b = b / maxVal;
    } else {
      // Reinhard tone mapping (only when not normalizing)
      r = r / (1 + r);
      g = g / (1 + g);
      b = b / (1 + b);
    }

    // Gamma correction
    const gamma = 1 / 2.2;
    r = Math.pow(Math.max(0, r), gamma);
    g = Math.pow(Math.max(0, g), gamma);
    b = Math.pow(Math.max(0, b), gamma);

    data[i * 4] = Math.floor(Math.min(255, r * 255));
    data[i * 4 + 1] = Math.floor(Math.min(255, g * 255));
    data[i * 4 + 2] = Math.floor(Math.min(255, b * 255));
    data[i * 4 + 3] = 255;
  }

  ctx.putImageData(imageData, 0, 0);

  // Setup mouse event handlers for pixel value display
  setupHDRICanvasMouseHandler(canvas);
}

// Track if mouse handler is already set up
let hdriCanvasMouseHandlerSet = false;

/**
 * Setup mouse event handlers for HDRI canvas pixel display
 */
function setupHDRICanvasMouseHandler(canvas) {
  if (hdriCanvasMouseHandlerSet) return;
  hdriCanvasMouseHandlerSet = true;

  const pixelInfo = document.getElementById('hdri-pixel-info');
  if (!pixelInfo) return;

  canvas.addEventListener('mousemove', (e) => {
    if (!projectedHDRI) return;

    const rect = canvas.getBoundingClientRect();
    const scaleX = projectedHDRI.width / rect.width;
    const scaleY = projectedHDRI.height / rect.height;

    const x = Math.floor((e.clientX - rect.left) * scaleX);
    const y = Math.floor((e.clientY - rect.top) * scaleY);

    if (x >= 0 && x < projectedHDRI.width && y >= 0 && y < projectedHDRI.height) {
      const idx = (y * projectedHDRI.width + x) * 3;
      const r = projectedHDRI.data[idx];
      const g = projectedHDRI.data[idx + 1];
      const b = projectedHDRI.data[idx + 2];

      // Format values - use scientific notation for very small/large values
      const formatVal = (v) => {
        if (v === 0) return '0';
        if (Math.abs(v) < 0.001 || Math.abs(v) >= 1000) {
          return v.toExponential(2);
        }
        return v.toFixed(3);
      };

      pixelInfo.innerHTML = `<span class="coord">[${x}, ${y}]</span> ` +
        `<span class="value">R:</span>${formatVal(r)} ` +
        `<span class="value">G:</span>${formatVal(g)} ` +
        `<span class="value">B:</span>${formatVal(b)}`;
    }
  });

  canvas.addEventListener('mouseleave', () => {
    pixelInfo.innerHTML = '<span class="coord">Move mouse over image</span>';
  });
}

// ============================================
// Envmap Preview (DomeLight texture)
// ============================================

// Store current envmap image data
let currentEnvmapImageData = null;
let envmapCanvasMouseHandlerSet = false;

/**
 * Show envmap section with DomeLight data
 * @param {Object} domeLight - USD DomeLight data
 * @param {Object} imageData - Raw image data from getImage()
 */
function showEnvmapSection(domeLight, imageData) {
  const section = document.getElementById('envmap-section');
  if (!section) return;

  section.style.display = 'block';

  // Update color swatch
  const colorSwatch = document.getElementById('envmap-color-swatch');
  const colorValue = document.getElementById('envmap-color-value');
  if (colorSwatch && domeLight.color) {
    const r = Math.round((domeLight.color[0] || 1) * 255);
    const g = Math.round((domeLight.color[1] || 1) * 255);
    const b = Math.round((domeLight.color[2] || 1) * 255);
    colorSwatch.style.background = `rgb(${r}, ${g}, ${b})`;
    colorValue.textContent = `(${domeLight.color[0]?.toFixed(2) || 1}, ${domeLight.color[1]?.toFixed(2) || 1}, ${domeLight.color[2]?.toFixed(2) || 1})`;
  }

  // Update texture info
  const textureInfo = document.getElementById('envmap-texture-info');
  if (textureInfo) {
    if (domeLight.textureFile) {
      textureInfo.textContent = domeLight.textureFile;
    } else {
      textureInfo.textContent = 'None';
    }
  }

  // Show preview if we have valid image data
  const previewContainer = document.getElementById('envmap-preview-container');
  if (previewContainer && imageData && imageData.width > 0 && imageData.height > 0) {
    currentEnvmapImageData = imageData;
    previewContainer.style.display = 'block';

    // Update dimensions
    const dimensions = document.getElementById('envmap-dimensions');
    if (dimensions) {
      dimensions.textContent = `${imageData.width} x ${imageData.height}`;
    }

    // Render to canvas
    updateEnvmapPreviewCanvas();
  } else {
    if (previewContainer) {
      previewContainer.style.display = 'none';
    }
  }
}

/**
 * Hide envmap section
 */
function hideEnvmapSection() {
  const section = document.getElementById('envmap-section');
  if (section) {
    section.style.display = 'none';
  }
  currentEnvmapImageData = null;
}

/**
 * Update envmap preview canvas
 */
function updateEnvmapPreviewCanvas() {
  if (!currentEnvmapImageData) return;

  const canvas = document.getElementById('envmap-preview-canvas');
  if (!canvas) return;

  const ctx = canvas.getContext('2d');
  const width = currentEnvmapImageData.width;
  const height = currentEnvmapImageData.height;

  // Set canvas size (limit to reasonable preview size)
  const maxPreviewWidth = 512;
  const scale = width > maxPreviewWidth ? maxPreviewWidth / width : 1;
  canvas.width = Math.floor(width * scale);
  canvas.height = Math.floor(height * scale);

  // Create image data
  const imageData = ctx.createImageData(canvas.width, canvas.height);
  const data = imageData.data;

  // Determine if this is HDR data
  const srcData = currentEnvmapImageData.data;
  const channels = currentEnvmapImageData.channels || 3;
  const bytesPerPixel = srcData.length / (width * height);
  const isFloat = bytesPerPixel >= channels * 4;

  // Get source data as float
  let floatSrc;
  if (isFloat) {
    floatSrc = new Float32Array(srcData.buffer, srcData.byteOffset, width * height * channels);
  }

  // Find max value for HDR normalization
  let maxVal = 1;
  if (isFloat) {
    for (let i = 0; i < floatSrc.length; i++) {
      if (isFinite(floatSrc[i])) {
        maxVal = Math.max(maxVal, floatSrc[i]);
      }
    }
  }

  // Render with bilinear sampling for downscaled preview
  for (let dy = 0; dy < canvas.height; dy++) {
    for (let dx = 0; dx < canvas.width; dx++) {
      // Map to source coordinates
      const sx = dx / scale;
      const sy = dy / scale;
      const srcX = Math.min(width - 1, Math.floor(sx));
      const srcY = Math.min(height - 1, Math.floor(sy));
      const srcIdx = (srcY * width + srcX) * channels;

      let r, g, b;
      if (isFloat) {
        r = floatSrc[srcIdx] || 0;
        g = channels > 1 ? (floatSrc[srcIdx + 1] || 0) : r;
        b = channels > 2 ? (floatSrc[srcIdx + 2] || 0) : r;

        // Normalize and tone map
        r = r / maxVal;
        g = g / maxVal;
        b = b / maxVal;

        // Apply reinhard tone mapping
        r = r / (1 + r);
        g = g / (1 + g);
        b = b / (1 + b);

        // Gamma correction
        r = Math.pow(Math.max(0, r), 1/2.2);
        g = Math.pow(Math.max(0, g), 1/2.2);
        b = Math.pow(Math.max(0, b), 1/2.2);
      } else {
        r = (srcData[srcIdx] || 0) / 255;
        g = channels > 1 ? (srcData[srcIdx + 1] || 0) / 255 : r;
        b = channels > 2 ? (srcData[srcIdx + 2] || 0) / 255 : r;
      }

      const dstIdx = (dy * canvas.width + dx) * 4;
      data[dstIdx] = Math.floor(Math.min(255, r * 255));
      data[dstIdx + 1] = Math.floor(Math.min(255, g * 255));
      data[dstIdx + 2] = Math.floor(Math.min(255, b * 255));
      data[dstIdx + 3] = 255;
    }
  }

  ctx.putImageData(imageData, 0, 0);

  // Setup mouse handler for pixel info
  setupEnvmapCanvasMouseHandler(canvas);
}

/**
 * Setup mouse handler for envmap canvas
 */
function setupEnvmapCanvasMouseHandler(canvas) {
  if (envmapCanvasMouseHandlerSet) return;
  envmapCanvasMouseHandlerSet = true;

  const pixelInfo = document.getElementById('envmap-pixel-value');
  if (!pixelInfo) return;

  canvas.addEventListener('mousemove', (e) => {
    if (!currentEnvmapImageData) return;

    const rect = canvas.getBoundingClientRect();
    const width = currentEnvmapImageData.width;
    const height = currentEnvmapImageData.height;
    const channels = currentEnvmapImageData.channels || 3;

    const scaleX = width / rect.width;
    const scaleY = height / rect.height;

    const x = Math.floor((e.clientX - rect.left) * scaleX);
    const y = Math.floor((e.clientY - rect.top) * scaleY);

    if (x >= 0 && x < width && y >= 0 && y < height) {
      const srcData = currentEnvmapImageData.data;
      const bytesPerPixel = srcData.length / (width * height);
      const isFloat = bytesPerPixel >= channels * 4;

      let r, g, b;
      const srcIdx = (y * width + x) * channels;

      if (isFloat) {
        const floatSrc = new Float32Array(srcData.buffer, srcData.byteOffset, width * height * channels);
        r = floatSrc[srcIdx] || 0;
        g = channels > 1 ? (floatSrc[srcIdx + 1] || 0) : r;
        b = channels > 2 ? (floatSrc[srcIdx + 2] || 0) : r;
      } else {
        r = (srcData[srcIdx] || 0) / 255;
        g = channels > 1 ? (srcData[srcIdx + 1] || 0) / 255 : r;
        b = channels > 2 ? (srcData[srcIdx + 2] || 0) / 255 : r;
      }

      const formatVal = (v) => {
        if (v === 0) return '0';
        if (Math.abs(v) < 0.001 || Math.abs(v) >= 1000) {
          return v.toExponential(2);
        }
        return v.toFixed(3);
      };

      pixelInfo.textContent = `[${x},${y}] R:${formatVal(r)} G:${formatVal(g)} B:${formatVal(b)}`;
    }
  });

  canvas.addEventListener('mouseleave', () => {
    pixelInfo.textContent = '';
  });
}

/**
 * Export projected HDRI to file
 */
async function exportHDRI(format = 'exr') {
  if (!projectedHDRI) {
    console.warn('Project lights first');
    return;
  }

  console.log(`Exporting HDRI as ${format.toUpperCase()}...`);

  let blob;
  let filename;

  switch (format.toLowerCase()) {
    case 'exr': {
      const exrData = await writeEXR(projectedHDRI, {
        compression: 'zip',
        pixelType: 'half'
      });
      blob = new Blob([exrData], { type: 'application/octet-stream' });
      filename = 'light-projection.exr';
      break;
    }

    case 'hdr': {
      // Write Radiance HDR format
      const width = projectedHDRI.width;
      const height = projectedHDRI.height;
      const rgbe = new Uint8Array(width * height * 4);

      for (let i = 0; i < width * height; i++) {
        const r = projectedHDRI.data[i * 3];
        const g = projectedHDRI.data[i * 3 + 1];
        const b = projectedHDRI.data[i * 3 + 2];

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
      const headerBytes = new TextEncoder().encode(header);
      const totalLength = headerBytes.length + rgbe.length;
      const combined = new Uint8Array(totalLength);
      combined.set(headerBytes, 0);
      combined.set(rgbe, headerBytes.length);

      blob = new Blob([combined], { type: 'application/octet-stream' });
      filename = 'light-projection.hdr';
      break;
    }

    default:
      console.warn(`Unknown format: ${format}`);
      return;
  }

  // Download file
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);

  console.log(`Exported: ${filename} (${blob.size} bytes)`);
}

/**
 * Set HDRI resolution
 */
function setHDRIResolution(width, height) {
  hdriSettings.width = width;
  hdriSettings.height = height || Math.floor(width / 2);
  console.log(`HDRI resolution set to ${hdriSettings.width}x${hdriSettings.height}`);
}

/**
 * Set HDRI projection center
 */
function setHDRICenter(x, y, z) {
  hdriSettings.center = { x, y, z };
  console.log(`HDRI center set to (${x}, ${y}, ${z})`);

  // Schedule HDRI refresh if live update is enabled
  scheduleHDRIRefresh();
}

/**
 * Set HDRI max distance
 */
function setHDRIMaxDistance(distance) {
  hdriSettings.maxDistance = distance;
  console.log(`HDRI max distance set to ${distance}`);

  // Schedule HDRI refresh if live update is enabled
  scheduleHDRIRefresh();
}

/**
 * Get current HDRI data (for external use)
 */
function getProjectedHDRI() {
  return projectedHDRI;
}

// ============================================
// File Loading
// ============================================

let loader = null;

async function initLoader() {
  if (loader) return loader;

  loader = new TinyUSDZLoader();
  await loader.init({ useMemory64: false });
  loader.setMaxMemoryLimitMB(500);

  console.log('TinyUSDZ loader initialized');
  return loader;
}

/**
 * Load USD from ArrayBuffer
 */
async function loadUSDFromBuffer(buffer, filename) {
  try {
    await initLoader();

    const usd = await new Promise((resolve, reject) => {
      loader.parse(buffer, filename, resolve, reject);
    });

    if (!usd) {
      throw new Error('Failed to parse USD file');
    }

    console.log('USD file loaded successfully');
    console.log(`  Meshes: ${usd.numMeshes()}`);
    console.log(`  Materials: ${usd.numMaterials()}`);
    console.log(`  Lights: ${usd.numLights()}`);

    await loadLightsFromUSD(usd);

    return usd;
  } catch (error) {
    console.error('Error loading USD:', error);
    throw error;
  }
}

/**
 * Load USD from File object
 */
async function loadUSDFromFile(file) {
  const buffer = await file.arrayBuffer();
  return loadUSDFromBuffer(buffer, file.name);
}

/**
 * Load embedded USDA scene
 */
async function loadEmbeddedScene(sceneName = 'complete') {
  const usda = EMBEDDED_SCENES[sceneName];
  if (!usda) {
    console.error(`Unknown embedded scene: ${sceneName}`);
    return;
  }

  document.getElementById('currentFile').textContent = `Embedded: ${sceneName}`;
  document.getElementById('loadingIndicator').classList.add('active');

  try {
    const encoder = new TextEncoder();
    const buffer = encoder.encode(usda).buffer;
    await loadUSDFromBuffer(buffer, `${sceneName}.usda`);
  } catch (error) {
    console.error('Error loading embedded scene:', error);
    alert(`Failed to load embedded scene: ${error.message}`);
  } finally {
    if (window.hideLoadingIndicator) {
      window.hideLoadingIndicator();
    }
  }
}

/**
 * Focus camera on a specific light
 */
function focusOnLight(lightIndex) {
  if (lightIndex < 0 || lightIndex >= threeLights.length) return;

  const lightObj = threeLights[lightIndex];
  const position = new THREE.Vector3();

  if (lightObj.isGroup) {
    lightObj.getWorldPosition(position);
  } else if (lightObj.position) {
    position.copy(lightObj.position);
  }

  // Animate camera to look at the light
  const targetPosition = position.clone().add(new THREE.Vector3(3, 2, 3));

  // Simple camera animation
  const startPosition = camera.position.clone();
  const startTarget = controls.target.clone();
  const duration = 500;
  const startTime = Date.now();

  function animateCamera() {
    const elapsed = Date.now() - startTime;
    const t = Math.min(elapsed / duration, 1);
    const easeT = 1 - Math.pow(1 - t, 3); // Ease out cubic

    camera.position.lerpVectors(startPosition, targetPosition, easeT);
    controls.target.lerpVectors(startTarget, position, easeT);

    if (t < 1) {
      requestAnimationFrame(animateCamera);
    }
  }

  animateCamera();
}

/**
 * Toggle light on/off
 * @param {number} lightIndex - Index of the light to toggle
 * @param {boolean} [enabled] - Optional explicit state (true=on, false=off)
 * @returns {boolean} New state of the light
 */
function toggleLight(lightIndex, enabled) {
  if (lightIndex < 0 || lightIndex >= threeLights.length) {
    console.warn(`Invalid light index: ${lightIndex}`);
    return false;
  }

  const lightObj = threeLights[lightIndex];
  const helper = lightHelpers[lightIndex];

  // Determine new state
  const newState = enabled !== undefined ? enabled : !lightObj.visible;

  // Toggle the light object
  lightObj.visible = newState;

  // Toggle the helper if it exists
  if (helper) {
    helper.visible = newState;
  }

  // Update lightData state
  if (lightData[lightIndex]) {
    lightData[lightIndex].enabled = newState;
  }

  // Schedule HDRI refresh if live update is enabled
  scheduleHDRIRefresh();

  console.log(`Light ${lightIndex} (${lightData[lightIndex]?.name || 'unnamed'}): ${newState ? 'ON' : 'OFF'}`);

  return newState;
}

/**
 * Set all lights on or off
 * @param {boolean} enabled - true to turn all on, false to turn all off
 */
function setAllLightsEnabled(enabled) {
  for (let i = 0; i < threeLights.length; i++) {
    toggleLight(i, enabled);
  }

  // Notify UI to update
  if (window.updateLightListStates) {
    window.updateLightListStates();
  }
}

/**
 * Get light enabled state
 * @param {number} lightIndex - Index of the light
 * @returns {boolean} Whether the light is enabled
 */
function isLightEnabled(lightIndex) {
  if (lightIndex < 0 || lightIndex >= threeLights.length) {
    return false;
  }
  return threeLights[lightIndex].visible;
}

// ============================================
// Light Property Editing Functions
// ============================================

/**
 * Get the actual Three.js light from a threeLights entry
 * (handles Groups containing SpotLights/DirectionalLights)
 * @param {THREE.Object3D} lightObj - Light object (may be Group or Light)
 * @returns {THREE.Light|null} The actual light object
 */
function getActualLight(lightObj) {
  if (!lightObj) return null;
  if (lightObj.isLight) return lightObj;
  if (lightObj.isGroup) {
    return lightObj.children.find(c => c.isLight) || null;
  }
  return null;
}

/**
 * Set color of the currently selected light
 * @param {number} r - Red component (0-1)
 * @param {number} g - Green component (0-1)
 * @param {number} b - Blue component (0-1)
 */
function setSelectedLightColor(r, g, b) {
  if (selectedLight3DIndex < 0 || selectedLight3DIndex >= threeLights.length) {
    return;
  }

  const lightObj = threeLights[selectedLight3DIndex];
  const threeLight = getActualLight(lightObj);
  const usdLight = lightData[selectedLight3DIndex];

  // Update Three.js light color
  if (threeLight && threeLight.color) {
    threeLight.color.setRGB(r, g, b);
  }

  // Update lightData
  if (usdLight) {
    usdLight.color = [r, g, b];
  }

  // Update the helper visualization
  updateLightHelperColor(selectedLight3DIndex, r, g, b);

  // Update the light list swatch
  updateLightListSwatch(selectedLight3DIndex, r, g, b);

  // Schedule HDRI refresh if live update is enabled
  scheduleHDRIRefresh();

  console.log(`Light ${selectedLight3DIndex} color set to RGB(${r.toFixed(2)}, ${g.toFixed(2)}, ${b.toFixed(2)})`);
}

/**
 * Set intensity of the currently selected light
 * @param {number} intensity - Light intensity value
 */
function setSelectedLightIntensity(intensity) {
  if (selectedLight3DIndex < 0 || selectedLight3DIndex >= threeLights.length) {
    return;
  }

  const lightObj = threeLights[selectedLight3DIndex];
  const threeLight = getActualLight(lightObj);
  const usdLight = lightData[selectedLight3DIndex];

  // Calculate effective intensity with exposure
  const exposure = usdLight?.exposure || 0;
  const effectiveIntensity = intensity * Math.pow(2, exposure);

  // Update Three.js light intensity
  if (threeLight && threeLight.intensity !== undefined) {
    threeLight.intensity = effectiveIntensity;
  }

  // Update lightData
  if (usdLight) {
    usdLight.intensity = intensity;
  }

  // Update light list details
  updateLightListDetails(selectedLight3DIndex);

  // Schedule HDRI refresh if live update is enabled
  scheduleHDRIRefresh();

  console.log(`Light ${selectedLight3DIndex} intensity set to ${intensity} (effective: ${effectiveIntensity.toFixed(2)})`);
}

/**
 * Set exposure of the currently selected light
 * @param {number} exposure - Exposure value in EV
 */
function setSelectedLightExposure(exposure) {
  if (selectedLight3DIndex < 0 || selectedLight3DIndex >= threeLights.length) {
    return;
  }

  const lightObj = threeLights[selectedLight3DIndex];
  const threeLight = getActualLight(lightObj);
  const usdLight = lightData[selectedLight3DIndex];

  // Calculate effective intensity with new exposure
  const baseIntensity = usdLight?.intensity || 1;
  const effectiveIntensity = baseIntensity * Math.pow(2, exposure);

  // Update Three.js light intensity
  if (threeLight && threeLight.intensity !== undefined) {
    threeLight.intensity = effectiveIntensity;
  }

  // Update lightData
  if (usdLight) {
    usdLight.exposure = exposure;
  }

  // Update light list details
  updateLightListDetails(selectedLight3DIndex);

  // Schedule HDRI refresh if live update is enabled
  scheduleHDRIRefresh();

  console.log(`Light ${selectedLight3DIndex} exposure set to ${exposure} EV (effective intensity: ${effectiveIntensity.toFixed(2)})`);
}

/**
 * Set position of the currently selected light
 * @param {number} x - X position
 * @param {number} y - Y position
 * @param {number} z - Z position
 */
function setSelectedLightPosition(x, y, z) {
  if (selectedLight3DIndex < 0 || selectedLight3DIndex >= threeLights.length) {
    return;
  }

  const lightObj = threeLights[selectedLight3DIndex];
  const threeLight = getActualLight(lightObj);
  const usdLight = lightData[selectedLight3DIndex];

  // Update Three.js light position
  if (threeLight) {
    threeLight.position.set(x, y, z);
    threeLight.updateMatrixWorld(true);
  }

  // Update lightData
  if (usdLight) {
    usdLight.position = [x, y, z];
  }

  // Update helper position
  const helper = lightHelpers[selectedLight3DIndex];
  if (helper) {
    if (threeLight && threeLight.isSpotLight) {
      // Update sphere mesh position for SpotLight helpers
      helper.traverse((child) => {
        if (child.isMesh && child.geometry.type === 'SphereGeometry') {
          child.position.set(x, y, z);
        }
      });
    } else {
      helper.position.set(x, y, z);
    }
  }

  // Schedule HDRI refresh if live update is enabled
  scheduleHDRIRefresh();

  console.log(`Light ${selectedLight3DIndex} position set to (${x.toFixed(2)}, ${y.toFixed(2)}, ${z.toFixed(2)})`);
}

/**
 * Set rotation of the currently selected light (in degrees)
 * @param {number} x - X rotation in degrees
 * @param {number} y - Y rotation in degrees
 * @param {number} z - Z rotation in degrees
 */
function setSelectedLightRotation(x, y, z) {
  if (selectedLight3DIndex < 0 || selectedLight3DIndex >= threeLights.length) {
    return;
  }

  const lightObj = threeLights[selectedLight3DIndex];
  const threeLight = getActualLight(lightObj);
  const usdLight = lightData[selectedLight3DIndex];

  // Convert degrees to radians
  const xRad = x * Math.PI / 180;
  const yRad = y * Math.PI / 180;
  const zRad = z * Math.PI / 180;

  // Update Three.js light rotation
  if (threeLight) {
    // For lights in groups, rotate the group
    if (lightObj.isGroup) {
      lightObj.rotation.set(xRad, yRad, zRad, 'XYZ');
      lightObj.updateMatrixWorld(true);
    } else {
      threeLight.rotation.set(xRad, yRad, zRad, 'XYZ');
      threeLight.updateMatrixWorld(true);
    }
  }

  // Update lightData (store in degrees)
  if (usdLight) {
    usdLight.rotation = [x, y, z];
  }

  // Update helper rotation
  const helper = lightHelpers[selectedLight3DIndex];
  if (helper && !threeLight?.isSpotLight) {
    helper.rotation.set(xRad, yRad, zRad, 'XYZ');
  }

  // Schedule HDRI refresh if live update is enabled
  scheduleHDRIRefresh();

  console.log(`Light ${selectedLight3DIndex} rotation set to (${x.toFixed(1)}°, ${y.toFixed(1)}°, ${z.toFixed(1)}°)`);
}

/**
 * Set cone angle of the currently selected light
 * @param {number} angle - Cone angle in degrees (1-180)
 */
function setSelectedLightConeAngle(angle) {
  if (selectedLight3DIndex < 0 || selectedLight3DIndex >= threeLights.length) {
    return;
  }

  const lightObj = threeLights[selectedLight3DIndex];
  const threeLight = getActualLight(lightObj);
  const usdLight = lightData[selectedLight3DIndex];

  // Clamp angle
  angle = Math.max(1, Math.min(180, angle));

  // Update Three.js SpotLight angle (Three.js uses half-angle in radians)
  if (threeLight && threeLight.isSpotLight) {
    threeLight.angle = (angle / 2) * Math.PI / 180;
  }

  // Update lightData
  if (usdLight) {
    usdLight.coneAngle = angle;
    usdLight.shapingConeAngle = angle / 2; // USD uses half-angle
  }

  // Update helper
  const helper = lightHelpers[selectedLight3DIndex];
  if (helper) {
    helper.traverse((child) => {
      if (child.type === 'SpotLightHelper' && child.update) {
        child.update();
      }
    });
  }

  // Update light list details
  updateLightListDetails(selectedLight3DIndex);

  // Schedule HDRI refresh if live update is enabled
  scheduleHDRIRefresh();

  console.log(`Light ${selectedLight3DIndex} cone angle set to ${angle}°`);
}

/**
 * Set cone softness (penumbra) of the currently selected light
 * @param {number} softness - Softness value (0-1)
 */
function setSelectedLightConeSoftness(softness) {
  if (selectedLight3DIndex < 0 || selectedLight3DIndex >= threeLights.length) {
    return;
  }

  const lightObj = threeLights[selectedLight3DIndex];
  const threeLight = getActualLight(lightObj);
  const usdLight = lightData[selectedLight3DIndex];

  // Clamp softness
  softness = Math.max(0, Math.min(1, softness));

  // Update Three.js SpotLight penumbra
  if (threeLight && threeLight.isSpotLight) {
    threeLight.penumbra = softness;
  }

  // Update lightData
  if (usdLight) {
    usdLight.coneSoftness = softness;
    usdLight.shapingConeSoftness = softness;
  }

  // Update helper
  const helper = lightHelpers[selectedLight3DIndex];
  if (helper) {
    helper.traverse((child) => {
      if (child.type === 'SpotLightHelper' && child.update) {
        child.update();
      }
    });
  }

  // Schedule HDRI refresh if live update is enabled
  scheduleHDRIRefresh();

  console.log(`Light ${selectedLight3DIndex} cone softness set to ${softness.toFixed(2)}`);
}

/**
 * Update the light helper color visualization
 */
function updateLightHelperColor(lightIndex, r, g, b) {
  const helper = lightHelpers[lightIndex];
  if (!helper) return;

  const color = new THREE.Color(r, g, b);

  helper.traverse((child) => {
    // Update SpotLightHelper (use type check since no isSpotLightHelper flag exists)
    if (child.type === 'SpotLightHelper') {
      child.update();
    }

    if (child.material && !child.material._isSelected) {
      // Store original color for selection state restoration
      child.material._baseColor = color.clone();
      child.material.color.copy(color);
    }
  });
}

/**
 * Update the light list item's color swatch
 */
function updateLightListSwatch(lightIndex, r, g, b) {
  const lightList = document.getElementById('lightList');
  if (!lightList) return;

  const items = lightList.querySelectorAll('.light-item');
  if (lightIndex >= 0 && lightIndex < items.length) {
    const swatch = items[lightIndex].querySelector('.light-color-swatch');
    if (swatch) {
      const hex = '#' +
        Math.round(r * 255).toString(16).padStart(2, '0') +
        Math.round(g * 255).toString(16).padStart(2, '0') +
        Math.round(b * 255).toString(16).padStart(2, '0');
      swatch.style.backgroundColor = hex;
    }
  }
}

/**
 * Update the light list item's details text
 */
function updateLightListDetails(lightIndex) {
  const lightList = document.getElementById('lightList');
  if (!lightList) return;

  const items = lightList.querySelectorAll('.light-item');
  if (lightIndex < 0 || lightIndex >= items.length) return;

  const light = lightData[lightIndex];
  if (!light) return;

  let details = `Intensity: ${(light.intensity || 1).toFixed(2)}`;
  if (light.exposure && light.exposure !== 0) {
    details += ` | Exp: ${light.exposure.toFixed(1)} EV`;
  }
  if (light.radius && light.type !== 'distant' && light.type !== 'dome') {
    details += ` | R: ${light.radius.toFixed(2)}`;
  }
  if (light.type === 'rect' && light.width && light.height) {
    details += ` | ${light.width.toFixed(1)}x${light.height.toFixed(1)}`;
  }
  if (light.shapingConeAngle && light.shapingConeAngle < 90) {
    details += ` | Cone: ${light.shapingConeAngle.toFixed(0)}`;
  }

  const detailsEl = items[lightIndex].querySelector('.light-details');
  if (detailsEl) {
    detailsEl.textContent = details;
  }
}

// ============================================
// Event Handlers
// ============================================

// Handle file upload from dialog
window.addEventListener('loadUSDFile', async (event) => {
  const { file } = event.detail;

  try {
    await loadUSDFromFile(file);
  } catch (error) {
    alert(`Failed to load USD file: ${error.message}`);
  } finally {
    if (window.hideLoadingIndicator) {
      window.hideLoadingIndicator();
    }
  }
});

// Expose functions to window
window.loadEmbeddedScene = loadEmbeddedScene;
window.clearLights = clearLights;
window.focusOnLight = focusOnLight;
window.toggleLight = toggleLight;
window.setAllLightsEnabled = setAllLightsEnabled;
window.isLightEnabled = isLightEnabled;
window.setToneMapping = setToneMapping;
window.setExposure = setExposure;
window.setGamma = setGamma;

// Spectral functions
window.setSpectralMode = setSpectralMode;
window.setMonochromeWavelength = setMonochromeWavelength;
window.selectLightForSpectral = selectLightForSpectral;
window.wavelengthToRGB = wavelengthToRGB;
window.spectralToRGB = spectralToRGB;
window.applyDemoSpectralData = applyDemoSpectralData;
window.applyBlackbodyToSelected = applyBlackbodyToSelected;
window.generateBlackbodySpectrum = generateBlackbodySpectrum;

// HDRI Projection functions
window.projectLightsToHDRI = projectLightsToHDRI;
window.toggleHDRIPreview = toggleHDRIPreview;
window.updateHDRIPreviewCanvas = updateHDRIPreviewCanvas;
window.applyHDRIToScene = applyHDRIToScene;
window.removeHDRIFromScene = removeHDRIFromScene;
window.setLightingMode = setLightingMode;
window.getLightingMode = getLightingMode;
window.toggleLightingMode = toggleLightingMode;
window.setShowEnvmapBackground = setShowEnvmapBackground;
window.getShowEnvmapBackground = getShowEnvmapBackground;
window.exportHDRI = exportHDRI;
window.setHDRIResolution = setHDRIResolution;
window.setHDRICenter = setHDRICenter;
window.setHDRIMaxDistance = setHDRIMaxDistance;
window.getProjectedHDRI = getProjectedHDRI;
window.refreshHDRIProjection = refreshHDRIProjection;
window.setHDRILiveUpdate = setHDRILiveUpdate;
window.isHDRILiveUpdateEnabled = isHDRILiveUpdateEnabled;

// HDRI Locator functions
window.createHDRILocator = createHDRILocator;
window.toggleHDRILocator = toggleHDRILocator;
window.setHDRILocatorPosition = setHDRILocatorPosition;
window.getHDRICenter = getHDRICenter;
window.updateHDRIRangeIndicator = updateHDRIRangeIndicator;

// Light Selection and Transform functions
window.selectLight3D = selectLight3D;
window.setLightTransformMode = setLightTransformMode;
window.getSelectedLight3DIndex = () => selectedLight3DIndex;

// Debug functions for transform controls
window.getLightTransformControls = () => lightTransformControls;
window.getThreeLights = () => threeLights;
window.getLightHelpers = () => lightHelpers;
window.debugTransformControls = () => {
  console.log('=== Transform Controls Debug ===');
  console.log('selectedLight3DIndex:', selectedLight3DIndex);
  console.log('lightTransformControls:', lightTransformControls);
  if (lightTransformControls) {
    console.log('  - enabled:', lightTransformControls.enabled);
    console.log('  - visible:', lightTransformControls.visible);
    console.log('  - mode:', lightTransformControls.mode);
    console.log('  - object:', lightTransformControls.object);
    console.log('  - parent (in scene?):', lightTransformControls.parent);
  }
  if (selectedLight3DIndex >= 0) {
    const light = threeLights[selectedLight3DIndex];
    console.log('Selected light:', light);
    if (light) {
      console.log('  - position:', light.position?.clone());
      console.log('  - visible:', light.visible);
      console.log('  - parent:', light.parent);
    }
  }
  console.log('threeLights count:', threeLights.length);
  console.log('lightHelpers count:', lightHelpers.length);
  return { lightTransformControls, selectedLight3DIndex, threeLights, lightHelpers };
};

// Light Property Editing functions
window.setSelectedLightColor = setSelectedLightColor;
window.setSelectedLightIntensity = setSelectedLightIntensity;
window.setSelectedLightExposure = setSelectedLightExposure;
window.setSelectedLightPosition = setSelectedLightPosition;
window.setSelectedLightRotation = setSelectedLightRotation;
window.setSelectedLightConeAngle = setSelectedLightConeAngle;
window.setSelectedLightConeSoftness = setSelectedLightConeSoftness;

// Mesh Selection and Transform functions
window.selectMesh = selectMesh;
window.deselectMesh = deselectMesh;
window.setMeshTransformMode = setMeshTransformMode;
window.getSelectedMesh = () => selectedMesh;
window.getMeshTransformControls = () => meshTransformControls;
window.getSelectableMeshes = () => selectableMeshes;

// Selection Mode functions
window.setSelectionMode = setSelectionMode;
window.getSelectionMode = () => selectionMode;

// Envmap preview functions
window.showEnvmapSection = showEnvmapSection;
window.hideEnvmapSection = hideEnvmapSection;
window.updateEnvmapPreviewCanvas = updateEnvmapPreviewCanvas;

// Window resize handler
window.addEventListener('resize', () => {
  camera.aspect = window.innerWidth / window.innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(window.innerWidth, window.innerHeight);
  composer.setSize(window.innerWidth, window.innerHeight);
});

// ============================================
// Animation Loop
// ============================================

let lastTime = performance.now();
let frameCount = 0;
let fps = 60;

function animate() {
  requestAnimationFrame(animate);

  // Update FPS
  frameCount++;
  const currentTime = performance.now();
  if (currentTime - lastTime >= 1000) {
    fps = Math.round(frameCount * 1000 / (currentTime - lastTime));
    document.getElementById('statFPS').textContent = fps;
    frameCount = 0;
    lastTime = currentTime;
  }

  // Rotate torus for visual interest
  torus.rotation.x += 0.005;
  torus.rotation.y += 0.008;

  // Update controls
  controls.update();

  // Update light transform controls
  if (lightTransformControls && lightTransformControls.enabled) {
    lightTransformControls.update();
  }

  // Update light helpers that need updating (including SpotLightHelper children)
  for (const helper of lightHelpers) {
    if (helper) {
      helper.traverse((child) => {
        if (child.type === 'SpotLightHelper' && child.update) {
          child.update();
        }
      });
    }
  }

  // Render using composer for ACES 2.0, otherwise use standard renderer
  if (usePostProcessing) {
    composer.render();
  } else {
    renderer.render(scene, camera);
  }
}

// ============================================
// Initialize
// ============================================

async function init() {
  console.log('Initializing UsdLux demo...');

  // Initialize spectral canvas
  initSpectralCanvas();

  // Start animation loop
  animate();

  // Load default embedded scene
  await loadEmbeddedScene('complete');

  console.log('UsdLux demo initialized');
}

init();
