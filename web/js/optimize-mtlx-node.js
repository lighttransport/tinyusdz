#!/usr/bin/env node
/**
 * MaterialX NodeGraph Optimizer CLI Tool
 *
 * Analyzes and optimizes MaterialX node graphs from USD files.
 * Uses the optimizer from TinyUSDZMaterialX.js module.
 *
 * Usage: npx vite-node optimize-mtlx-node.js -- <usd-file> [options]
 */

import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';
import {
    optimizeNodeGraph,
    analyzeNodeGraph,
    getOptimizationSummary,
    NodeGraphOptimizationLevel
} from 'tinyusdz/TinyUSDZMaterialX.js';
import fs from 'node:fs';
import path from 'node:path';

// Parse command line arguments
function parseArgs() {
    const args = process.argv.slice(2);
    const options = {
        inputFile: null,
        materialId: null,
        outputFile: null,
        level: NodeGraphOptimizationLevel.STANDARD,
        analyzeOnly: false,
        verbose: false,
        showBefore: false,
        showAfter: true,
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
        } else if (arg === '-l' || arg === '--level') {
            const level = args[++i].toLowerCase();
            switch (level) {
                case 'none': options.level = NodeGraphOptimizationLevel.NONE; break;
                case 'basic': options.level = NodeGraphOptimizationLevel.BASIC; break;
                case 'standard': options.level = NodeGraphOptimizationLevel.STANDARD; break;
                case 'aggressive': options.level = NodeGraphOptimizationLevel.AGGRESSIVE; break;
                default:
                    console.error(`Unknown optimization level: ${level}`);
                    process.exit(1);
            }
        } else if (arg === '--analyze') {
            options.analyzeOnly = true;
        } else if (arg === '-v' || arg === '--verbose') {
            options.verbose = true;
        } else if (arg === '--show-before') {
            options.showBefore = true;
        } else if (arg === '--no-after') {
            options.showAfter = false;
        } else if (arg === '--compact') {
            options.compact = true;
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
MaterialX NodeGraph Optimizer CLI Tool

Analyzes and optimizes MaterialX node graphs by detecting common patterns
and replacing multi-node patterns with single high-level operations.

Optimization Patterns:
  - Invert: constant -> subtract([1,1,1], x) -> mix => invert(x, factor)
  - Brightness/Contrast: multiply -> add -> subtract -> max => brightness_contrast()
  - HSV Adjust: combine3([h,s,v]) -> hsvadjust => hsv_adjust(h, s, v)
  - Identity removal: multiply by 1, add 0, power by 1, etc.

Usage: npx vite-node optimize-mtlx-node.js -- <usd-file> [options]

Arguments:
  <usd-file>              USD/USDA/USDC/USDZ file to load

Options:
  -m, --material <id>     Optimize only specific material by ID (default: all)
  -o, --output <file>     Write optimized node graph to file
  -l, --level <level>     Optimization level: none, basic, standard, aggressive
                          (default: standard)
  --analyze               Only analyze patterns, don't apply optimizations
  -v, --verbose           Enable verbose logging
  --show-before           Show node graph before optimization
  --no-after              Don't show node graph after optimization
  --compact               Compact JSON output
  -h, --help              Show this help message

Optimization Levels:
  none        - No optimization (pass-through)
  basic       - Remove identity operations only
  standard    - Pattern detection + identity removal (default)
  aggressive  - All optimizations + constant folding

Examples:
  # Analyze patterns in a USD file
  npx vite-node optimize-mtlx-node.js -- model.usda --analyze

  # Optimize with verbose output
  npx vite-node optimize-mtlx-node.js -- model.usda -v --show-before

  # Optimize specific material
  npx vite-node optimize-mtlx-node.js -- model.usda -m 1

  # Save optimized graph to file
  npx vite-node optimize-mtlx-node.js -- model.usda -o optimized.json
`);
}

function loadFile(filename) {
    try {
        const data = fs.readFileSync(filename);
        const mimeType = 'application/octet-stream';
        const blob = new Blob([data], { type: mimeType });
        return new File([blob], path.basename(filename), { type: blob.type });
    } catch (err) {
        console.error(`Error loading file: ${err.message}`);
        return null;
    }
}

function formatJson(obj, compact) {
    return compact ? JSON.stringify(obj) : JSON.stringify(obj, null, 2);
}

async function optimizeNodeGraphs(options) {
    if (options.verbose) {
        console.error(`Loading USD file: ${options.inputFile}`);
    }

    if (!fs.existsSync(options.inputFile)) {
        console.error(`Error: File not found: ${options.inputFile}`);
        process.exit(1);
    }

    const loader = new TinyUSDZLoader();
    await loader.init({ useMemory64: false });
    loader.setMaxMemoryLimitMB(500);

    if (options.verbose) {
        console.error('Loader initialized');
    }

    const data = fs.readFileSync(options.inputFile);
    const arrayBuffer = data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);

    const usd = await new Promise((resolve, reject) => {
        loader.parse(arrayBuffer, options.inputFile, resolve, reject);
    });

    if (!usd) {
        console.error('Error: Failed to load USD file');
        process.exit(1);
    }

    const numMaterials = usd.numMaterials();

    if (options.verbose) {
        console.error(`Found ${numMaterials} material(s)`);
    }

    if (numMaterials === 0) {
        console.error('Warning: No materials found in USD file');
        return;
    }

    const results = [];
    const materialIds = options.materialId !== null
        ? [options.materialId]
        : Array.from({ length: numMaterials }, (_, i) => i);

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

            let nodeGraph = null;
            if (materialData.openPBR?.nodeGraph) {
                nodeGraph = materialData.openPBR.nodeGraph;
            }

            if (!nodeGraph) {
                if (options.verbose) {
                    console.error(`  Material "${materialName}": No nodeGraph found`);
                }
                continue;
            }

            const originalNodeCount = nodeGraph.nodegraph?.nodes?.length || 0;

            if (options.verbose) {
                console.error(`  Material "${materialName}": Found nodeGraph with ${originalNodeCount} nodes`);
            }

            if (options.analyzeOnly) {
                const analysis = analyzeNodeGraph(nodeGraph);
                results.push({ materialId: matId, materialName, analysis });

                if (options.verbose) {
                    console.error(`  Patterns found:`);
                    console.error(`    - Invert: ${analysis.patterns.invert.length}`);
                    console.error(`    - Brightness/Contrast: ${analysis.patterns.brightnessContrast.length}`);
                    console.error(`    - HSV Adjust: ${analysis.patterns.hsvAdjust.length}`);
                    console.error(`    - Identity ops: ${analysis.identityOps.length}`);
                    console.error(`  Potential node reduction: ${analysis.potentialReduction.nodesRemovable}`);
                }
            } else {
                if (options.showBefore) {
                    console.error(`\n--- Before Optimization (Material: ${materialName}) ---`);
                    console.error(formatJson({
                        nodeCount: originalNodeCount,
                        nodes: nodeGraph.nodegraph?.nodes?.map(n => ({ name: n.name, category: n.category, type: n.type }))
                    }, options.compact));
                }

                const optimized = optimizeNodeGraph(nodeGraph, options.level);
                const optimizedNodeCount = optimized.nodegraph?.nodes?.length || 0;

                results.push({
                    materialId: matId,
                    materialName,
                    originalNodeCount,
                    optimizedNodeCount,
                    reduction: originalNodeCount - optimizedNodeCount,
                    optimizationInfo: optimized.optimizationInfo,
                    nodeGraph: options.showAfter ? optimized : undefined
                });

                if (options.verbose) {
                    console.error(`\n  Optimization Summary:`);
                    console.error(`    ${getOptimizationSummary(optimized).split('\n').join('\n    ')}`);
                }
            }
        } catch (err) {
            console.error(`Error processing material ${matId}: ${err.message}`);
            if (options.verbose) console.error(err.stack);
        }
    }

    const output = formatJson(results, options.compact);

    if (options.outputFile) {
        try {
            fs.writeFileSync(options.outputFile, output, 'utf8');
            if (options.verbose) console.error(`\nOutput written to: ${options.outputFile}`);
        } catch (err) {
            console.error(`Error writing output file: ${err.message}`);
            process.exit(1);
        }
    } else {
        console.log(output);
    }

    if (!options.analyzeOnly && results.length > 0) {
        const totalOriginal = results.reduce((sum, r) => sum + (r.originalNodeCount || 0), 0);
        const totalOptimized = results.reduce((sum, r) => sum + (r.optimizedNodeCount || 0), 0);
        console.error(`\nTotal: ${totalOriginal} -> ${totalOptimized} nodes (${totalOriginal - totalOptimized} removed)`);
    }

    if (options.verbose) console.error('\nDone!');
}

async function main() {
    try {
        const options = parseArgs();
        await optimizeNodeGraphs(options);
    } catch (err) {
        console.error(`Fatal error: ${err.message}`);
        if (process.env.DEBUG) console.error(err.stack);
        process.exit(1);
    }
}

main();
