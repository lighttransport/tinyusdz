#!/usr/bin/env node
// MaterialX NodeGraph Dump CLI Tool
// Dumps and verifies MaterialX shader node graph data from USD files
// Usage: node dump-mtlx-node.js <usd-file> [options]

import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';
import fs from 'node:fs';
import path from 'node:path';

// Parse command line arguments
function parseArgs() {
  const args = process.argv.slice(2);
  const options = {
    inputFile: null,
    materialId: null, // null means all materials
    outputFile: null,
    pretty: true,
    verbose: false,
    validateOnly: false,
    showConnections: false,
    showNodes: true,
    showOutputs: true,
    compact: false
  };

  for (let i = 0; i < args.length; i++) {
    const arg = args[i];

    if (arg === '-h' || arg === '--help') {
      printHelp();
      process.exit(0);
    } else if (arg === '-m' || arg === '--material') {
      options.materialId = parseInt(args[++i], 10);
    } else if (arg === '-o' || arg === '--output') {
      options.outputFile = args[++i];
    } else if (arg === '-v' || arg === '--verbose') {
      options.verbose = true;
    } else if (arg === '--validate') {
      options.validateOnly = true;
    } else if (arg === '--connections') {
      options.showConnections = true;
    } else if (arg === '--no-nodes') {
      options.showNodes = false;
    } else if (arg === '--no-outputs') {
      options.showOutputs = false;
    } else if (arg === '--compact') {
      options.compact = true;
      options.pretty = false;
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
MaterialX NodeGraph Dump CLI Tool

Dumps and verifies MaterialX shader node graph data from USD files.
The node graph contains shader nodes used in Blender MaterialX exports
(e.g., math operations, color adjustments, geometry nodes).

Usage: node dump-mtlx-node.js <usd-file> [options]

Arguments:
  <usd-file>              USD/USDA/USDC/USDZ file to load

Options:
  -m, --material <id>     Dump only specific material by ID (default: all)
  -o, --output <file>     Write output to file instead of stdout
  -v, --verbose           Enable verbose logging
  --validate              Only validate node graph structure, don't dump
  --connections           Show connection details between nodes
  --no-nodes              Don't show individual node details
  --no-outputs            Don't show nodegraph outputs
  --compact               Compact output (single line JSON)
  -h, --help              Show this help message

Examples:
  # Dump all node graphs
  node dump-mtlx-node.js models/invert-material.usda

  # Dump specific material's node graph
  node dump-mtlx-node.js models/suzanne-pbr.usda -m 1

  # Validate node graph structure only
  node dump-mtlx-node.js models/test.usda --validate

  # Show node connections in detail
  node dump-mtlx-node.js models/test.usda --connections -v

  # Save to file
  node dump-mtlx-node.js models/test.usda -o nodegraph.json

Node Graph Structure:
  The node graph JSON contains:
  - version: MaterialX version (e.g., "1.39")
  - nodegraph.name: NodeGraph name
  - nodegraph.nodes[]: Array of shader nodes
    - name: Node instance name
    - category: Node type (e.g., "image", "multiply", "mix")
    - type: MaterialX node definition (e.g., "ND_multiply_color3")
    - inputs[]: Node inputs (values or connections)
    - outputs[]: Node outputs
  - nodegraph.outputs[]: NodeGraph outputs (connections to shader inputs)
  - connections[]: Shader parameter connections to node outputs
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

// Validate node graph structure
function validateNodeGraph(nodeGraph, materialName) {
  const errors = [];
  const warnings = [];

  if (!nodeGraph) {
    errors.push('NodeGraph is null or undefined');
    return { valid: false, errors, warnings };
  }

  // Check version
  if (!nodeGraph.version) {
    warnings.push('Missing version field');
  }

  // Check nodegraph object
  if (!nodeGraph.nodegraph) {
    errors.push('Missing nodegraph object');
    return { valid: errors.length === 0, errors, warnings };
  }

  const ng = nodeGraph.nodegraph;

  // Check nodes array
  if (!Array.isArray(ng.nodes)) {
    errors.push('nodegraph.nodes is not an array');
  } else {
    const nodeNames = new Set();

    for (let i = 0; i < ng.nodes.length; i++) {
      const node = ng.nodes[i];

      // Check required fields
      if (!node.name) {
        errors.push(`Node ${i}: missing name`);
      } else {
        if (nodeNames.has(node.name)) {
          errors.push(`Node ${i}: duplicate name "${node.name}"`);
        }
        nodeNames.add(node.name);
      }

      if (!node.category) {
        warnings.push(`Node "${node.name || i}": missing category`);
      }

      if (!node.type) {
        warnings.push(`Node "${node.name || i}": missing type (info:id)`);
      }

      // Check inputs
      if (node.inputs) {
        if (!Array.isArray(node.inputs)) {
          errors.push(`Node "${node.name || i}": inputs is not an array`);
        } else {
          for (let j = 0; j < node.inputs.length; j++) {
            const input = node.inputs[j];
            if (!input.name) {
              errors.push(`Node "${node.name || i}" input ${j}: missing name`);
            }
            // Input should have either value or nodename (connection)
            if (input.value === undefined && !input.nodename) {
              warnings.push(`Node "${node.name || i}" input "${input.name || j}": no value or connection`);
            }
          }
        }
      }
    }

    // Validate connections reference existing nodes
    for (const node of ng.nodes) {
      if (node.inputs) {
        for (const input of node.inputs) {
          if (input.nodename && !nodeNames.has(input.nodename)) {
            errors.push(`Node "${node.name}" input "${input.name}": references unknown node "${input.nodename}"`);
          }
        }
      }
    }
  }

  // Check outputs
  if (ng.outputs) {
    if (!Array.isArray(ng.outputs)) {
      errors.push('nodegraph.outputs is not an array');
    } else {
      for (let i = 0; i < ng.outputs.length; i++) {
        const output = ng.outputs[i];
        if (!output.name) {
          warnings.push(`NodeGraph output ${i}: missing name`);
        }
        if (!output.nodename) {
          warnings.push(`NodeGraph output "${output.name || i}": missing nodename reference`);
        }
      }
    }
  }

  return {
    valid: errors.length === 0,
    errors,
    warnings
  };
}

// Extract node graph summary
function summarizeNodeGraph(nodeGraph) {
  if (!nodeGraph || !nodeGraph.nodegraph) {
    return null;
  }

  const ng = nodeGraph.nodegraph;
  const nodes = ng.nodes || [];

  // Count node types
  const nodeTypes = {};
  const nodeCategories = {};

  for (const node of nodes) {
    const type = node.type || 'unknown';
    const category = node.category || 'unknown';

    nodeTypes[type] = (nodeTypes[type] || 0) + 1;
    nodeCategories[category] = (nodeCategories[category] || 0) + 1;
  }

  // Count connections
  let connectionCount = 0;
  for (const node of nodes) {
    if (node.inputs) {
      for (const input of node.inputs) {
        if (input.nodename) {
          connectionCount++;
        }
      }
    }
  }

  return {
    version: nodeGraph.version,
    nodeCount: nodes.length,
    outputCount: (ng.outputs || []).length,
    connectionCount,
    nodeCategories,
    nodeTypes
  };
}

// Format node for display
function formatNode(node, options) {
  if (options.compact) {
    return {
      name: node.name,
      type: node.type,
      inputs: (node.inputs || []).length,
      outputs: (node.outputs || []).length
    };
  }

  const result = {
    name: node.name,
    category: node.category,
    type: node.type
  };

  if (node.inputs && node.inputs.length > 0) {
    result.inputs = node.inputs.map(input => {
      const inputInfo = { name: input.name };

      if (input.nodename) {
        inputInfo.connection = {
          nodename: input.nodename,
          output: input.output || 'out'
        };
      } else if (input.value !== undefined) {
        inputInfo.type = input.type;
        inputInfo.value = input.value;
      }

      if (input.colorspace) {
        inputInfo.colorspace = input.colorspace;
      }

      return inputInfo;
    });
  }

  if (options.showConnections && node.outputs && node.outputs.length > 0) {
    result.outputs = node.outputs;
  }

  return result;
}

async function dumpNodeGraphs(options) {
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

  // Parse USD file
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

  // Get number of materials
  const numMaterials = usd.numMaterials();

  if (options.verbose) {
    console.error(`Found ${numMaterials} material(s)`);
  }

  if (numMaterials === 0) {
    console.error('Warning: No materials found in USD file');
    if (!options.outputFile) {
      console.log('[]');
    }
    return;
  }

  const results = [];

  // Determine which materials to process
  const materialIds = options.materialId !== null
    ? [options.materialId]
    : Array.from({ length: numMaterials }, (_, i) => i);

  // Process materials
  for (const matId of materialIds) {
    if (matId >= numMaterials) {
      console.error(`Warning: Material ID ${matId} out of range (0-${numMaterials - 1})`);
      continue;
    }

    if (options.verbose) {
      console.error(`\nProcessing material ${matId}...`);
    }

    try {
      const result = usd.getMaterialWithFormat(matId, 'json');

      if (result.error) {
        console.error(`Error getting material ${matId}: ${result.error}`);
        continue;
      }

      const materialData = JSON.parse(result.data);
      const materialName = materialData.name || `Material_${matId}`;

      // Check for nodeGraph in openPBR
      let nodeGraph = null;
      let nodeGraphSource = null;

      if (materialData.openPBR && materialData.openPBR.nodeGraph) {
        nodeGraph = materialData.openPBR.nodeGraph;
        nodeGraphSource = 'openPBR';
      }

      if (!nodeGraph) {
        if (options.verbose) {
          console.error(`  Material "${materialName}": No nodeGraph found`);
        }
        continue;
      }

      if (options.verbose) {
        console.error(`  Material "${materialName}": Found nodeGraph (source: ${nodeGraphSource})`);
      }

      // Validate node graph
      const validation = validateNodeGraph(nodeGraph, materialName);

      if (options.validateOnly) {
        const validationResult = {
          materialId: matId,
          materialName,
          valid: validation.valid,
          summary: summarizeNodeGraph(nodeGraph)
        };

        if (validation.errors.length > 0) {
          validationResult.errors = validation.errors;
        }
        if (validation.warnings.length > 0) {
          validationResult.warnings = validation.warnings;
        }

        results.push(validationResult);

        if (options.verbose) {
          if (validation.valid) {
            console.error(`    Validation: PASSED`);
          } else {
            console.error(`    Validation: FAILED`);
            for (const err of validation.errors) {
              console.error(`      Error: ${err}`);
            }
          }
          for (const warn of validation.warnings) {
            console.error(`      Warning: ${warn}`);
          }
        }

        continue;
      }

      // Build result object
      const matResult = {
        materialId: matId,
        materialName,
        hasOpenPBR: !!materialData.hasOpenPBR,
        nodeGraphSource,
        validation: {
          valid: validation.valid,
          errorCount: validation.errors.length,
          warningCount: validation.warnings.length
        }
      };

      if (validation.errors.length > 0) {
        matResult.validation.errors = validation.errors;
      }
      if (validation.warnings.length > 0) {
        matResult.validation.warnings = validation.warnings;
      }

      // Add summary
      matResult.summary = summarizeNodeGraph(nodeGraph);

      // Add node graph data
      if (options.showNodes) {
        const ng = nodeGraph.nodegraph;
        matResult.nodes = (ng.nodes || []).map(node => formatNode(node, options));
      }

      if (options.showOutputs && nodeGraph.nodegraph.outputs) {
        matResult.outputs = nodeGraph.nodegraph.outputs;
      }

      if (options.showConnections && nodeGraph.connections) {
        matResult.shaderConnections = nodeGraph.connections;
      }

      // Include raw nodeGraph if verbose
      if (options.verbose && !options.compact) {
        matResult.rawNodeGraph = nodeGraph;
      }

      results.push(matResult);

    } catch (err) {
      console.error(`Error processing material ${matId}: ${err.message}`);
      if (options.verbose) {
        console.error(err.stack);
      }
    }
  }

  // Output results
  let output;
  if (options.compact) {
    output = JSON.stringify(results);
  } else if (options.pretty) {
    output = JSON.stringify(results, null, 2);
  } else {
    output = JSON.stringify(results);
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

  // Print summary for validation mode
  if (options.validateOnly) {
    const passed = results.filter(r => r.valid).length;
    const failed = results.filter(r => !r.valid).length;
    console.error(`\nValidation Summary: ${passed} passed, ${failed} failed out of ${results.length} materials with node graphs`);
  }

  if (options.verbose) {
    console.error('\nDone!');
  }
}

// Main execution
async function main() {
  try {
    const options = parseArgs();
    await dumpNodeGraphs(options);
  } catch (err) {
    console.error(`Fatal error: ${err.message}`);
    if (process.env.DEBUG) {
      console.error(err.stack);
    }
    process.exit(1);
  }
}

main();
