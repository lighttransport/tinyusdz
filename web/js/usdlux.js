/**
 * TinyUSDZ UsdLux Light Demo
 * Visualizes USD lights using Three.js
 */

import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { RectAreaLightHelper } from 'three/examples/jsm/helpers/RectAreaLightHelper.js';
import { RectAreaLightUniformsLib } from 'three/examples/jsm/lights/RectAreaLightUniformsLib.js';
import { EffectComposer } from 'three/examples/jsm/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/examples/jsm/postprocessing/RenderPass.js';
import { ShaderPass } from 'three/examples/jsm/postprocessing/ShaderPass.js';
import { OutputPass } from 'three/examples/jsm/postprocessing/OutputPass.js';
import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';

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

  // Update display if a light is selected
  if (selectedLightIndex >= 0 && selectedLightIndex < lightData.length) {
    drawSpectralCurve(lightData[selectedLightIndex].spectralEmission);
    updateSpectralInfo(lightData[selectedLightIndex]);
  }

  // Update colors if in spectral mode
  if (spectralMode === 'spectral') {
    updateLightColors();
  }

  console.log('Demo spectral data applied to lights');
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
        double3 xformOp:translate = (0, 6, 0)
        double3 xformOp:rotateXYZ = (-90, 0, 0)
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
scene.add(box);

// Ambient light (low intensity as fallback)
const ambientLight = new THREE.AmbientLight(0x404050, 0.3);
scene.add(ambientLight);

// ============================================
// Light Management
// ============================================

// Store created Three.js lights and helpers
const threeLights = [];
const lightHelpers = [];
const lightData = []; // Store RenderLight data

/**
 * Clear all USD lights from the scene
 */
function clearLights() {
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
}

/**
 * Create a light helper/visualizer for finite lights
 * @param {THREE.Light} light - Three.js light
 * @param {Object} usdLight - USD light data
 * @returns {THREE.Object3D|null} Helper object
 */
