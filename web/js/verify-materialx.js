#!/usr/bin/env node
/**
 * MaterialX Verification CLI Tool
 *
 * Renders materials with headless Chrome (using SwiftShader fallback)
 * and compares against reference implementations.
 */

import { program } from 'commander';
import puppeteer from 'puppeteer';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import { PNG } from 'pngjs';
import pixelmatch from 'pixelmatch';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// Configuration
const CHROME_PATH = process.env.CHROME_PATH || '/opt/google/chrome/chrome';
const OUTPUT_DIR = path.join(__dirname, 'verification-results');
const SCREENSHOTS_DIR = path.join(OUTPUT_DIR, 'screenshots');
const DIFFS_DIR = path.join(OUTPUT_DIR, 'diffs');

// Ensure output directories exist
[OUTPUT_DIR, SCREENSHOTS_DIR, DIFFS_DIR].forEach(dir => {
  if (!fs.existsSync(dir)) {
    fs.mkdirSync(dir, { recursive: true });
  }
});

/**
 * Launch headless Chrome with SwiftShader fallback
 */
async function launchBrowser(useGPU = false) {
  const args = [
    '--no-sandbox',
    '--disable-setuid-sandbox',
    '--disable-dev-shm-usage',
    '--disable-web-security', // Allow loading local files
  ];

  if (!useGPU) {
    // Force SwiftShader (software rendering)
    args.push(
      '--disable-gpu',
      '--use-gl=swiftshader',
      '--use-angle=swiftshader-webgl',
      '--ignore-gpu-blocklist',
      '--enable-unsafe-swiftshader'
    );
    console.log('🔧 Using SwiftShader (software rendering)');
  } else {
    args.push('--enable-webgl', '--enable-webgpu');
    console.log('🎮 Using hardware GPU acceleration');
  }

  const launchOptions = {
    headless: 'new',
    args,
    ignoreDefaultArgs: ['--disable-extensions'],
  };

  // Try to use system Chrome if available
  if (fs.existsSync(CHROME_PATH)) {
    launchOptions.executablePath = CHROME_PATH;
    console.log(`✓ Using Chrome at: ${CHROME_PATH}`);
  } else {
    console.log('⚠ Using bundled Chromium (system Chrome not found)');
  }

  return await puppeteer.launch(launchOptions);
}

/**
 * Render a material to PNG using headless Chrome
 */
async function renderMaterial(browser, htmlPath, materialName, outputPath, options = {}) {
  const page = await browser.newPage();

  await page.setViewport({
    width: options.width || 800,
    height: options.height || 600,
    deviceScaleFactor: 1,
  });

  // Enable console logging from the page
  page.on('console', msg => {
    if (options.verbose) {
      console.log(`  [Browser] ${msg.text()}`);
    }
  });

  // Handle errors
  page.on('pageerror', error => {
    console.error(`  ❌ Page error: ${error.message}`);
  });

  try {
    // Load the HTML page
    const url = `file://${htmlPath}`;
    console.log(`  Loading: ${url}`);
    await page.goto(url, { waitUntil: 'domcontentloaded', timeout: 30000 });

    // Wait for rendering to complete (give it plenty of time for CDN loads + rendering)
    await page.waitForFunction(
      () => window.renderComplete === true,
      { timeout: 120000 }
    );

    // Take screenshot
    await page.screenshot({
      path: outputPath,
      type: 'png',
    });

    console.log(`  ✓ Rendered: ${outputPath}`);

    await page.close();
    return true;
  } catch (error) {
    console.error(`  ❌ Render failed: ${error.message}`);
    await page.close();
    return false;
  }
}

/**
 * Compare two PNG images and generate diff
 */
