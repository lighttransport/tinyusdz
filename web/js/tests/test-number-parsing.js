import { TinyUSDZLoader } from '../src/tinyusdz/TinyUSDZLoader.js';
import fs from 'node:fs';

async function main() {
    const loader = new TinyUSDZLoader();
    await loader.init();

    const filePath = '../../models/test-large-numbers.usda';

    try {
        const data = fs.readFileSync(filePath);

        console.log(`Loading ${filePath} (${data.length} bytes)`);

        // Load USD
        await new Promise((resolve, reject) => {
            loader.parse(
                data,
                'test-large-numbers.usda',
                (usd) => {
                    console.log('✓ USD loaded successfully');
                    console.log('✓ Large number parsing test passed');
                    resolve();
                },
                (error) => {
                    console.error('✗ Failed to load USD:', error);
                    reject(error);
                }
            );
        });

    } catch (error) {
        console.error('✗ Error:', error);
    }
}

main();