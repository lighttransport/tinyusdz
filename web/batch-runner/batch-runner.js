#!/usr/bin/env node
/**
 * Visual Regression Batch Runner for USD Files
 *
 * Renders USD files with headless Chrome (SwiftShader or GPU) and performs
 * visual regression testing by comparing against reference images.
 *
 * Usage (from web/batch-runner directory):
 *   npm run generate -- -i ../js/models -o ./batch-results
 *   npm run validate -- -i ../js/models -o ./batch-results --threshold 2.0
 *
 * Or directly:
 *   node batch-runner.js generate -i <input-dir> -o <output-dir>
 */

import { program } from 'commander';
import { spawn } from 'child_process';
import { createServer } from 'vite';
import { glob } from 'glob';
import fs from 'fs';
import fsPromises from 'fs/promises';
import path from 'path';
import { fileURLToPath } from 'url';
import { PNG } from 'pngjs';
import pixelmatch from 'pixelmatch';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// Configuration
const CHROME_PATH = process.env.CHROME_PATH || '/opt/google/chrome/chrome';
const DEFAULT_EXTENSIONS = ['.usd', '.usda', '.usdc', '.usdz'];
const DEFAULT_WIDTH = 800;
const DEFAULT_HEIGHT = 600;
const DEFAULT_TIMEOUT = 120000;
const DEFAULT_RENDER_TIMEOUT = 60000;
const DEFAULT_THRESHOLD = 2.0;

// ============================================================================
// Browser Management (using Chrome subprocess)
// ============================================================================

/**
 * Take a screenshot using Chrome subprocess
 * @param {string} url - URL to screenshot
 * @param {string} outputPath - Path to save screenshot
 * @param {object} options - Options (width, height, timeout, verbose)
 * @returns {Promise<{success: boolean, error?: string}>}
 */
async function takeScreenshot(url, outputPath, options = {}) {
  const chromePath = fs.existsSync(CHROME_PATH) ? CHROME_PATH : '/usr/bin/google-chrome';
  const timeout = options.timeout || DEFAULT_RENDER_TIMEOUT;

  const args = [
    '--headless=new',
    '--enable-unsafe-swiftshader',
    '--enable-webgl',
    '--disable-gpu',
    '--disable-dev-shm-usage',
    '--no-sandbox',
    `--window-size=${options.width || DEFAULT_WIDTH},${options.height || DEFAULT_HEIGHT}`,
    `--virtual-time-budget=${timeout}`,
    `--screenshot=${outputPath}`,
    url
  ];

  if (options.verbose) {
    console.log(`  Chrome: ${chromePath}`);
    console.log(`  URL: ${url}`);
  }

  return new Promise((resolve) => {
    const chrome = spawn(chromePath, args, {
      env: { ...process.env, DISPLAY: ':99' }
    });
    let stderr = '';

    chrome.stderr.on('data', (data) => {
      stderr += data.toString();
    });

    chrome.on('close', (code) => {
      if (code === 0 && fs.existsSync(outputPath)) {
        resolve({ success: true });
      } else {
        resolve({ success: false, error: stderr || `Chrome exited with code ${code}` });
      }
    });

    chrome.on('error', (err) => {
      resolve({ success: false, error: err.message });
    });
  });
}

// ============================================================================
// Dev Server Management
// ============================================================================

/**
 * Start Vite dev server on an available port
 */
async function startDevServer(verbose = false) {
  const webJsDir = path.join(__dirname, '..', 'js');
  const server = await createServer({
    root: webJsDir, // web/js directory (where vite.config.ts is)
    configFile: path.join(webJsDir, 'vite.config.ts'),
    server: {
      port: 0, // Auto-assign available port
      strictPort: false,
      cors: true,
    },
    logLevel: verbose ? 'info' : 'silent',
  });

  await server.listen();
  const address = server.httpServer.address();
  const url = `http://localhost:${address.port}`;

  if (verbose) console.log(`Dev server started at: ${url}`);

  return {
    url,
    root: server.config.root,
    close: () => server.close(),
  };
}

// ============================================================================
// File Scanning
// ============================================================================

/**
 * Scan directory for USD files
 */
