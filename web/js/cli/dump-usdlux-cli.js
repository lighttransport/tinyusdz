#!/usr/bin/env node
// UsdLux Light Dump CLI Tool
// Usage: node dump-usdlux-cli.js <usd-file> [options]

import { TinyUSDZLoader } from '../src/tinyusdz/TinyUSDZLoader.js';
import fs from 'node:fs';
import path from 'node:path';
import YAML from 'yaml';

// Parse command line arguments
function parseArgs() {
  const args = process.argv.slice(2);
  const options = {
    inputFile: null,
    format: 'json', // 'json', 'yaml', 'summary', or 'xml'
    outputFile: null,
    lightId: null, // null means all lights
    pretty: true,
    verbose: false,
    showMeshes: false, // Show mesh light geometries
    showMaterials: false, // Show material info for mesh lights
    showTransform: false, // Show transform matrices
    showSpectral: false, // Show spectral emission data
    showAll: false, // Show all optional data
    serialized: false, // Use serialized format from getLightWithFormat
    colorMode: 'rgb', // 'rgb', 'hex', or 'kelvin'
    showNodes: false // Show node hierarchy with kind info
  };

  for (let i = 0; i < args.length; i++) {
    const arg = args[i];

    if (arg === '-h' || arg === '--help') {
      printHelp();
      process.exit(0);
    } else if (arg === '-f' || arg === '--format') {
      options.format = args[++i];
      if (!['json', 'yaml', 'summary', 'xml'].includes(options.format)) {
        console.error('Error: Format must be "json", "yaml", "summary", or "xml"');
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
    } else if (arg === '--show-transform' || arg === '-t') {
      options.showTransform = true;
    } else if (arg === '--show-spectral' || arg === '-s') {
      options.showSpectral = true;
    } else if (arg === '--all' || arg === '-a') {
      options.showAll = true;
      options.showMeshes = true;
      options.showMaterials = true;
      options.showTransform = true;
      options.showSpectral = true;
      options.showNodes = true;
    } else if (arg === '--show-nodes' || arg === '-n') {
      options.showNodes = true;
    } else if (arg === '--serialized') {
      options.serialized = true;
    } else if (arg === '--color-mode') {
      options.colorMode = args[++i];
      if (!['rgb', 'hex', 'kelvin'].includes(options.colorMode)) {
        console.error('Error: Color mode must be "rgb", "hex", or "kelvin"');
        process.exit(1);
      }
    } else if (!options.inputFile) {
      options.inputFile = arg;
    }
  }

  if (!options.inputFile) {
    console.error('Error: Input USD file is required');
    printHelp();
    process.exit(1);
  }

  // XML format requires serialized mode
  if (options.format === 'xml') {
    options.serialized = true;
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
  -f, --format <format>   Output format: 'json', 'yaml', 'summary', or 'xml' (default: json)
  -o, --output <file>     Write output to file instead of stdout
  -l, --light <id>        Dump only specific light by ID (default: all)
  --no-pretty             Disable pretty-printing for JSON/YAML
  -v, --verbose           Enable verbose logging
  -a, --all               Show all optional data (meshes, materials, transform, spectral, nodes)
  -t, --show-transform    Include transform matrices in output
  -s, --show-spectral     Include spectral emission data (LTE SpectralAPI)
  -n, --show-nodes        Show node hierarchy with category/type info
  --show-meshes           Include mesh geometry data for mesh lights
  --show-materials        Include material data for mesh lights
  --serialized            Use serialized format from WASM (required for XML)
  --color-mode <mode>     Color display mode: 'rgb', 'hex', or 'kelvin' (default: rgb)
  -h, --help              Show this help message

Light Types Supported:
  - Point       Small spherical light source
  - Sphere      Spherical area light (SphereLight)
  - Disk        Circular area light (DiskLight)
  - Rect        Rectangular area light (RectLight)
  - Cylinder    Cylindrical area light (CylinderLight)
  - Distant     Directional light (DistantLight/sun)
  - Dome        Environment/IBL light (DomeLight)
  - Geometry    Mesh emissive light (GeometryLight)
  - Portal      Light portal

Examples:
  # Dump all lights as JSON
  node dump-usdlux-cli.js tests/feat/lux/04_complete_scene.usda

  # Dump as human-readable summary
  node dump-usdlux-cli.js tests/feat/lux/06_mesh_lights.usda -f summary

  # Dump specific light as YAML with all details
  node dump-usdlux-cli.js tests/feat/lux/04_complete_scene.usda -f yaml -l 0 --all

  # Save output to file with verbose logging
  node dump-usdlux-cli.js models/scene.usdc -o lights.json -v

  # Dump with spectral emission data
  node dump-usdlux-cli.js tests/usda/usdlux_advanced_features.usda --show-spectral

  # Export as XML (requires --serialized)
  node dump-usdlux-cli.js tests/feat/lux/04_complete_scene.usda -f xml -o lights.xml

  # Dump mesh lights with geometry and material info
  node dump-usdlux-cli.js tests/feat/lux/06_mesh_lights.usda --show-meshes --show-materials

  # Dump with node hierarchy (shows nodeCategory for each node)
  node dump-usdlux-cli.js tests/feat/lux/04_complete_scene.usda -f summary --show-nodes

  # Dump JSON with node hierarchy including nodeCategory
  node dump-usdlux-cli.js tests/feat/lux/04_complete_scene.usda -f json --show-nodes
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

// Convert RGB to hex color
function rgbToHex(r, g, b) {
  const toHex = (c) => {
    const hex = Math.round(Math.max(0, Math.min(255, c * 255))).toString(16);
    return hex.length === 1 ? '0' + hex : hex;
  };
  return `#${toHex(r)}${toHex(g)}${toHex(b)}`;
}

// Estimate color temperature from RGB (simplified Kelvin approximation)
function rgbToKelvin(r, g, b) {
  // Simple heuristic based on red/blue ratio
  if (b === 0) return '>10000K (warm)';
  const ratio = r / b;
  if (ratio > 2) return '~2700K (warm white)';
  if (ratio > 1.5) return '~3500K (neutral)';
  if (ratio > 1) return '~5000K (daylight)';
  if (ratio > 0.7) return '~6500K (cool daylight)';
  return '~10000K+ (blue sky)';
}

// Format color based on mode
function formatColor(color, mode = 'rgb') {
  if (!color || color.length < 3) return 'N/A';
  const [r, g, b] = color;

  switch (mode) {
    case 'hex':
      return rgbToHex(r, g, b);
    case 'kelvin':
      return rgbToKelvin(r, g, b);
    case 'rgb':
    default:
      return `RGB(${r.toFixed(3)}, ${g.toFixed(3)}, ${b.toFixed(3)})`;
  }
}

// Format a 4x4 matrix for display
function formatMatrix(transform, indent = '│   ') {
  if (!transform || transform.length !== 16) return `${indent}[Invalid matrix]\n`;

  let output = '';
  for (let row = 0; row < 4; row++) {
    const values = [];
    for (let col = 0; col < 4; col++) {
      values.push(transform[row * 4 + col].toFixed(4).padStart(10));
    }
    output += `${indent}[${values.join(', ')}]\n`;
  }
  return output;
}

// Format spectral emission data
function formatSpectralEmission(spectral, indent = '│   ') {
  if (!spectral) return '';

  let output = `${indent}Interpolation: ${spectral.interpolation || 'linear'}\n`;
  output += `${indent}Unit: ${spectral.unit || 'nanometers'}\n`;

  if (spectral.preset && spectral.preset !== 'none') {
    output += `${indent}Preset: ${spectral.preset.toUpperCase()}\n`;
  }

  if (spectral.samples && spectral.samples.length > 0) {
    output += `${indent}Samples (${spectral.samples.length} points):\n`;
    const maxSamples = 10;
    const samples = spectral.samples.slice(0, maxSamples);
    for (const [wavelength, value] of samples) {
      output += `${indent}  ${wavelength.toFixed(1)}nm: ${value.toFixed(6)}\n`;
    }
    if (spectral.samples.length > maxSamples) {
      output += `${indent}  ... and ${spectral.samples.length - maxSamples} more\n`;
    }
  }

  return output;
}

// Format a single node recursively
function formatNodeRec(node, indent = '  ', depth = 0) {
  let output = '';
  const prefix = indent.repeat(depth);
  const categoryStr = node.nodeCategory ? `[${node.nodeCategory}]` : '';
  const typeStr = node.nodeType || 'unknown';
  const idStr = node.contentId >= 0 ? `#${node.contentId}` : '';

  output += `${prefix}├── ${node.primName} ${categoryStr} (${typeStr}) ${idStr}\n`;
  output += `${prefix}│   Path: ${node.absPath}\n`;

  if (node.displayName) {
    output += `${prefix}│   Display: ${node.displayName}\n`;
  }

  if (node.children && node.children.length > 0) {
    for (const child of node.children) {
      output += formatNodeRec(child, indent, depth + 1);
    }
  }

  return output;
}

// Format node hierarchy for summary output
function formatNodeHierarchy(usd) {
  let output = '';
  output += `\n  ╔════════════════════════════════════════════════════════════════╗\n`;
  output += `  ║              Node Hierarchy (with Category)                    ║\n`;
  output += `  ╚════════════════════════════════════════════════════════════════╝\n\n`;

  const numRootNodes = usd.numRootNodes();
  output += `  Root Nodes: ${numRootNodes}\n`;
  output += `  Default Root: ${usd.getDefaultRootNodeId()}\n\n`;

  // Get and format the default root node
  const rootNode = usd.getDefaultRootNode();
  if (rootNode && rootNode.primName) {
    output += `  Node Tree:\n`;
    output += formatNodeRec(rootNode, '  ', 1);
  } else {
    output += `  (No nodes found)\n`;
  }

  return output;
}

function formatSummary(lightsData, options = {}) {
  const { verbose = false, showTransform = false, showSpectral = false, colorMode = 'rgb' } = options;
  let output = '';

  output += `\n`;
  output += `  ╔════════════════════════════════════════════════════════════════╗\n`;
  output += `  ║              UsdLux Lights Summary                             ║\n`;
  output += `  ╚════════════════════════════════════════════════════════════════╝\n\n`;

  // Group lights by type
  const byType = {};
  for (const light of lightsData) {
    const type = light.type || 'unknown';
    if (!byType[type]) byType[type] = [];
    byType[type].push(light);
  }

  output += `  Total Lights: ${lightsData.length}\n`;
  output += `  By Type: ${Object.entries(byType).map(([t, l]) => `${t}(${l.length})`).join(', ')}\n`;

  for (let i = 0; i < lightsData.length; i++) {
    const light = lightsData[i];
    const lightType = light.type || 'unknown';

    output += `\n  ┌─── Light ${i}: ${light.name || 'Unnamed'} ${'─'.repeat(Math.max(0, 40 - (light.name || 'Unnamed').length))}\n`;
    output += `  │ Type: ${lightType}\n`;
    output += `  │ Path: ${light.absPath || light.abs_path || 'N/A'}\n`;

    if (light.displayName) {
      output += `  │ Display Name: ${light.displayName}\n`;
    }

    // Color
    if (light.color) {
      output += `  │ Color: ${formatColor(light.color, colorMode)}\n`;
    }

    // Intensity & Exposure
    if (light.intensity !== undefined) {
      output += `  │ Intensity: ${light.intensity.toFixed(3)}\n`;
    }

    if (light.exposure !== undefined && light.exposure !== 0) {
      output += `  │ Exposure: ${light.exposure.toFixed(2)} EV\n`;
      const effectiveIntensity = light.intensity * Math.pow(2, light.exposure);
      output += `  │ Effective Intensity: ${effectiveIntensity.toFixed(3)}\n`;
    }

    // Color temperature
    if (light.enableColorTemperature && light.colorTemperature) {
      output += `  │ Color Temperature: ${light.colorTemperature.toFixed(0)}K`;
      if (light.colorTemperature < 3000) output += ' (warm)';
      else if (light.colorTemperature < 5000) output += ' (neutral)';
      else if (light.colorTemperature < 7000) output += ' (daylight)';
      else output += ' (cool)';
      output += '\n';
    }

    // Diffuse/Specular multipliers
    if (verbose) {
      if (light.diffuse !== undefined && light.diffuse !== 1.0) {
        output += `  │ Diffuse Multiplier: ${light.diffuse.toFixed(3)}\n`;
      }
      if (light.specular !== undefined && light.specular !== 1.0) {
        output += `  │ Specular Multiplier: ${light.specular.toFixed(3)}\n`;
      }
      if (light.normalize) {
        output += `  │ Normalize: true (power normalized by area)\n`;
      }
    }

    // Type-specific properties
    output += `  │\n`;
    output += `  │ ── Type-Specific Properties ──\n`;

    if (lightType === 'point' || lightType === 'sphere') {
      if (light.radius !== undefined && light.radius > 0) {
        output += `  │ Radius: ${light.radius.toFixed(4)}\n`;
      }
    } else if (lightType === 'distant') {
      if (light.angle !== undefined && light.angle > 0) {
        output += `  │ Angular Diameter: ${light.angle.toFixed(2)}°\n`;
      }
    } else if (lightType === 'rect') {
      if (light.width !== undefined && light.height !== undefined) {
        output += `  │ Dimensions: ${light.width.toFixed(3)} × ${light.height.toFixed(3)}\n`;
        output += `  │ Area: ${(light.width * light.height).toFixed(4)} sq units\n`;
      }
      if (light.textureFile) {
        output += `  │ Texture: ${light.textureFile}\n`;
      }
    } else if (lightType === 'disk') {
      if (light.radius !== undefined) {
        output += `  │ Radius: ${light.radius.toFixed(4)}\n`;
        output += `  │ Area: ${(Math.PI * light.radius * light.radius).toFixed(4)} sq units\n`;
      }
    } else if (lightType === 'cylinder') {
      if (light.radius !== undefined && light.length !== undefined) {
        output += `  │ Radius: ${light.radius.toFixed(4)}\n`;
        output += `  │ Length: ${light.length.toFixed(4)}\n`;
        output += `  │ Surface Area: ${(2 * Math.PI * light.radius * light.length).toFixed(4)} sq units\n`;
      }
    } else if (lightType === 'dome') {
      if (light.textureFile) {
        output += `  │ Environment Map: ${light.textureFile}\n`;
      }
      if (light.domeTextureFormat) {
        output += `  │ Texture Format: ${light.domeTextureFormat}\n`;
      }
      if (light.guideRadius !== undefined) {
        output += `  │ Guide Radius: ${light.guideRadius.toExponential(2)}\n`;
      }
      if (light.envmapTextureId !== undefined && light.envmapTextureId >= 0) {
        output += `  │ Environment Texture ID: ${light.envmapTextureId}\n`;
      }
    } else if (lightType === 'geometry') {
      output += `  │ Geometry Mesh ID: ${light.geometryMeshId ?? light.geometry_mesh_id ?? 'N/A'}\n`;
      if (light.materialSyncMode || light.material_sync_mode) {
        output += `  │ Material Sync Mode: ${light.materialSyncMode || light.material_sync_mode}\n`;
      }
      if (light.mesh_geometry) {
        output += `  │ Mesh: ${light.mesh_geometry.primName}\n`;
        output += `  │   Vertices: ${light.mesh_geometry.numVertices}\n`;
        output += `  │   Faces: ${light.mesh_geometry.numFaces}\n`;
      }
    } else if (lightType === 'portal') {
      output += `  │ (Portal light - no additional properties)\n`;
    }

    // Shaping properties (spotlight/IES)
    const hasShaping = (light.shapingConeAngle !== undefined && light.shapingConeAngle < 90) ||
                       light.shapingIesFile ||
                       (light.shapingFocus !== undefined && light.shapingFocus !== 0);

    if (hasShaping) {
      output += `  │\n`;
      output += `  │ ── Shaping (Spotlight/IES) ──\n`;

      if (light.shapingConeAngle !== undefined && light.shapingConeAngle < 90) {
        output += `  │ Cone Angle: ${light.shapingConeAngle.toFixed(2)}°\n`;
        output += `  │ Cone Softness: ${(light.shapingConeSoftness || 0).toFixed(3)}\n`;
      }
      if (light.shapingFocus !== undefined && light.shapingFocus !== 0) {
        output += `  │ Focus: ${light.shapingFocus.toFixed(3)}\n`;
      }
      if (light.shapingFocusTint && (light.shapingFocusTint[0] !== 0 || light.shapingFocusTint[1] !== 0 || light.shapingFocusTint[2] !== 0)) {
        output += `  │ Focus Tint: ${formatColor(light.shapingFocusTint, colorMode)}\n`;
      }
      if (light.shapingIesFile) {
        output += `  │ IES Profile: ${light.shapingIesFile}\n`;
        if (light.shapingIesAngleScale !== undefined && light.shapingIesAngleScale !== 0) {
          output += `  │ IES Angle Scale: ${light.shapingIesAngleScale.toFixed(3)}\n`;
        }
        if (light.shapingIesNormalize) {
          output += `  │ IES Normalize: true\n`;
        }
      }
    }

    // Shadow properties
    if (light.shadowEnable !== undefined) {
      output += `  │\n`;
      output += `  │ ── Shadow ──\n`;
      output += `  │ Enabled: ${light.shadowEnable ? 'Yes' : 'No'}\n`;

      if (light.shadowEnable) {
        if (light.shadowColor) {
          const isBlack = light.shadowColor[0] === 0 && light.shadowColor[1] === 0 && light.shadowColor[2] === 0;
          if (!isBlack || verbose) {
            output += `  │ Color: ${formatColor(light.shadowColor, colorMode)}\n`;
          }
        }
        if (light.shadowDistance !== undefined && light.shadowDistance > 0) {
          output += `  │ Distance: ${light.shadowDistance.toFixed(2)}\n`;
        }
        if (light.shadowFalloff !== undefined && light.shadowFalloff > 0) {
          output += `  │ Falloff: ${light.shadowFalloff.toFixed(3)}\n`;
        }
        if (light.shadowFalloffGamma !== undefined && light.shadowFalloffGamma !== 1.0) {
          output += `  │ Falloff Gamma: ${light.shadowFalloffGamma.toFixed(3)}\n`;
        }
      }
    }

    // Position/Direction
    if (light.position || light.direction) {
      output += `  │\n`;
      output += `  │ ── Spatial ──\n`;
      if (light.position) {
        output += `  │ Position: (${light.position[0].toFixed(3)}, ${light.position[1].toFixed(3)}, ${light.position[2].toFixed(3)})\n`;
      }
      if (light.direction) {
        output += `  │ Direction: (${light.direction[0].toFixed(3)}, ${light.direction[1].toFixed(3)}, ${light.direction[2].toFixed(3)})\n`;
      }
    }

    // Transform matrix
    if (showTransform && light.transform) {
      output += `  │\n`;
      output += `  │ ── Transform Matrix ──\n`;
      output += formatMatrix(light.transform, '  │   ');
    }

    // Spectral emission (LTE SpectralAPI)
    if (showSpectral && light.spectralEmission) {
      output += `  │\n`;
      output += `  │ ── Spectral Emission (LTE SpectralAPI) ──\n`;
      output += formatSpectralEmission(light.spectralEmission, '  │   ');
    }

    output += `  └${'─'.repeat(60)}\n`;
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
      } else if (options.format === 'xml') {
        console.log('<?xml version="1.0"?>\n<lights/>');
      } else {
        console.log(options.format === 'json' ? '[]' : 'lights: []');
      }
    }
    return;
  }

  const results = [];
  const xmlResults = [];

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
      let lightData;

      if (options.serialized) {
        // Use serialized format
        const formatType = options.format === 'xml' ? 'xml' : 'json';
        const result = usd.getLightWithFormat(lightId, formatType);

        if (result.error) {
          console.error(`Error getting light ${lightId}: ${result.error}`);
          continue;
        }

        if (options.format === 'xml') {
          xmlResults.push(result.data);
          continue;
        }

        lightData = JSON.parse(result.data);
      } else {
        // Use direct object format (more comprehensive)
        lightData = usd.getLight(lightId);

        if (lightData.error) {
          console.error(`Error getting light ${lightId}: ${lightData.error}`);
          continue;
        }
      }

      if (options.verbose) {
        console.error(`  Type: ${lightData.type}`);
        console.error(`  Name: ${lightData.name || 'N/A'}`);
        if (lightData.type === 'geometry') {
          console.error(`  Mesh ID: ${lightData.geometryMeshId}`);
        }
        if (lightData.spectralEmission) {
          console.error(`  Has Spectral Emission: yes`);
        }
      }

      // Optionally fetch mesh data for geometry lights
      if (options.showMeshes && lightData.type === 'geometry') {
        const meshId = lightData.geometryMeshId ?? lightData.geometry_mesh_id;
        if (meshId !== undefined && meshId >= 0 && meshId < usd.numMeshes()) {
          const mesh = usd.getMeshCopy(meshId);
          lightData.mesh_geometry = {
            primName: mesh.primName,
            numVertices: mesh.faceVertexIndices?.length || 0,
            numFaces: mesh.faceVertexCounts?.length || 0
          };
        }
      }

      // Optionally fetch material data for mesh lights
      if (options.showMaterials && lightData.type === 'geometry') {
        // Find material bound to the mesh if available
        const meshId = lightData.geometryMeshId ?? lightData.geometry_mesh_id;
        if (meshId !== undefined && meshId >= 0 && meshId < usd.numMeshes()) {
          const mesh = usd.getMeshCopy(meshId);
          if (mesh.materialId !== undefined && mesh.materialId >= 0) {
            const material = usd.getMaterial(mesh.materialId);
            lightData.material_info = {
              id: mesh.materialId,
              name: material?.name || 'Unknown'
            };
          }
        }
      }

      // Filter out transform if not requested (to reduce output size)
      if (!options.showTransform && !options.showAll) {
        delete lightData.transform;
      }

      // Filter out spectral if not requested
      if (!options.showSpectral && !options.showAll) {
        delete lightData.spectralEmission;
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
    let jsonOutput = { lights: results };

    // Add node hierarchy if requested
    if (options.showNodes) {
      const rootNode = usd.getDefaultRootNode();
      if (rootNode && rootNode.primName) {
        jsonOutput.nodeHierarchy = rootNode;
      }
    }

    output = options.pretty
      ? JSON.stringify(jsonOutput, null, 2)
      : JSON.stringify(jsonOutput);
  } else if (options.format === 'yaml') {
    let yamlOutput = { lights: results };

    // Add node hierarchy if requested
    if (options.showNodes) {
      const rootNode = usd.getDefaultRootNode();
      if (rootNode && rootNode.primName) {
        yamlOutput.nodeHierarchy = rootNode;
      }
    }

    output = YAML.stringify(yamlOutput, {
      indent: 2,
      lineWidth: 0,
      minContentWidth: 0,
      defaultKeyType: 'PLAIN',
      defaultStringType: 'QUOTE_DOUBLE'
    });
  } else if (options.format === 'xml') {
    // Combine XML results
    output = '<?xml version="1.0" encoding="UTF-8"?>\n<lights>\n';
    for (const xml of xmlResults) {
      // Indent each light XML
      const indented = xml.split('\n').map(line => '  ' + line).join('\n');
      output += indented + '\n';
    }
    output += '</lights>';
  } else if (options.format === 'summary') {
    output = formatSummary(results, {
      verbose: options.verbose,
      showTransform: options.showTransform || options.showAll,
      showSpectral: options.showSpectral || options.showAll,
      colorMode: options.colorMode
    });

    // Append node hierarchy if requested
    if (options.showNodes) {
      output += formatNodeHierarchy(usd);
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
