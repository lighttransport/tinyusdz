import { TinyUSDZLoader } from '../src/tinyusdz/TinyUSDZLoader.js';
import fs from 'node:fs';

async function test(file, expected) {
    const loader = new TinyUSDZLoader();
    await loader.init();
    const data = fs.readFileSync(file);
    return new Promise((resolve) => {
        loader.parse(data, file,
            () => resolve('SUCCESS'),
            () => resolve('FAIL')
        );
    });
}

(async () => {
    console.log('Testing boundary values (should succeed):',
        await test('../../models/test-digit-boundaries.usda'));
    console.log('Testing over-boundary values (should fail):',
        await test('../../models/test-digit-over-boundaries.usda'));
})();