async function scanDirectory(inputDir, options = {}) {
  const extensions = options.extensions || DEFAULT_EXTENSIONS;
  const recursive = options.recursive !== false;

  const patterns = extensions.map(ext =>
    recursive ? `**/*${ext}` : `*${ext}`
  );

  const files = await glob(patterns, {
    cwd: inputDir,
    absolute: true,
    nodir: true,
    follow: true,
    ignore: ['**/node_modules/**', '**/.git/**'],
  });

  // Remove duplicates and sort
  return [...new Set(files)].sort();
}

// ============================================================================
// Rendering
// ============================================================================

/**
 * Render a single USD file and save screenshot using Chrome subprocess
 */
async function renderFile(serverUrl, serverRoot, filePath, outputPath, options = {}) {
  const startTime = Date.now();
  const result = {
    file: filePath,
    filename: path.basename(filePath),
    status: 'PENDING',
    error: null,
    renderTime: 0,
    screenshotPath: null,
  };

  try {
    // Calculate relative path from server root to file
    const relativePath = path.relative(serverRoot, filePath);
    const renderTimeout = options.renderTimeout || DEFAULT_RENDER_TIMEOUT;

    // Build viewer URL with parameters
    const viewerUrl = `${serverUrl}/materialx.html?usd=${encodeURIComponent(relativePath)}&autoRender=true&renderTimeout=${renderTimeout}`;

    if (options.verbose) {
      console.log(`  Loading: ${viewerUrl}`);
    }

    // Ensure output directory exists
    await fsPromises.mkdir(path.dirname(outputPath), { recursive: true });

    // Take screenshot using Chrome subprocess
    const screenshotResult = await takeScreenshot(viewerUrl, outputPath, {
      width: options.width || DEFAULT_WIDTH,
      height: options.height || DEFAULT_HEIGHT,
      timeout: renderTimeout,
      verbose: options.verbose,
    });

    if (screenshotResult.success) {
      result.status = 'SUCCESS';
      result.screenshotPath = outputPath;
    } else {
      result.status = 'ERROR';
      result.error = screenshotResult.error || 'Screenshot failed';
    }

    result.renderTime = Date.now() - startTime;

  } catch (error) {
    result.status = 'ERROR';
    result.renderTime = Date.now() - startTime;
    result.error = error.message;
  }

  return result;
}

// ============================================================================
// Image Comparison
// ============================================================================

/**
 * Compare two PNG images and generate diff
 */
function compareImages(screenshotPath, referencePath, diffPath, options = {}) {
  if (!fs.existsSync(screenshotPath)) {
    return {
      error: `Screenshot not found: ${screenshotPath}`,
      pixelsDifferent: -1,
      percentDifferent: -1,
      status: 'ERROR',
    };
  }

  if (!fs.existsSync(referencePath)) {
    return {
      error: `Reference not found: ${referencePath}`,
      pixelsDifferent: -1,
      percentDifferent: -1,
      status: 'SKIP',
    };
  }

  const img1 = PNG.sync.read(fs.readFileSync(screenshotPath));
  const img2 = PNG.sync.read(fs.readFileSync(referencePath));

  if (img1.width !== img2.width || img1.height !== img2.height) {
    return {
      error: 'Image dimensions do not match',
      pixelsDifferent: -1,
      percentDifferent: -1,
      status: 'ERROR',
    };
  }

  const { width, height } = img1;
  const diff = new PNG({ width, height });

  const threshold = options.pixelmatchThreshold || 0.1;
  const numDiffPixels = pixelmatch(
    img1.data,
    img2.data,
    diff.data,
    width,
    height,
    { threshold }
  );

  // Save diff image
  fs.mkdirSync(path.dirname(diffPath), { recursive: true });
  fs.writeFileSync(diffPath, PNG.sync.write(diff));

  const totalPixels = width * height;
  const percentDifferent = (numDiffPixels / totalPixels) * 100;
  const diffThreshold = options.threshold || DEFAULT_THRESHOLD;
  const passed = percentDifferent < diffThreshold;

  return {
    pixelsDifferent: numDiffPixels,
    totalPixels,
    percentDifferent: parseFloat(percentDifferent.toFixed(4)),
    passed,
    status: passed ? 'PASS' : 'FAIL',
    diffPath,
  };
}

// ============================================================================
// Report Generation
// ============================================================================

/**
 * Generate HTML report
 */