function createLightHelper(light, usdLight) {
  const type = usdLight.type || 'unknown';

  // Create position marker sphere for finite lights
  if (type === 'point' || type === 'sphere') {
    const helperGeom = new THREE.SphereGeometry(usdLight.radius || 0.1, 16, 16);
    const helperMat = new THREE.MeshBasicMaterial({
      color: new THREE.Color(usdLight.color?.[0] || 1, usdLight.color?.[1] || 1, usdLight.color?.[2] || 1),
      transparent: true,
      opacity: 0.6,
      wireframe: true
    });
    const helper = new THREE.Mesh(helperGeom, helperMat);
    helper.position.copy(light.position);
    return helper;
  }

  if (type === 'disk') {
    const helperGeom = new THREE.CircleGeometry(usdLight.radius || 0.5, 32);
    const helperMat = new THREE.MeshBasicMaterial({
      color: new THREE.Color(usdLight.color?.[0] || 1, usdLight.color?.[1] || 1, usdLight.color?.[2] || 1),
      transparent: true,
      opacity: 0.5,
      side: THREE.DoubleSide
    });
    const helper = new THREE.Mesh(helperGeom, helperMat);
    if (light.parent) {
      helper.position.copy(light.parent.position);
      helper.quaternion.copy(light.parent.quaternion);
    }
    return helper;
  }

  if (type === 'rect' && light.isRectAreaLight) {
    return new RectAreaLightHelper(light);
  }

  if (type === 'cylinder') {
    const helperGeom = new THREE.CylinderGeometry(
      usdLight.radius || 0.1,
      usdLight.radius || 0.1,
      usdLight.length || 1,
      16
    );
    const helperMat = new THREE.MeshBasicMaterial({
      color: new THREE.Color(usdLight.color?.[0] || 1, usdLight.color?.[1] || 1, usdLight.color?.[2] || 1),
      transparent: true,
      opacity: 0.5,
      wireframe: true
    });
    const helper = new THREE.Mesh(helperGeom, helperMat);
    if (light.parent) {
      helper.position.copy(light.parent.position);
      helper.quaternion.copy(light.parent.quaternion);
    }
    return helper;
  }

  if (type === 'distant') {
    // Arrow helper for directional light
    const dir = new THREE.Vector3(0, -1, 0);
    if (light.target) {
      dir.subVectors(light.target.position, light.position).normalize();
    }
    const helper = new THREE.ArrowHelper(dir, light.position, 3, light.color.getHex(), 0.5, 0.3);
    return helper;
  }

  // SpotLight helper for lights with shaping
  if (light.isSpotLight) {
    return new THREE.SpotLightHelper(light);
  }

  // Point light helper
  if (light.isPointLight) {
    return new THREE.PointLightHelper(light, usdLight.radius || 0.2);
  }

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

  // Get position from transform or position array
  const position = new THREE.Vector3(
    usdLight.position?.[0] || 0,
    usdLight.position?.[1] || 0,
    usdLight.position?.[2] || 0
  );

  // Extract position from transform matrix if available
  if (usdLight.transform && usdLight.transform.length === 16) {
    position.set(
      usdLight.transform[12],
      usdLight.transform[13],
      usdLight.transform[14]
    );
  }

  let light = null;
  let lightGroup = null;

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
        lightGroup.position.copy(position);
        lightGroup.add(light);

        // Position target based on direction
        const targetOffset = new THREE.Vector3(0, -5, 0);
        if (usdLight.direction) {
          targetOffset.set(
            usdLight.direction[0] * 5,
            usdLight.direction[1] * 5,
            usdLight.direction[2] * 5
          );
        }
        light.target.position.copy(targetOffset);
        lightGroup.add(light.target);

        // Apply rotation from transform
        if (usdLight.transform && usdLight.transform.length === 16) {
          const matrix = new THREE.Matrix4();
          matrix.fromArray(usdLight.transform);
          const rotation = new THREE.Euler();
          rotation.setFromRotationMatrix(matrix);
          lightGroup.rotation.copy(rotation);
        }
      } else {
        light = new THREE.PointLight(color, intensity);
        light.decay = 2;
        light.distance = 0;
        light.position.copy(position);
      }
      break;
    }

    case 'distant': {
      light = new THREE.DirectionalLight(color, intensity);

      // Position far away to simulate infinite distance
      light.position.set(10, 10, 10);

      // Apply rotation from transform to determine direction
      if (usdLight.transform && usdLight.transform.length === 16) {
        const matrix = new THREE.Matrix4();
        matrix.fromArray(usdLight.transform);
        const direction = new THREE.Vector3(0, 0, -1);
        direction.applyMatrix4(matrix).normalize();
        light.position.copy(direction.multiplyScalar(-10));
      }

      light.target.position.set(0, 0, 0);
      scene.add(light.target);
      break;
    }

    case 'rect': {
      const width = usdLight.width || 1;
      const height = usdLight.height || 1;
      light = new THREE.RectAreaLight(color, intensity, width, height);

      lightGroup = new THREE.Group();
      lightGroup.position.copy(position);
      lightGroup.add(light);

      // Apply rotation from transform
      if (usdLight.transform && usdLight.transform.length === 16) {
        const matrix = new THREE.Matrix4();
        matrix.fromArray(usdLight.transform);
        const rotation = new THREE.Euler();
        rotation.setFromRotationMatrix(matrix);
        lightGroup.rotation.copy(rotation);
      }
      break;
    }

    case 'disk': {
      // Three.js doesn't have disk light, approximate with point light
      light = new THREE.PointLight(color, intensity);
      light.decay = 2;
      light.distance = 0;

      lightGroup = new THREE.Group();
      lightGroup.position.copy(position);
      lightGroup.add(light);

      if (usdLight.transform && usdLight.transform.length === 16) {
        const matrix = new THREE.Matrix4();
        matrix.fromArray(usdLight.transform);
        const rotation = new THREE.Euler();
        rotation.setFromRotationMatrix(matrix);
        lightGroup.rotation.copy(rotation);
      }
      break;
    }

    case 'cylinder': {
      // Approximate with point light
      light = new THREE.PointLight(color, intensity);
      light.decay = 2;
      light.distance = 0;

      lightGroup = new THREE.Group();
      lightGroup.position.copy(position);
      lightGroup.add(light);

      if (usdLight.transform && usdLight.transform.length === 16) {
        const matrix = new THREE.Matrix4();
        matrix.fromArray(usdLight.transform);
        const rotation = new THREE.Euler();
        rotation.setFromRotationMatrix(matrix);
        lightGroup.rotation.copy(rotation);
      }
      break;
    }

    case 'dome': {
      // Use hemisphere light for dome/environment
      light = new THREE.HemisphereLight(color, new THREE.Color(0x444444), intensity);
      break;
    }

    default:
      console.warn(`Unsupported light type: ${type}`);
      return null;
  }

  if (!light) return null;

  // Configure shadows
  if (light.castShadow !== undefined && usdLight.shadowEnable !== false) {
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
 * Load lights from USD data
 * @param {Object} usdLoader - TinyUSDZ loader instance
 */
function loadLightsFromUSD(usdLoader) {
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
    lightData.push(usdLight);

    const threeLight = convertUSDLightToThreeJS(usdLight);
    if (threeLight) {
      scene.add(threeLight);
      threeLights.push(threeLight);

      // Create helper for visualization
      const actualLight = threeLight.isGroup ?
        threeLight.children.find(c => c.isLight) : threeLight;

      if (actualLight) {
        const helper = createLightHelper(actualLight, usdLight);
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

    loadLightsFromUSD(usd);

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

  // Update light helpers that need updating
  for (const helper of lightHelpers) {
    if (helper.update) {
      helper.update();
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
