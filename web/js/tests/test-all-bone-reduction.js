import { TinyUSDZLoader } from './src/tinyusdz/TinyUSDZLoader.js';
import fs from 'node:fs';

async function testFile(filePath, expectedElementSize) {
    const loader = new TinyUSDZLoader();
    await loader.init();

    try {
        const data = fs.readFileSync(filePath);

        console.log(`\n=== Testing ${filePath} ===`);
        console.log(`File size: ${data.length} bytes`);
        console.log(`Expected elementSize: ${expectedElementSize}`);

        // Enable bone reduction in loader
        loader.setEnableBoneReduction(true);
        loader.setTargetBoneCount(4);

        // Load USD with bone reduction config
        await new Promise((resolve, reject) => {
            loader.parse(
                data,
                filePath.split('/').pop(),
                (usd) => {
                    console.log('✓ USD loaded successfully');

                    // Get render scene
                    const ok = usd.toRenderScene();
                    if (!ok) {
                        console.error('✗ Failed to convert to render scene');
                        reject(new Error('Failed to convert to render scene'));
                        return;
                    }

                    // Get the render scene data
                    const renderSceneJSON = usd.getRenderSceneAsJSON();
                    const renderScene = JSON.parse(renderSceneJSON);

                    console.log(`✓ Converted to render scene with ${renderScene.meshes.length} meshes`);

                    for (const mesh of renderScene.meshes) {
                        if (mesh.joint_and_weights) {
                            const elementSize = mesh.joint_and_weights.elementSize;
                            console.log(`  Mesh: ${mesh.abs_path}`);
                            console.log(`    Original elementSize: ${expectedElementSize}`);
                            console.log(`    After reduction: ${elementSize}`);

                            if (elementSize === 4) {
                                console.log(`    ✓ Bone reduction successful: ${expectedElementSize} → ${elementSize}`);
                            } else if (elementSize === expectedElementSize && expectedElementSize <= 4) {
                                console.log(`    ✓ No reduction needed (already ≤ 4)`);
                            } else {
                                console.log(`    ✗ Bone reduction failed: expected 4, got ${elementSize}`);
                            }

                            const numIndices = mesh.joint_and_weights.jointIndices?.length || 0;
                            const numWeights = mesh.joint_and_weights.jointWeights?.length || 0;
                            const numVertices = numIndices / elementSize;
                            console.log(`    Vertices: ${numVertices}`);
                            console.log(`    Total indices: ${numIndices}`);
                            console.log(`    Total weights: ${numWeights}`);
                        }
                    }

                    resolve();
                },
                (error) => {
                    console.error('✗ Failed to load USD:', error);
                    reject(error);
                },
                {
                    maxMemoryLimitMB: 2048
                }
            );
        });

    } catch (error) {
        console.error('✗ Error:', error);
    }
}

async function main() {
    const testFiles = [
        { path: '../../models/synthetic-skin-8influences.usda', elementSize: 8 },
        { path: '../../models/synthetic-skin-16influences.usda', elementSize: 16 },
        { path: '../../models/synthetic-skin-32influences.usda', elementSize: 32 }
    ];

    for (const test of testFiles) {
        await testFile(test.path, test.elementSize);
    }

    console.log('\n=== All tests completed ===');
}

main();