function generateHTMLReport(results, outputDir, inputDir) {
  const timestamp = new Date().toISOString();
  const passed = results.filter(r => r.status === 'PASS').length;
  const failed = results.filter(r => r.status === 'FAIL').length;
  const skipped = results.filter(r => r.status === 'SKIP').length;
  const errors = results.filter(r => r.status === 'ERROR').length;

  // Group results by folder
  const folders = {};
  for (const result of results) {
    const relPath = path.relative(inputDir, result.file);
    const folder = path.dirname(relPath) || '.';
    if (!folders[folder]) {
      folders[folder] = [];
    }
    folders[folder].push(result);
  }

  const html = `<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Visual Regression Report - ${timestamp}</title>
  <style>
    * { box-sizing: border-box; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
      max-width: 1600px;
      margin: 0 auto;
      padding: 20px;
      background: #f5f5f5;
    }
    h1 { color: #333; margin-bottom: 5px; }
    .timestamp { color: #666; font-size: 14px; margin-bottom: 20px; }
    .summary {
      background: white;
      padding: 20px;
      border-radius: 8px;
      margin-bottom: 20px;
      box-shadow: 0 2px 4px rgba(0,0,0,0.1);
    }
    .stats {
      display: flex;
      gap: 15px;
      margin-top: 15px;
      flex-wrap: wrap;
    }
    .stat {
      padding: 10px 20px;
      border-radius: 4px;
      font-weight: bold;
      font-size: 14px;
    }
    .stat.pass { background: #d4edda; color: #155724; }
    .stat.fail { background: #f8d7da; color: #721c24; }
    .stat.skip { background: #fff3cd; color: #856404; }
    .stat.error { background: #f5c6cb; color: #721c24; }
    .stat.total { background: #e2e3e5; color: #383d41; }
    .progress-bar {
      height: 8px;
      background: #e9ecef;
      border-radius: 4px;
      overflow: hidden;
      margin-top: 15px;
    }
    .progress {
      height: 100%;
      background: #28a745;
      transition: width 0.3s;
    }
    .folder-section {
      margin-bottom: 30px;
    }
    .folder-section h2 {
      color: #333;
      font-size: 18px;
      margin-bottom: 15px;
      padding-bottom: 10px;
      border-bottom: 2px solid #ddd;
    }
    .test-grid {
      display: grid;
      grid-template-columns: repeat(auto-fill, minmax(500px, 1fr));
      gap: 20px;
    }
    .test-card {
      background: white;
      padding: 15px;
      border-radius: 8px;
      box-shadow: 0 2px 4px rgba(0,0,0,0.1);
    }
    .test-card.pass { border-left: 4px solid #28a745; }
    .test-card.fail { border-left: 4px solid #dc3545; }
    .test-card.skip { border-left: 4px solid #ffc107; }
    .test-card.error { border-left: 4px solid #dc3545; }
    .test-card h3 {
      margin: 0 0 10px 0;
      font-size: 14px;
      word-break: break-all;
    }
    .comparison {
      display: grid;
      grid-template-columns: 1fr 1fr 1fr;
      gap: 8px;
      margin-top: 10px;
    }
    .comparison > div {
      text-align: center;
    }
    .comparison img {
      width: 100%;
      border: 1px solid #ddd;
      border-radius: 4px;
      cursor: pointer;
    }
    .comparison img:hover {
      border-color: #007bff;
    }
    .label {
      font-size: 11px;
      color: #666;
      margin-bottom: 4px;
      font-weight: bold;
    }
    .metrics {
      margin-top: 10px;
      padding: 10px;
      background: #f8f9fa;
      border-radius: 4px;
      font-family: monospace;
      font-size: 12px;
    }
    .metrics div { margin: 2px 0; }
    .error-msg {
      color: #dc3545;
      font-style: italic;
    }
    .lightbox {
      display: none;
      position: fixed;
      top: 0;
      left: 0;
      width: 100%;
      height: 100%;
      background: rgba(0,0,0,0.9);
      z-index: 1000;
      justify-content: center;
      align-items: center;
    }
    .lightbox.active { display: flex; }
    .lightbox img {
      max-width: 95%;
      max-height: 95%;
      object-fit: contain;
    }
    .lightbox-close {
      position: absolute;
      top: 20px;
      right: 30px;
      color: white;
      font-size: 40px;
      cursor: pointer;
    }
  </style>
</head>
<body>
  <h1>Visual Regression Test Report</h1>
  <p class="timestamp">Generated: ${timestamp}</p>

  <div class="summary">
    <h2>Summary</h2>
    <div class="stats">
      <div class="stat pass">PASS: ${passed}</div>
      <div class="stat fail">FAIL: ${failed}</div>
      <div class="stat skip">SKIP: ${skipped}</div>
      <div class="stat error">ERROR: ${errors}</div>
      <div class="stat total">Total: ${results.length}</div>
    </div>
    <div class="progress-bar">
      <div class="progress" style="width: ${results.length > 0 ? (passed / results.length * 100) : 0}%"></div>
    </div>
  </div>

  ${Object.entries(folders).map(([folder, tests]) => `
    <div class="folder-section">
      <h2>${folder === '.' ? 'Root' : folder}</h2>
      <div class="test-grid">
        ${tests.map(test => {
          const statusClass = test.status.toLowerCase();
          const relPath = path.relative(inputDir, test.file);
          const screenshotRel = test.screenshotPath ? path.relative(outputDir, test.screenshotPath) : '';
          const referenceRel = test.referencePath ? path.relative(outputDir, test.referencePath) : '';
          const diffRel = test.diffPath ? path.relative(outputDir, test.diffPath) : '';

          return `
            <div class="test-card ${statusClass}">
              <h3>${test.status === 'PASS' ? '✓' : test.status === 'FAIL' ? '✗' : test.status === 'SKIP' ? '⊘' : '⚠'} ${test.filename}</h3>

              ${test.status === 'PASS' || test.status === 'FAIL' ? `
                <div class="comparison">
                  <div>
                    <div class="label">Screenshot</div>
                    <img src="${screenshotRel}" alt="Screenshot" onclick="openLightbox(this.src)" />
                  </div>
                  <div>
                    <div class="label">Reference</div>
                    <img src="${referenceRel}" alt="Reference" onclick="openLightbox(this.src)" />
                  </div>
                  <div>
                    <div class="label">Diff</div>
                    <img src="${diffRel}" alt="Diff" onclick="openLightbox(this.src)" />
                  </div>
                </div>
              ` : ''}

              <div class="metrics">
                <div>Status: <strong>${test.status}</strong></div>
                ${test.percentDifferent !== undefined && test.percentDifferent >= 0 ? `
                  <div>Difference: ${test.percentDifferent.toFixed(4)}%</div>
                  <div>Pixels: ${test.pixelsDifferent} / ${test.totalPixels}</div>
                ` : ''}
                <div>Render Time: ${test.renderTime}ms</div>
                ${test.error ? `<div class="error-msg">Error: ${test.error}</div>` : ''}
              </div>
            </div>
          `;
        }).join('')}
      </div>
    </div>
  `).join('')}

  <div class="lightbox" id="lightbox" onclick="closeLightbox()">
    <span class="lightbox-close">&times;</span>
    <img id="lightbox-img" src="" alt="Full size" />
  </div>

  <script>
    function openLightbox(src) {
      document.getElementById('lightbox-img').src = src;
      document.getElementById('lightbox').classList.add('active');
    }
    function closeLightbox() {
      document.getElementById('lightbox').classList.remove('active');
    }
    document.addEventListener('keydown', e => {
      if (e.key === 'Escape') closeLightbox();
    });
  </script>
</body>
</html>`;

  const reportPath = path.join(outputDir, 'reports', 'index.html');
  fs.mkdirSync(path.dirname(reportPath), { recursive: true });
  fs.writeFileSync(reportPath, html);

  return reportPath;
}

