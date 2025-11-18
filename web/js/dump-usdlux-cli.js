#!/usr/bin/env node
// UsdLux Light Dump CLI Tool
// Usage: node dump-usdlux-cli.js <usd-file> [options]

import { TinyUSDZLoader } from './src/tinyusdz/TinyUSDZLoader.js';
import fs from 'node:fs';
import path from 'node:path';
import YAML from 'yaml';

// Parse command line arguments
function parseArgs() {
  const args = process.argv.slice(2);
  const options = {
    inputFile: null,
    format: 'json', // 'json', 'yaml', or 'summary'
    outputFile: null,
    lightId: null, // null means all lights
    pretty: true,
    verbose: false,
    showMeshes: false, // Show mesh light geometries
    showMaterials: false // Show material info for mesh lights
  };

  for (let i = 0; i < args.length; i++) {
    const arg = args[i];

    if (arg === '-h' || arg === '--help') {
      printHelp();
      process.exit(0);
    } else if (arg === '-f' || arg === '--format') {
      options.format = args[++i];
      if (!['json', 'yaml', 'summary'].includes(options.format)) {
        console.error('Error: Format must be "json", "yaml", or "summary"');
        process.exit(1);
      }
    } else if (arg === '-o' || arg === '--output') {
      options.outputFile = args[++i];
    } else if (arg === '-l' || arg === '--light') {
      options.lightId = parseInt(args[++i], 10);
    } else if (arg === '--no-pretty') {
      options.pretty = false;
    } else if (arg === '-v' || arg === '--verbose') {
      options.verbose = true;
    } else if (arg === '--show-meshes') {
      options.showMeshes = true;
    } else if (arg === '--show-materials') {
      options.showMaterials = true;
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
UsdLux Light Dump CLI Tool

Usage: node dump-usdlux-cli.js <usd-file> [options]

Arguments:
  <usd-file>              USD/USDA/USDC/USDZ file to load

Options:
  -f, --format <format>   Output format: 'json', 'yaml', or 'summary' (default: json)
  -o, --output <file>     Write output to file instead of stdout
  -l, --light <id>        Dump only specific light by ID (default: all)
  --no-pretty             Disable pretty-printing for JSON/YAML
  --show-meshes           Include mesh geometry data for mesh lights
  --show-materials        Include material data for mesh lights
  -v, --verbose           Enable verbose logging
  -h, --help              Show this help message

Examples:
  # Dump all lights as JSON
  node dump-usdlux-cli.js tests/feat/lux/04_complete_scene.usda

  # Dump as human-readable summary
  node dump-usdlux-cli.js tests/feat/lux/06_mesh_lights.usda -f summary

  # Dump specific light as YAML
  node dump-usdlux-cli.js tests/feat/lux/04_complete_scene.usda -f yaml -l 0

  # Save output to file with verbose logging
  node dump-usdlux-cli.js models/scene.usdc -o lights.json -v

  # Dump mesh lights with geometry and material info
  node dump-usdlux-cli.js tests/feat/lux/06_mesh_lights.usda --show-meshes --show-materials
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

function formatSummary(lightsData, verbose = false) {
  let output = '';

  output += `╔════════════════════════════════════════════════════════╗\n`;
  output += `║          UsdLux Lights Summary                         ║\n`;
  output += `╚════════════════════════════════════════════════════════╝\n\n`;
  output += `Total Lights: ${lightsData.length}\n\n`;

  for (let i = 0; i < lightsData.length; i++) {
    const light = lightsData[i];

    output += `\n┌─ Light ${i}: ${light.name || 'Unnamed'} ──────────────\n`;
    output += `│ Type: ${light.lightType}\n`;
    output += `│ Path: ${light.abs_path || 'N/A'}\n`;

    if (light.color) {
      const r = light.color[0].toFixed(3);
      const g = light.color[1].toFixed(3);
      const b = light.color[2].toFixed(3);
      output += `│ Color: RGB(${r}, ${g}, ${b})\n`;
    }

    if (light.intensity !== undefined) {
      output += `│ Intensity: ${light.intensity.toFixed(2)}\n`;
    }

    if (light.exposure !== undefined && light.exposure !== 0) {
      output += `│ Exposure: ${light.exposure.toFixed(2)} EV\n`;
      const effectiveIntensity = light.intensity * Math.pow(2, light.exposure);
      output += `│ Effective Intensity: ${effectiveIntensity.toFixed(2)}\n`;
    }

    if (light.enableColorTemperature && light.colorTemperature) {
      output += `│ Color Temperature: ${light.colorTemperature}K\n`;
    }

    // Type-specific properties
    if (light.lightType === 'Point' || light.lightType === 'Sphere') {
      if (light.radius !== undefined && light.radius > 0) {
        output += `│ Radius: ${light.radius.toFixed(3)}\n`;
      }
    } else if (light.lightType === 'Directional') {
      if (light.angle !== undefined && light.angle > 0) {
        output += `│ Angle: ${light.angle.toFixed(2)}°\n`;
      }
    } else if (light.lightType === 'Rect') {
      if (light.width !== undefined && light.height !== undefined) {
        output += `│ Dimensions: ${light.width.toFixed(2)} × ${light.height.toFixed(2)}\n`;
      }
      if (light.textureFile) {
        output += `│ Texture: ${light.textureFile}\n`;
      }
    } else if (light.lightType === 'Disk') {
      if (light.radius !== undefined) {
        output += `│ Radius: ${light.radius.toFixed(3)}\n`;
      }
    } else if (light.lightType === 'Cylinder') {
      if (light.radius !== undefined && light.length !== undefined) {
        output += `│ Radius: ${light.radius.toFixed(3)}, Length: ${light.length.toFixed(3)}\n`;
      }
      if (light.shapingConeAngle !== undefined && light.shapingConeAngle > 0) {
        output += `│ Shaping Cone Angle: ${light.shapingConeAngle.toFixed(2)}°\n`;
        output += `│ Cone Softness: ${(light.shapingConeSoftness || 0).toFixed(2)}\n`;
      }
    } else if (light.lightType === 'Dome') {
      if (light.textureFile) {
        output += `│ Texture: ${light.textureFile}\n`;
      }
      if (light.domeTextureFormat) {
        output += `│ Texture Format: ${light.domeTextureFormat}\n`;
      }
    } else if (light.lightType === 'Geometry') {
      output += `│ Mesh ID: ${light.geometry_mesh_id}\n`;
      if (light.geometry_mesh_name) {
        output += `│ Mesh Name: ${light.geometry_mesh_name}\n`;
      }
      if (light.material_sync_mode) {
        output += `│ Material Sync Mode: ${light.material_sync_mode}\n`;
      }
    }

    // Shadow properties
    if (light.shadowEnable) {
      output += `│ Shadows: Enabled\n`;
      if (verbose && light.shadowColor) {
        const r = light.shadowColor[0].toFixed(3);
        const g = light.shadowColor[1].toFixed(3);
        const b = light.shadowColor[2].toFixed(3);
        output += `│   Shadow Color: RGB(${r}, ${g}, ${b})\n`;
      }
    }

    // IES profile
    if (light.shapingIesFile) {
      output += `│ IES Profile: ${light.shapingIesFile}\n`;
    }

    output += `└─────────────────────────────────────────────\n`;
  }

  return output;
}

async function dumpLights(options) {
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
    console.error(`Scene contains:`);
    console.error(`  - ${usd.numMeshes()} mesh(es)`);
    console.error(`  - ${usd.numMaterials()} material(s)`);
    console.error(`  - ${usd.numLights()} light(s)`);
  }

  // Get number of lights
  const numLights = usd.numLights();

  if (options.verbose) {
    console.error(`\nFound ${numLights} light(s)`);
  }

  if (numLights === 0) {
    console.error('Warning: No lights found in USD file');
    if (!options.outputFile) {
      if (options.format === 'summary') {
        console.log('No lights found in scene.');
      } else {
        console.log(options.format === 'json' ? '[]' : 'lights: []');
      }
    }
    return;
  }

  const results = [];

  // Determine which lights to dump
  const lightIds = options.lightId !== null
    ? [options.lightId]
    : Array.from({ length: numLights }, (_, i) => i);

  // Dump lights
  for (const lightId of lightIds) {
    if (lightId >= numLights) {
      console.error(`Warning: Light ID ${lightId} out of range (0-${numLights - 1})`);
      continue;
    }

    if (options.verbose) {
      console.error(`\nProcessing light ${lightId}...`);
    }

    try {
      const result = usd.getLightWithFormat(lightId, 'json');

      if (result.error) {
        console.error(`Error getting light ${lightId}: ${result.error}`);
        continue;
      }

      const lightData = JSON.parse(result.data);

      if (options.verbose) {
        console.error(`  Type: ${lightData.lightType}`);
        console.error(`  Name: ${lightData.name || 'N/A'}`);
        if (lightData.lightType === 'Geometry') {
          console.error(`  Mesh ID: ${lightData.geometry_mesh_id}`);
        }
      }

      // Optionally fetch mesh data for geometry lights
      if (options.showMeshes && lightData.lightType === 'Geometry' && lightData.geometry_mesh_id !== undefined) {
        const meshId = lightData.geometry_mesh_id;
        if (meshId >= 0 && meshId < usd.numMeshes()) {
          const mesh = usd.getMesh(meshId);
          lightData.mesh_geometry = {
            primName: mesh.primName,
            numVertices: mesh.faceVertexIndices.length,
            numFaces: mesh.faceVertexCounts.length
          };
        }
      }

      // Optionally fetch material data for mesh lights
      if (options.showMaterials && lightData.lightType === 'Geometry') {
        // Find material bound to the mesh if available
        // This would require additional implementation
        lightData.material_info = 'Material lookup not yet implemented';
      }

      results.push(lightData);
    } catch (err) {
      console.error(`Error processing light ${lightId}: ${err.message}`);
      if (options.verbose) {
        console.error(err.stack);
      }
    }
  }

  // Output results
  let output;

  if (options.format === 'json') {
    output = options.pretty
      ? JSON.stringify(results, null, 2)
      : JSON.stringify(results);
  } else if (options.format === 'yaml') {
    output = YAML.stringify(results, {
      indent: 2,
      lineWidth: 0,
      minContentWidth: 0,
      defaultKeyType: 'PLAIN',
      defaultStringType: 'QUOTE_DOUBLE'
    });
  } else if (options.format === 'summary') {
    output = formatSummary(results, options.verbose);
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
    await dumpLights(options);
  } catch (err) {
    console.error(`Fatal error: ${err.message}`);
    if (process.env.DEBUG) {
      console.error(err.stack);
    }
    process.exit(1);
  }
}

main();
