#!/usr/bin/env node
// MaterialX RenderMaterial Dump CLI Tool
// Usage: node dump-materialx-cli.js <usd-file> [options]

import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';
import fs from 'node:fs';
import path from 'node:path';
import YAML from 'yaml';

// Parse command line arguments
function parseArgs() {
  const args = process.argv.slice(2);
  const options = {
    inputFile: null,
    format: 'json', // 'json', 'yaml', or 'xml'
    outputFile: null,
    materialId: null, // null means all materials
    pretty: true,
    verbose: false
  };

  for (let i = 0; i < args.length; i++) {
    const arg = args[i];

    if (arg === '-h' || arg === '--help') {
      printHelp();
      process.exit(0);
    } else if (arg === '-f' || arg === '--format') {
      options.format = args[++i];
      if (options.format !== 'json' && options.format !== 'xml' && options.format !== 'yaml') {
        console.error('Error: Format must be "json", "yaml", or "xml"');
        process.exit(1);
      }
    } else if (arg === '-o' || arg === '--output') {
      options.outputFile = args[++i];
    } else if (arg === '-m' || arg === '--material') {
      options.materialId = parseInt(args[++i], 10);
    } else if (arg === '--no-pretty') {
      options.pretty = false;
    } else if (arg === '-v' || arg === '--verbose') {
      options.verbose = true;
    } else if (!options.inputFile) {
      options.inputFile = arg;
    }
  }

  if (!options.inputFile) {
    console.error('Error: Input USD file is required');
    printHelp();
    process.exit(1);
  }

  return options;
}

function printHelp() {
  console.log(`
MaterialX RenderMaterial Dump CLI Tool

Usage: node dump-materialx-cli.js <usd-file> [options]

Arguments:
  <usd-file>              USD/USDA/USDC/USDZ file to load

Options:
  -f, --format <format>   Output format: 'json', 'yaml', or 'xml' (default: json)
  -o, --output <file>     Write output to file instead of stdout
  -m, --material <id>     Dump only specific material by ID (default: all)
  --no-pretty             Disable pretty-printing for JSON/YAML
  -v, --verbose           Enable verbose logging
  -h, --help              Show this help message

Examples:
  # Dump all materials as JSON
  node dump-materialx-cli.js models/polysphere-materialx-001.usda

  # Dump as human-readable YAML
  node dump-materialx-cli.js models/suzanne-pbr.usda -f yaml

  # Dump specific material as MaterialX XML
  node dump-materialx-cli.js models/suzanne-pbr.usda -f xml -m 0

  # Save output to file
  node dump-materialx-cli.js models/teapot-pbr.usdc -o materials.json

  # Verbose mode with YAML output
  node dump-materialx-cli.js models/texturedcube.usdc -f yaml -v
`);
}

function loadFile(filename) {
  try {
    const data = fs.readFileSync(filename);
    const mimeType = 'application/octet-stream';
    const blob = new Blob([data], { type: mimeType });
    const f = new File([blob], path.basename(filename), { type: blob.type });
    return f;
  } catch (err) {
    console.error(`Error loading file: ${err.message}`);
    return null;
  }
}

function formatMaterialOutput(materialData, format, pretty) {
  if (format === 'json') {
    return pretty ? JSON.stringify(materialData, null, 2) : JSON.stringify(materialData);
  } else {
    // XML format - already a string
    return materialData;
  }
}