/**
 * Generate CSV report
 */
function generateCSVReport(results, outputDir, inputDir) {
  const headers = [
    'filename',
    'folder',
    'status',
    'diff_percent',
    'pixels_different',
    'total_pixels',
    'render_time_ms',
    'reference_path',
    'screenshot_path',
    'diff_path',
    'error',
  ];

  const rows = results.map(r => {
    const relPath = path.relative(inputDir, r.file);
    const folder = path.dirname(relPath) || '.';
    return [
      r.filename,
      folder,
      r.status,
      r.percentDifferent !== undefined ? r.percentDifferent.toFixed(4) : '',
      r.pixelsDifferent !== undefined ? r.pixelsDifferent : '',
      r.totalPixels !== undefined ? r.totalPixels : '',
      r.renderTime,
      r.referencePath ? path.relative(outputDir, r.referencePath) : '',
      r.screenshotPath ? path.relative(outputDir, r.screenshotPath) : '',
      r.diffPath ? path.relative(outputDir, r.diffPath) : '',
      r.error || '',
    ].map(v => `"${String(v).replace(/"/g, '""')}"`).join(',');
  });

  const csv = [headers.join(','), ...rows].join('\n');
  const csvPath = path.join(outputDir, 'results.csv');
  fs.writeFileSync(csvPath, csv);

  return csvPath;
}