function compareImages(img1Path, img2Path, diffPath) {
  if (!fs.existsSync(img1Path) || !fs.existsSync(img2Path)) {
    return {
      error: 'Missing image file(s)',
      pixelsDifferent: -1,
      percentDifferent: -1,
    };
  }

  const img1 = PNG.sync.read(fs.readFileSync(img1Path));
  const img2 = PNG.sync.read(fs.readFileSync(img2Path));

  if (img1.width !== img2.width || img1.height !== img2.height) {
    return {
      error: 'Image dimensions do not match',
      pixelsDifferent: -1,
      percentDifferent: -1,
    };
  }

  const { width, height } = img1;
  const diff = new PNG({ width, height });

  const numDiffPixels = pixelmatch(
    img1.data,
    img2.data,
    diff.data,
    width,
    height,
    { threshold: 0.1 } // 0-1 range, lower = more strict
  );

  // Save diff image
  fs.writeFileSync(diffPath, PNG.sync.write(diff));

  const totalPixels = width * height;
  const percentDifferent = (numDiffPixels / totalPixels) * 100;

  return {
    pixelsDifferent: numDiffPixels,
    totalPixels,
    percentDifferent: percentDifferent.toFixed(2),
    passed: percentDifferent < 2.0, // < 2% difference = pass
  };
}

/**
 * Generate HTML report
 */
function generateReport(results, outputPath) {
  const timestamp = new Date().toISOString();
  const passed = results.filter(r => r.passed).length;
  const failed = results.filter(r => !r.passed).length;

  const html = `<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>MaterialX Verification Report</title>
  <style>
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
      max-width: 1400px;
      margin: 0 auto;
      padding: 20px;
      background: #f5f5f5;
    }
    h1 { color: #333; }
    .summary {
      background: white;
      padding: 20px;
      border-radius: 8px;
      margin-bottom: 20px;
      box-shadow: 0 2px 4px rgba(0,0,0,0.1);
    }
    .stats {
      display: flex;
      gap: 20px;
      margin-top: 15px;
    }
    .stat {
      padding: 10px 20px;
      border-radius: 4px;
      font-weight: bold;
    }
    .stat.passed { background: #d4edda; color: #155724; }
    .stat.failed { background: #f8d7da; color: #721c24; }
    .test-card {
      background: white;
      padding: 20px;
      margin-bottom: 20px;
      border-radius: 8px;
      box-shadow: 0 2px 4px rgba(0,0,0,0.1);
    }
    .test-card.passed { border-left: 4px solid #28a745; }
    .test-card.failed { border-left: 4px solid #dc3545; }
    .comparison {
      display: grid;
      grid-template-columns: 1fr 1fr 1fr;
      gap: 10px;
      margin-top: 15px;
    }
    .comparison img {
      width: 100%;
      border: 1px solid #ddd;
      border-radius: 4px;
    }
    .label {
      font-size: 12px;
      color: #666;
      margin-bottom: 5px;
      font-weight: bold;
    }
    .metrics {
      margin-top: 10px;
      padding: 10px;
      background: #f8f9fa;
      border-radius: 4px;
      font-family: monospace;
      font-size: 13px;
    }
    .timestamp {
      color: #666;
      font-size: 14px;
    }
  </style>
</head>
<body>
  <h1>MaterialX Verification Report</h1>

  <div class="summary">
    <h2>Summary</h2>
    <p class="timestamp">Generated: ${timestamp}</p>
    <div class="stats">
      <div class="stat passed">✓ ${passed} Passed</div>
      <div class="stat failed">✗ ${failed} Failed</div>
      <div class="stat">Total: ${results.length}</div>
    </div>
  </div>

  ${results.map(result => `
    <div class="test-card ${result.passed ? 'passed' : 'failed'}">
      <h3>${result.passed ? '✓' : '✗'} ${result.material}</h3>

      <div class="comparison">
        <div>
          <div class="label">TinyUSDZ Renderer</div>
          <img src="${path.relative(path.dirname(outputPath), result.tinyusdz)}" alt="TinyUSDZ" />
        </div>
        <div>
          <div class="label">Reference (MaterialX)</div>
          <img src="${path.relative(path.dirname(outputPath), result.reference)}" alt="Reference" />
        </div>
        <div>
          <div class="label">Difference (highlighted)</div>
          <img src="${path.relative(path.dirname(outputPath), result.diff)}" alt="Diff" />
        </div>
      </div>

      <div class="metrics">
        <div>Pixels Different: ${result.comparison.pixelsDifferent} / ${result.comparison.totalPixels}</div>
        <div>Difference: ${result.comparison.percentDifferent}%</div>
        <div>Status: ${result.passed ? 'PASSED (< 2% difference)' : 'FAILED (≥ 2% difference)'}</div>
      </div>
    </div>
  `).join('\n')}

</body>
</html>`;

  fs.writeFileSync(outputPath, html);
  console.log(`\n📊 Report generated: ${outputPath}`);
}