async function dumpMaterials(options) {
  if (options.verbose) {
    console.error(`Loading USD file: ${options.inputFile}`);
  }

  // Check if file exists
  if (!fs.existsSync(options.inputFile)) {
    console.error(`Error: File not found: ${options.inputFile}`);
    process.exit(1);
  }

  // Initialize loader
  const loader = new TinyUSDZLoader();
  await loader.init({ useMemory64: false });
  loader.setMaxMemoryLimitMB(500);

  if (options.verbose) {
    console.error('Loader initialized');
  }

  // Load file
  const file = loadFile(options.inputFile);
  if (!file) {
    process.exit(1);
  }

  // Read file as ArrayBuffer
  const data = fs.readFileSync(options.inputFile);
  const arrayBuffer = data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);

  // Parse USD file with proper conversion to RenderScene
  const usd = await new Promise((resolve, reject) => {
    loader.parse(arrayBuffer, options.inputFile, resolve, reject);
  });

  if (!usd) {
    console.error('Error: Failed to load USD file');
    process.exit(1);
  }

  if (options.verbose) {
    console.error('USD file loaded successfully');
  }

  // Get number of materials and meshes
  const numMaterials = usd.numMaterials();
  const numMeshes = usd.numMeshes();

  if (options.verbose) {
    console.error(`Found ${numMaterials} material(s), ${numMeshes} mesh(es)`);

    // Check for meshes with GeomSubsets
    let subsetsFound = 0;
    for (let i = 0; i < numMeshes; i++) {
      const mesh = usd.getMesh(i);
      if (mesh.materialSubsets && mesh.materialSubsets.length > 0) {
        subsetsFound++;
        console.error(`  Mesh ${i} "${mesh.primName}" has ${mesh.materialSubsets.length} material subset(s)`);
      }
    }
    if (subsetsFound > 0) {
      console.error(`Total: ${subsetsFound} mesh(es) with GeomSubsets (per-face materials)`);
    }
  }

  if (numMaterials === 0) {
    console.error('Warning: No materials found in USD file');
    if (!options.outputFile) {
      console.log(options.format === 'json' ? '[]' : '<?xml version="1.0"?>\n<materials/>\n');
    }
    return;
  }

  const results = [];

  // Determine which materials to dump
  const materialIds = options.materialId !== null
    ? [options.materialId]
    : Array.from({ length: numMaterials }, (_, i) => i);

  // Dump materials
  for (const matId of materialIds) {
    if (matId >= numMaterials) {
      console.error(`Warning: Material ID ${matId} out of range (0-${numMaterials - 1})`);
      continue;
    }

    if (options.verbose) {
      console.error(`\nProcessing material ${matId}...`);
    }

    try {
      // For YAML, we get JSON from the native module and convert it
      const nativeFormat = (options.format === 'yaml') ? 'json' : options.format;
      const result = usd.getMaterialWithFormat(matId, nativeFormat);

      if (result.error) {
        console.error(`Error getting material ${matId}: ${result.error}`);
        continue;
      }

      if (options.format === 'json' || options.format === 'yaml') {
        const materialData = JSON.parse(result.data);

        if (options.verbose) {
          console.error(`  Format: ${options.format}`);
          console.error(`  Has OpenPBR: ${materialData.hasOpenPBR || false}`);
          console.error(`  Has UsdPreviewSurface: ${materialData.hasUsdPreviewSurface || false}`);
          if (materialData.name) {
            console.error(`  Name: ${materialData.name}`);
          }
        }

        results.push(materialData);
      } else {
        // XML format
        if (options.verbose) {
          console.error(`  Format: ${result.format}`);
          console.error(`  XML length: ${result.data.length} bytes`);
        }

        results.push({
          materialId: matId,
          format: result.format,
          xml: result.data
        });
      }
    } catch (err) {
      console.error(`Error processing material ${matId}: ${err.message}`);
    }
  }

  // Output results
  let output;

  if (options.format === 'json') {
    output = options.pretty
      ? JSON.stringify(results, null, 2)
      : JSON.stringify(results);
  } else if (options.format === 'yaml') {
    // YAML format - human-readable
    output = YAML.stringify(results, {
      indent: 2,
      lineWidth: 0,  // Don't wrap lines
      minContentWidth: 0,
      defaultKeyType: 'PLAIN',
      defaultStringType: 'QUOTE_DOUBLE'
    });
  } else {
    // XML format - combine multiple materials
    if (results.length === 1) {
      output = results[0].xml;
    } else {
      // Wrap multiple materials in a container
      const xmlParts = ['<?xml version="1.0"?>\n<materials>\n'];
      for (const result of results) {
        xmlParts.push(`  <!-- Material ${result.materialId} -->\n`);
        // Indent each line of the XML
        const indented = result.xml
          .split('\n')
          .map(line => line.trim() ? '  ' + line : line)
          .join('\n');
        xmlParts.push(indented);
        xmlParts.push('\n\n');
      }
      xmlParts.push('</materials>\n');
      output = xmlParts.join('');
    }
  }

  // Write to file or stdout
  if (options.outputFile) {
    try {
      fs.writeFileSync(options.outputFile, output, 'utf8');
      if (options.verbose) {
        console.error(`\nOutput written to: ${options.outputFile}`);
      }
    } catch (err) {
      console.error(`Error writing output file: ${err.message}`);
      process.exit(1);
    }
  } else {
    console.log(output);
  }

  if (options.verbose) {
    console.error('\nDone!');
  }
}

// Main execution
async function main() {
  try {
    const options = parseArgs();
    await dumpMaterials(options);
  } catch (err) {
    console.error(`Fatal error: ${err.message}`);
    if (process.env.DEBUG) {
      console.error(err.stack);
    }
    process.exit(1);
  }
}

main();