/**
 * Generate manifest JSON
 */
function generateManifest(results, outputDir, inputDir, options) {
  const manifest = {
    generated: new Date().toISOString(),
    inputDir,
    outputDir,
    options: {
      width: options.width,
      height: options.height,
      gpu: options.gpu,
      threshold: options.threshold,
      renderTimeout: options.renderTimeout,
    },
    summary: {
      total: results.length,
      passed: results.filter(r => r.status === 'PASS').length,
      failed: results.filter(r => r.status === 'FAIL').length,
      skipped: results.filter(r => r.status === 'SKIP').length,
      errors: results.filter(r => r.status === 'ERROR').length,
    },
    files: results.map(r => ({
      file: path.relative(inputDir, r.file),
      status: r.status,
      diffPercent: r.percentDifferent,
      renderTime: r.renderTime,
      error: r.error,
    })),
  };

  const manifestPath = path.join(outputDir, 'manifest.json');
  fs.writeFileSync(manifestPath, JSON.stringify(manifest, null, 2));

  return manifestPath;
}

// ============================================================================
// Progress Reporting
// ============================================================================

function logProgress(current, total, result, verbose = false) {
  const percent = Math.round((current / total) * 100);
  const statusIcon = {
    SUCCESS: '✓',
    PASS: '✓',
    FAIL: '✗',
    SKIP: '⊘',
    ERROR: '⚠',
  }[result.status] || '?';

  const statusColor = {
    SUCCESS: '\x1b[32m',
    PASS: '\x1b[32m',
    FAIL: '\x1b[31m',
    SKIP: '\x1b[33m',
    ERROR: '\x1b[31m',
  }[result.status] || '';

  const reset = '\x1b[0m';

  console.log(
    `[${current}/${total}] ${percent}% ${statusColor}${statusIcon}${reset} ${result.filename}` +
    (result.percentDifferent !== undefined && result.percentDifferent >= 0
      ? ` (${result.percentDifferent.toFixed(2)}% diff)`
      : '') +
    (result.error ? ` - ${result.error}` : '')
  );
}

function printSummary(results) {
  const passed = results.filter(r => r.status === 'PASS' || r.status === 'SUCCESS').length;
  const failed = results.filter(r => r.status === 'FAIL').length;
  const skipped = results.filter(r => r.status === 'SKIP').length;
  const errors = results.filter(r => r.status === 'ERROR').length;

  console.log('\n' + '='.repeat(60));
  console.log('SUMMARY');
  console.log('='.repeat(60));
  console.log(`\x1b[32m✓ Passed:\x1b[0m  ${passed}`);
  console.log(`\x1b[31m✗ Failed:\x1b[0m  ${failed}`);
  console.log(`\x1b[33m⊘ Skipped:\x1b[0m ${skipped}`);
  console.log(`\x1b[31m⚠ Errors:\x1b[0m  ${errors}`);
  console.log(`  Total:   ${results.length}`);
  console.log('='.repeat(60));
}

// ============================================================================
// Commands
// ============================================================================

/**
 * Generate reference images
 */
