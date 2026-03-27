import { TinyUSDZLoader } from '../src/tinyusdz/TinyUSDZLoader.js';
import fs from 'node:fs';

async function main() {
    const loader = new TinyUSDZLoader();
    await loader.init();

    const filePath = '../../models/synthetic-skin-16influences.usda';

    try {
        const data = fs.readFileSync(filePath);

        console.log(`Loading ${filePath} (${data.length} bytes)`);

        // Enable bone reduction in loader
        loader.setEnableBoneReduction(true);
        loader.setTargetBoneCount(4);

        // Load USD with bone reduction config
        await new Promise((resolve, reject) => {
            loader.parse(
                data,
                'synthetic-skin-16influences.usda',
                (usd) => {
                    console.log('\n=== USD loaded successfully ===');

                    // Get mesh info
                    const renderScene = usd.getRenderScene();
                    console.log('\nScene has', renderScene.meshes.length, 'meshes');
                    resolve(usd);
                },
                (error) => {
                    console.error('Failed to load USD:', error);
                    reject(error);
                },
                {
                    maxMemoryLimitMB: 2048
                }
            );
        }).then((usd) => {
            const renderScene = usd.getRenderScene();
            const meshes = renderScene.meshes;
            console.log('\nMeshes found:', meshes.length);

        for (const mesh of meshes) {
                console.log('\nMesh path:', mesh.abs_path);
                if (mesh.joint_and_weights) {
                    console.log('  Joint and weights elementSize:', mesh.joint_and_weights.elementSize);
                    console.log('  Weights per vertex:', mesh.joint_and_weights.elementSize);
                    console.log('  Total joint indices:', mesh.joint_and_weights.jointIndices?.length);
                    console.log('  Total joint weights:', mesh.joint_and_weights.jointWeights?.length);

                    const numVertices = mesh.joint_and_weights.jointIndices.length / mesh.joint_and_weights.elementSize;
                    console.log('  Number of vertices:', numVertices);
                }
            }
        });

    } catch (error) {
        console.error('Error:', error);
    }
}

main();