/**
 * Test for C++20 coroutine-based async USD loading
 *
 * This tests the new loadFromBinaryAsync method that uses C++20 coroutines
 * to yield to the JavaScript event loop between processing phases.
 *
 * Run with: node test-coroutine-async.mjs
 */

import { TinyUSDZLoader } from './src/tinyusdz/TinyUSDZLoader.js';
import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

async function testCoroutineAsync() {
    console.log('=== C++20 Coroutine Async Loading Test ===\n');

    const loader = new TinyUSDZLoader();
    await loader.init();

    // Test file - use a binary USDC or USDZ file for proper testing
    // USDA files are text and need different handling
    let testFile = join(__dirname, 'assets', 'suzanne.usdc');

    let binary;
    try {
        // Try USDC first
        binary = new Uint8Array(readFileSync(testFile));
        console.log(`Loaded test file: ${testFile} (${binary.length} bytes)\n`);
    } catch (e) {
        // Fallback to USDA (read as string, convert to Uint8Array)
        try {
            testFile = join(__dirname, 'assets', 'cube-xform.usda');
            const text = readFileSync(testFile, 'utf-8');
            const encoder = new TextEncoder();
            binary = encoder.encode(text);
            console.log(`Loaded test file (text): ${testFile} (${binary.length} bytes)\n`);
        } catch (e2) {
            console.error(`Failed to read test file: ${testFile}`);
            console.error('Please ensure the test file exists.');
            process.exit(1);
        }
    }

    // Track phases
    const phases = [];
    const startTime = performance.now();

    console.log('Starting async parse with coroutine yields...\n');

    try {
        // Use the new coroutine-based async parser
        const usd = await loader.parseAsync(binary, testFile, {
            onPhaseStart: (info) => {
                const elapsed = (performance.now() - startTime).toFixed(1);
                phases.push({ ...info, elapsed });
                console.log(`[${elapsed}ms] Phase: ${info.phase} (${(info.progress * 100).toFixed(0)}%)`);
            }
        });

        const totalTime = (performance.now() - startTime).toFixed(1);

        console.log('\n=== Results ===');
        console.log(`Total time: ${totalTime}ms`);
        console.log(`Phases observed: ${phases.length}`);
        console.log(`Meshes: ${usd.numMeshes()}`);
        console.log(`Materials: ${usd.numMaterials()}`);
        console.log(`Textures: ${usd.numTextures()}`);

        console.log('\n=== Phase Timeline ===');
        phases.forEach((p, i) => {
            console.log(`  ${i + 1}. ${p.phase} at ${p.elapsed}ms (${(p.progress * 100).toFixed(0)}%)`);
        });

        console.log('\n[PASS] Coroutine async loading completed successfully!');

    } catch (error) {
        console.error('\n[FAIL] Error during async loading:', error);
        process.exit(1);
    }
}

// Run test
testCoroutineAsync().catch(console.error);