async function generateCommand(options) {
  console.log('Visual Regression Batch Runner - Generate Mode\n');

  const inputDir = path.resolve(options.input);
  const outputDir = path.resolve(options.output);
  const referencesDir = path.join(outputDir, 'references');

  // Parse extensions
  const extensions = options.extensions
    ? options.extensions.split(',').map(e => e.trim())
    : DEFAULT_EXTENSIONS;

  console.log(`Input directory:  ${inputDir}`);
  console.log(`Output directory: ${outputDir}`);
  console.log(`Extensions:       ${extensions.join(', ')}`);
  console.log(`Resolution:       ${options.width}x${options.height}`);
  console.log(`Render timeout:   ${options.renderTimeout}ms`);
  console.log(`GPU mode:         ${options.gpu ? 'Yes' : 'No (SwiftShader)'}`);
  console.log('');

  // Scan for USD files
  console.log('Scanning for USD files...');
  const files = await scanDirectory(inputDir, {
    extensions,
    recursive: options.recursive,
  });

  if (files.length === 0) {
    console.log('No USD files found.');
    return;
  }

  console.log(`Found ${files.length} USD file(s)\n`);

  // Start dev server (no browser needed - using Chrome subprocess)
  const server = await startDevServer(options.verbose);

  const results = [];

  try {
    for (let i = 0; i < files.length; i++) {
      const file = files[i];
      const relPath = path.relative(inputDir, file);
      const outputPath = path.join(referencesDir, relPath + '.png');

      const result = await renderFile(
        server.url,
        server.root,
        file,
        outputPath,
        {
          width: parseInt(options.width, 10),
          height: parseInt(options.height, 10),
          timeout: parseInt(options.timeout, 10),
          renderTimeout: parseInt(options.renderTimeout, 10),
          verbose: options.verbose,
        }
      );

      results.push(result);
      logProgress(i + 1, files.length, result, options.verbose);
    }

    // Generate manifest
    generateManifest(results, outputDir, inputDir, options);

    printSummary(results);

    const errors = results.filter(r => r.status === 'ERROR').length;
    if (errors > 0) {
      console.log(`\nGenerated ${results.length - errors} reference image(s) with ${errors} error(s)`);
    } else {
      console.log(`\nGenerated ${results.length} reference image(s)`);
    }

  } finally {
    await server.close();
  }
}

/**
 * Validate against reference images
 */
async function validateCommand(options) {
  console.log('Visual Regression Batch Runner - Validate Mode\n');

  const inputDir = path.resolve(options.input);
  const outputDir = path.resolve(options.output);
  const referencesDir = path.join(outputDir, 'references');
  const screenshotsDir = path.join(outputDir, 'screenshots');
  const diffsDir = path.join(outputDir, 'diffs');

  // Parse extensions
  const extensions = options.extensions
    ? options.extensions.split(',').map(e => e.trim())
    : DEFAULT_EXTENSIONS;

  const threshold = parseFloat(options.threshold);

  console.log(`Input directory:  ${inputDir}`);
  console.log(`Output directory: ${outputDir}`);
  console.log(`Extensions:       ${extensions.join(', ')}`);
  console.log(`Resolution:       ${options.width}x${options.height}`);
  console.log(`Render timeout:   ${options.renderTimeout}ms`);
  console.log(`Diff threshold:   ${threshold}%`);
  console.log(`GPU mode:         ${options.gpu ? 'Yes' : 'No (SwiftShader)'}`);
  console.log('');

  // Scan for USD files
  console.log('Scanning for USD files...');
  const files = await scanDirectory(inputDir, {
    extensions,
    recursive: options.recursive,
  });

  if (files.length === 0) {
    console.log('No USD files found.');
    return;
  }

  console.log(`Found ${files.length} USD file(s)\n`);

  // Start dev server (no browser needed - using Chrome subprocess)
  const server = await startDevServer(options.verbose);

  const results = [];

  try {
    for (let i = 0; i < files.length; i++) {
      const file = files[i];
      const relPath = path.relative(inputDir, file);
      const screenshotPath = path.join(screenshotsDir, relPath + '.png');
      const referencePath = path.join(referencesDir, relPath + '.png');
      const diffPath = path.join(diffsDir, relPath + '.png');

      // Render current version
      const renderResult = await renderFile(
        server.url,
        server.root,
        file,
        screenshotPath,
        {
          width: parseInt(options.width, 10),
          height: parseInt(options.height, 10),
          timeout: parseInt(options.timeout, 10),
          renderTimeout: parseInt(options.renderTimeout, 10),
          verbose: options.verbose,
        }
      );

      // Compare with reference
      let comparison = { status: 'ERROR', error: 'Render failed' };
      if (renderResult.status === 'SUCCESS') {
        comparison = compareImages(screenshotPath, referencePath, diffPath, {
          threshold,
        });
      }

      const result = {
        ...renderResult,
        referencePath,
        diffPath: comparison.diffPath,
        pixelsDifferent: comparison.pixelsDifferent,
        totalPixels: comparison.totalPixels,
        percentDifferent: comparison.percentDifferent,
        status: comparison.status,
        error: comparison.error || renderResult.error,
      };

      results.push(result);
      logProgress(i + 1, files.length, result, options.verbose);

      // Fail fast if requested
      if (options.failFast && result.status === 'FAIL') {
        console.log('\nFail fast triggered. Stopping.');
        break;
      }
    }

    // Generate reports
    console.log('\nGenerating reports...');

    const htmlPath = generateHTMLReport(results, outputDir, inputDir);
    console.log(`  HTML report: ${htmlPath}`);

    const csvPath = generateCSVReport(results, outputDir, inputDir);
    console.log(`  CSV report:  ${csvPath}`);

    generateManifest(results, outputDir, inputDir, { ...options, threshold });

    printSummary(results);

    // Exit with appropriate code
    const failures = results.filter(r => r.status === 'FAIL' || r.status === 'ERROR');
    process.exit(failures.length > 0 ? 1 : 0);

  } finally {
    await server.close();
  }
}

