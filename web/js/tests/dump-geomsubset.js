#!/usr/bin/env node
// dump-geomsubset.js
// Standalone Node.js CLI tool for debugging geometry/material grouping in USD files
// This tool helps debug issues where too many mesh groups are created for Three.js rendering
//
// Usage: node dump-geomsubset.js <input.usdz|usda|usdc>

import { TinyUSDZLoader } from '../src/tinyusdz/TinyUSDZLoader.js';
import { createRequire } from 'module';
const require = createRequire(import.meta.url);
const fs = require('fs');
const path = require('path');

// Parse command line arguments
const args = process.argv.slice(2);
if (args.length < 1) {
    console.log('Usage: node dump-geomsubset.js <input.usdz|usda|usdc>');
    console.log('');
    console.log('This tool dumps geometry subset and material grouping information');
    console.log('from USD files to help debug submesh/material issues in Three.js rendering.');
    console.log('');
    console.log('Example:');
    console.log('  node dump-geomsubset.js ./assets/geomsubset-mtlx.usdc');
    process.exit(1);
}

const inputFile = args[0];

// Check if file exists
if (!fs.existsSync(inputFile)) {
    console.error(`Error: File not found: ${inputFile}`);
    process.exit(1);
}

async function main() {
    console.log('='.repeat(80));
    console.log('TinyUSDZ Geometry Subset / Material Grouping Debug Tool');
    console.log('='.repeat(80));
    console.log(`Input file: ${inputFile}`);
    console.log('');

    // Initialize TinyUSDZ loader
    console.log('Initializing TinyUSDZ WASM...');
    const loader = new TinyUSDZLoader(null, { maxMemoryLimitMB: 512 });
    await loader.init({ useMemory64: false });
    console.log('TinyUSDZ initialized.');
    console.log('');

    // Read and load the USD file
    console.log(`Loading USD file: ${inputFile}`);
    const fileData = fs.readFileSync(inputFile);
    const usdBinary = new Uint8Array(fileData);

    const nativeLoader = new loader.native_.TinyUSDZLoaderNative();
    const filename = path.basename(inputFile);
    const success = nativeLoader.loadFromBinary(usdBinary, filename);

    if (!success) {
        console.error('Failed to parse USD file:', nativeLoader.error());
        process.exit(1);
    }

    // Get scene metadata
    const sceneMetadata = nativeLoader.getSceneMetadata ? nativeLoader.getSceneMetadata() : {};
    console.log('');
    console.log('-'.repeat(80));
    console.log('SCENE METADATA');
    console.log('-'.repeat(80));
    console.log(`  upAxis: ${sceneMetadata.upAxis || 'Y'}`);
    console.log(`  metersPerUnit: ${sceneMetadata.metersPerUnit || 1.0}`);
    console.log(`  timeCodesPerSecond: ${sceneMetadata.timeCodesPerSecond || 24.0}`);

    // Get mesh and material counts
    const numMeshes = nativeLoader.numMeshes();
    const numMaterials = nativeLoader.numMaterials();

    console.log('');
    console.log('-'.repeat(80));
    console.log('SUMMARY');
    console.log('-'.repeat(80));
    console.log(`  Total meshes: ${numMeshes}`);
    console.log(`  Total materials: ${numMaterials}`);

    // Dump material information
    console.log('');
    console.log('-'.repeat(80));
    console.log('MATERIALS');
    console.log('-'.repeat(80));

    const materialInfos = [];
    for (let i = 0; i < numMaterials; i++) {
        try {
            const result = nativeLoader.getMaterialWithFormat(i, 'json');
            if (!result.error) {
                const matData = JSON.parse(result.data);
                materialInfos.push(matData);

                const typeInfo = getMaterialType(matData);
                console.log(`  Material ${i}: name="${matData.name || 'unnamed'}"`);
                console.log(`    hasOpenPBR: ${typeInfo.hasOpenPBR}, hasUsdPreviewSurface: ${typeInfo.hasUsdPreviewSurface}`);
                if (matData.diffuseColor) {
                    console.log(`    diffuseColor: [${matData.diffuseColor.join(', ')}]`);
                }
                if (matData.openPBRShader && matData.openPBRShader.base_color) {
                    console.log(`    OpenPBR base_color: [${matData.openPBRShader.base_color.join(', ')}]`);
                }
            } else {
                console.log(`  Material ${i}: ERROR - ${result.error}`);
                materialInfos.push(null);
            }
        } catch (e) {
            console.log(`  Material ${i}: ERROR - ${e.message}`);
            materialInfos.push(null);
        }
    }

    // Dump mesh information with submeshes
    console.log('');
    console.log('-'.repeat(80));
    console.log('MESHES AND SUBMESHES');
    console.log('-'.repeat(80));

    let totalSubmeshCount = 0;
    let totalExpectedGroups = 0;

    for (let meshIdx = 0; meshIdx < numMeshes; meshIdx++) {
        const meshData = nativeLoader.getMesh(meshIdx);
        if (!meshData) {
            console.log(`  Mesh ${meshIdx}: ERROR - failed to get mesh data`);
            continue;
        }

        console.log('');
        console.log(`  Mesh ${meshIdx}: name="${meshData.name || 'unnamed'}"`);
        console.log(`    vertices: ${meshData.points ? meshData.points.length / 3 : 0}`);
        console.log(`    indices: ${meshData.faceVertexIndices ? meshData.faceVertexIndices.length : 0}`);
        console.log(`    triangles: ${meshData.faceVertexIndices ? meshData.faceVertexIndices.length / 3 : 0}`);
        console.log(`    materialId: ${meshData.materialId !== undefined ? meshData.materialId : 'none'}`);
        console.log(`    doubleSided: ${meshData.doubleSided || false}`);

        // Check for submeshes (pre-computed in WASM)
        if (meshData.submeshes && meshData.submeshes.length > 0) {
            const submeshes = meshData.submeshes;
            console.log(`    submeshes: ${submeshes.length} pre-computed groups`);
            totalSubmeshCount += submeshes.length;

            // Analyze submeshes
            const materialIdToGroups = new Map();
            let totalTriangles = 0;

            for (let i = 0; i < submeshes.length; i++) {
                const submesh = submeshes[i];
                const triangleCount = submesh.count / 3;
                totalTriangles += triangleCount;

                // Group by material ID
                if (!materialIdToGroups.has(submesh.materialId)) {
                    materialIdToGroups.set(submesh.materialId, []);
                }
                materialIdToGroups.get(submesh.materialId).push({
                    groupIndex: i,
                    start: submesh.start,
                    count: submesh.count,
                    triangles: triangleCount
                });

                const matName = submesh.materialId >= 0 && submesh.materialId < materialInfos.length && materialInfos[submesh.materialId]
                    ? materialInfos[submesh.materialId].name || 'unnamed'
                    : 'none';

                console.log(`      [${i}] start=${submesh.start}, count=${submesh.count} (${triangleCount} tris), materialId=${submesh.materialId} ("${matName}")`);
            }

            // Analyze material grouping
            const uniqueMaterials = materialIdToGroups.size;
            totalExpectedGroups += uniqueMaterials;

            console.log('');
            console.log(`    Material grouping analysis:`);
            console.log(`      Unique materials in this mesh: ${uniqueMaterials}`);
            console.log(`      Draw groups created: ${submeshes.length}`);

            if (submeshes.length > uniqueMaterials) {
                console.log(`      WARNING: More draw groups than unique materials!`);
                console.log(`      This may cause unnecessary draw calls in Three.js.`);
                console.log('');
                console.log('      Groups per material:');
                for (const [matId, groups] of materialIdToGroups.entries()) {
                    if (groups.length > 1) {
                        const matName = matId >= 0 && matId < materialInfos.length && materialInfos[matId]
                            ? materialInfos[matId].name || 'unnamed'
                            : 'none';
                        console.log(`        Material ${matId} ("${matName}"): ${groups.length} groups (could be merged into 1)`);
                        groups.forEach(g => {
                            console.log(`          - group ${g.groupIndex}: start=${g.start}, ${g.triangles} triangles`);
                        });
                    }
                }
            } else {
                console.log(`      OK: Each material has exactly one draw group.`);
            }

            console.log(`      Total triangles in submeshes: ${totalTriangles}`);
        } else {
            // Single material mesh
            console.log(`    submeshes: none (single material mesh)`);
            if (meshData.materialId !== undefined && meshData.materialId >= 0) {
                totalExpectedGroups += 1;
            }
        }
    }

    // Final summary
    console.log('');
    console.log('-'.repeat(80));
    console.log('ANALYSIS SUMMARY');
    console.log('-'.repeat(80));
    console.log(`  Total meshes: ${numMeshes}`);
    console.log(`  Total materials defined: ${numMaterials}`);
    console.log(`  Total submesh groups: ${totalSubmeshCount}`);
    console.log(`  Expected minimum groups (by unique material): ${totalExpectedGroups}`);

    if (totalSubmeshCount > totalExpectedGroups) {
        console.log('');
        console.log(`  OPTIMIZATION OPPORTUNITY:`);
        console.log(`    ${totalSubmeshCount - totalExpectedGroups} groups could potentially be merged.`);
        console.log(`    This would reduce draw calls in Three.js.`);
    } else if (totalSubmeshCount === totalExpectedGroups || totalSubmeshCount === 0) {
        console.log('');
        console.log(`  OK: Submesh grouping appears optimal.`);
    }

    console.log('');
    console.log('='.repeat(80));
    console.log('Done.');
}

// Helper function to detect material type (same as TinyUSDZLoaderUtils)
function getMaterialType(materialData) {
    if (!materialData) {
        return {
            hasOpenPBR: false,
            hasUsdPreviewSurface: false,
            hasBoth: false,
            hasNone: true,
            recommended: 'none'
        };
    }

    const hasOpenPBR = !!materialData.hasOpenPBR;
    const hasUsdPreviewSurface = !!materialData.hasUsdPreviewSurface;
    const hasBoth = hasOpenPBR && hasUsdPreviewSurface;
    const hasNone = !hasOpenPBR && !hasUsdPreviewSurface;

    let recommended = 'none';
    if (hasOpenPBR) {
        recommended = 'openpbr';
    } else if (hasUsdPreviewSurface) {
        recommended = 'usdpreviewsurface';
    }

    return {
        hasOpenPBR,
        hasUsdPreviewSurface,
        hasBoth,
        hasNone,
        recommended
    };
}

// Run main
main().catch(err => {
    console.error('Error:', err);
    process.exit(1);
});
