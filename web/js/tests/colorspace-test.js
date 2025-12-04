/**
 * Colorspace Conversion Tests (Pure Node.js)
 *
 * Tests various colorspace conversions without requiring WebGL/WebGPU.
 * Validates against known reference values from MaterialX specification.
 */

// Reference colorspace conversion functions
// Based on MaterialX specification and OpenColorIO

/**
 * sRGB to Linear conversion
 */
function srgbToLinear(value) {
  if (value <= 0.04045) {
    return value / 12.92;
  }
  return Math.pow((value + 0.055) / 1.055, 2.4);
}

/**
 * Linear to sRGB conversion
 */
function linearToSrgb(value) {
  if (value <= 0.0031308) {
    return value * 12.92;
  }
  return 1.055 * Math.pow(value, 1.0 / 2.4) - 0.055;
}

/**
 * Convert color array between sRGB and Linear
 */
function convertSrgbLinear(color, toLinear = true) {
  const fn = toLinear ? srgbToLinear : linearToSrgb;
  return color.map(fn);
}

/**
 * Simple Rec.709 matrix (for demonstration)
 * In reality, this should match MaterialX's exact matrices
 */
const REC709_TO_XYZ = [
  [0.4124564, 0.3575761, 0.1804375],
  [0.2126729, 0.7151522, 0.0721750],
  [0.0193339, 0.1191920, 0.9503041]
];

const XYZ_TO_REC709 = [
  [ 3.2404542, -1.5371385, -0.4985314],
  [-0.9692660,  1.8760108,  0.0415560],
  [ 0.0556434, -0.2040259,  1.0572252]
];

/**
 * Matrix multiply for color conversion
 */
function matrixMultiply(matrix, color) {
  return matrix.map(row =>
    row.reduce((sum, val, i) => sum + val * color[i], 0)
  );
}

/**
 * Compare two colors with tolerance
 */
function colorsMatch(c1, c2, tolerance = 0.001) {
  return c1.every((val, i) => Math.abs(val - c2[i]) < tolerance);
}

/**
 * Format color for display
 */
function formatColor(color) {
  return `[${color.map(v => v.toFixed(6)).join(', ')}]`;
}

// Test suite
const tests = [
  {
    name: 'sRGB to Linear - Mid Gray',
    input: [0.5, 0.5, 0.5],
    operation: 'srgb_to_linear',
    expected: [0.214041, 0.214041, 0.214041],
    tolerance: 0.001
  },
  {
    name: 'sRGB to Linear - Black',
    input: [0.0, 0.0, 0.0],
    operation: 'srgb_to_linear',
    expected: [0.0, 0.0, 0.0],
    tolerance: 0.001
  },
  {
    name: 'sRGB to Linear - White',
    input: [1.0, 1.0, 1.0],
    operation: 'srgb_to_linear',
    expected: [1.0, 1.0, 1.0],
    tolerance: 0.001
  },
  {
    name: 'sRGB to Linear - Red',
    input: [1.0, 0.0, 0.0],
    operation: 'srgb_to_linear',
    expected: [1.0, 0.0, 0.0],
    tolerance: 0.001
  },
  {
    name: 'Linear to sRGB - Mid Gray',
    input: [0.214041, 0.214041, 0.214041],
    operation: 'linear_to_srgb',
    expected: [0.5, 0.5, 0.5],
    tolerance: 0.001
  },
  {
    name: 'sRGB to Linear - Quarter Gray',
    input: [0.25, 0.25, 0.25],
    operation: 'srgb_to_linear',
    expected: [0.050876, 0.050876, 0.050876],
    tolerance: 0.001
  },
  {
    name: 'sRGB to Linear - Three Quarter Gray',
    input: [0.75, 0.75, 0.75],
    operation: 'srgb_to_linear',
    expected: [0.522522, 0.522522, 0.522522],
    tolerance: 0.001
  },
  {
    name: 'sRGB to Linear - Orange',
    input: [1.0, 0.5, 0.0],
    operation: 'srgb_to_linear',
    expected: [1.0, 0.214041, 0.0],
    tolerance: 0.001
  },
  {
    name: 'Rec.709 to XYZ - White',
    input: [1.0, 1.0, 1.0],
    operation: 'rec709_to_xyz',
    expected: [0.9505, 1.0000, 1.0890], // D65 white point
    tolerance: 0.01 // Looser tolerance for matrix ops
  },
];

/**
 * Execute a test
 */
function runTest(test) {
  let result;

  switch (test.operation) {
    case 'srgb_to_linear':
      result = convertSrgbLinear(test.input, true);
      break;
    case 'linear_to_srgb':
      result = convertSrgbLinear(test.input, false);
      break;
    case 'rec709_to_xyz':
      result = matrixMultiply(REC709_TO_XYZ, test.input);
      break;
    default:
      throw new Error(`Unknown operation: ${test.operation}`);
  }

  const passed = colorsMatch(result, test.expected, test.tolerance);

  return {
    name: test.name,
    input: test.input,
    expected: test.expected,
    result,
    passed,
    error: passed ? 0 : Math.max(...result.map((v, i) => Math.abs(v - test.expected[i])))
  };
}

/**
 * Run all tests
 */
function runAllTests() {
  console.log('🎨 MaterialX Colorspace Conversion Tests\n');
  console.log('='.repeat(80));

  const results = tests.map(runTest);

  results.forEach(result => {
    const icon = result.passed ? '✓' : '✗';
    const status = result.passed ? '\x1b[32mPASSED\x1b[0m' : '\x1b[31mFAILED\x1b[0m';

    console.log(`\n${icon} ${result.name} - ${status}`);
    console.log(`  Input:    ${formatColor(result.input)}`);
    console.log(`  Expected: ${formatColor(result.expected)}`);
    console.log(`  Result:   ${formatColor(result.result)}`);

    if (!result.passed) {
      console.log(`  \x1b[31mMax Error: ${result.error.toFixed(6)}\x1b[0m`);
    }
  });

  console.log('\n' + '='.repeat(80));

  const passed = results.filter(r => r.passed).length;
  const failed = results.filter(r => !r.passed).length;

  console.log(`\n✓ Passed: ${passed}`);
  console.log(`✗ Failed: ${failed}`);
  console.log(`Total: ${results.length}`);
  console.log('='.repeat(80));

  return failed === 0;
}

// Run tests if executed directly
if (import.meta.url === `file://${process.argv[1]}`) {
  const success = runAllTests();
  process.exit(success ? 0 : 1);
}

export { runAllTests, runTest, convertSrgbLinear, srgbToLinear, linearToSrgb };