/**
 * Clean output directory
 */
async function cleanCommand(options) {
  const outputDir = path.resolve(options.output);

  if (fs.existsSync(outputDir)) {
    fs.rmSync(outputDir, { recursive: true });
    console.log(`Cleaned: ${outputDir}`);
  } else {
    console.log(`Directory does not exist: ${outputDir}`);
  }
}

// ============================================================================
// CLI Definition
// ============================================================================

program
  .name('batch-runner')
  .description('Visual regression testing for USD files')
  .version('1.0.0');

program
  .command('generate')
  .description('Generate reference images for USD files')
  .requiredOption('-i, --input <path>', 'Input directory with USD files')
  .option('-o, --output <path>', 'Output directory for results', './batch-results')
  .option('--gpu', 'Use hardware GPU instead of SwiftShader', false)
  .option('-w, --width <number>', 'Screenshot width', String(DEFAULT_WIDTH))
  .option('-H, --height <number>', 'Screenshot height', String(DEFAULT_HEIGHT))
  .option('-r, --recursive', 'Scan directories recursively', true)
  .option('--no-recursive', 'Do not scan directories recursively')
  .option('--timeout <ms>', 'Page load timeout in milliseconds', String(DEFAULT_TIMEOUT))
  .option('--render-timeout <ms>', 'Render stabilization timeout in milliseconds', String(DEFAULT_RENDER_TIMEOUT))
  .option('--extensions <list>', 'Comma-separated file extensions', DEFAULT_EXTENSIONS.join(','))
  .option('-v, --verbose', 'Verbose output', false)
  .action(generateCommand);

program
  .command('validate')
  .description('Validate rendered images against references')
  .requiredOption('-i, --input <path>', 'Input directory with USD files')
  .option('-o, --output <path>', 'Output directory for results', './batch-results')
  .option('-t, --threshold <percent>', 'Diff threshold percentage', String(DEFAULT_THRESHOLD))
  .option('--gpu', 'Use hardware GPU instead of SwiftShader', false)
  .option('-w, --width <number>', 'Screenshot width', String(DEFAULT_WIDTH))
  .option('-H, --height <number>', 'Screenshot height', String(DEFAULT_HEIGHT))
  .option('-r, --recursive', 'Scan directories recursively', true)
  .option('--no-recursive', 'Do not scan directories recursively')
  .option('--timeout <ms>', 'Page load timeout in milliseconds', String(DEFAULT_TIMEOUT))
  .option('--render-timeout <ms>', 'Render stabilization timeout in milliseconds', String(DEFAULT_RENDER_TIMEOUT))
  .option('--extensions <list>', 'Comma-separated file extensions', DEFAULT_EXTENSIONS.join(','))
  .option('--fail-fast', 'Stop on first failure', false)
  .option('-v, --verbose', 'Verbose output', false)
  .action(validateCommand);

program
  .command('clean')
  .description('Clean output directory')
  .option('-o, --output <path>', 'Output directory to clean', './batch-results')
  .action(cleanCommand);

program.parse();