/**
 * Main verification command
 */
async function verify(options) {
  console.log('🚀 MaterialX Verification Tool\n');

  const browser = await launchBrowser(options.gpu);
  const results = [];

  try {
    // Test materials list
    const testMaterials = options.materials
      ? options.materials.split(',')
      : ['brass', 'glass', 'gold', 'copper'];

    for (const material of testMaterials) {
      console.log(`\n📦 Testing material: ${material}`);

      // Paths for test HTML pages
      const tinyusdHtmlPath = path.join(__dirname, 'tests', 'render-tinyusdz.html');
      const referencHtmlPath = path.join(__dirname, 'tests', 'render-reference.html');

      // Output paths
      const tinyusdOutput = path.join(SCREENSHOTS_DIR, `tinyusdz-${material}.png`);
      const referenceOutput = path.join(SCREENSHOTS_DIR, `reference-${material}.png`);
      const diffOutput = path.join(DIFFS_DIR, `diff-${material}.png`);

      // Render with TinyUSDZ
      console.log('  Rendering with TinyUSDZ...');
      const tinySuccess = await renderMaterial(
        browser,
        tinyusdHtmlPath,
        material,
        tinyusdOutput,
        { verbose: options.verbose }
      );

      // Render with reference implementation
      console.log('  Rendering with MaterialX reference...');
      const refSuccess = await renderMaterial(
        browser,
        referencHtmlPath,
        material,
        referenceOutput,
        { verbose: options.verbose }
      );

      if (!tinySuccess || !refSuccess) {
        console.log(`  ⚠ Skipping comparison (render failed)`);
        continue;
      }

      // Compare images
      console.log('  Comparing images...');
      const comparison = compareImages(tinyusdOutput, referenceOutput, diffOutput);

      const result = {
        material,
        tinyusdz: tinyusdOutput,
        reference: referenceOutput,
        diff: diffOutput,
        comparison,
        passed: comparison.passed,
      };

      results.push(result);

      console.log(`  ${comparison.passed ? '✓' : '✗'} Difference: ${comparison.percentDifferent}%`);
    }

    // Generate report
    const reportPath = path.join(OUTPUT_DIR, 'report.html');
    generateReport(results, reportPath);

    // Print summary
    console.log('\n' + '='.repeat(60));
    console.log('SUMMARY');
    console.log('='.repeat(60));
    const passed = results.filter(r => r.passed).length;
    const failed = results.filter(r => !r.passed).length;
    console.log(`✓ Passed: ${passed}`);
    console.log(`✗ Failed: ${failed}`);
    console.log(`Total: ${results.length}`);
    console.log('='.repeat(60));

    // Exit with appropriate code
    process.exit(failed > 0 ? 1 : 0);

  } finally {
    await browser.close();
  }
}

// CLI Definition
program
  .name('verify-materialx')
  .description('Verify MaterialX rendering with headless Chrome')
  .version('1.0.0');

program
  .command('render')
  .description('Render and compare materials')
  .option('-m, --materials <list>', 'Comma-separated list of materials to test', 'brass,glass,gold,copper')
  .option('--gpu', 'Use GPU acceleration (default: SwiftShader)', false)
  .option('-v, --verbose', 'Verbose output', false)
  .action(verify);

program
  .command('clean')
  .description('Clean verification results directory')
  .action(() => {
    if (fs.existsSync(OUTPUT_DIR)) {
      fs.rmSync(OUTPUT_DIR, { recursive: true });
      console.log('✓ Cleaned verification results');
    }
  });

program.parse();
