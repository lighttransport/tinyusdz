import { TinyUSDZLoader } from '../src/tinyusdz/TinyUSDZLoader.js';
import fs from 'node:fs';

async function testFile(filePath, shouldFail = false) {
    const loader = new TinyUSDZLoader();
    await loader.init();

    try {
        const data = fs.readFileSync(filePath);

        console.log(`\nTesting ${filePath} (${data.length} bytes)`);
        console.log(`Expected to ${shouldFail ? 'FAIL' : 'SUCCEED'}`);

        // Load USD
        await new Promise((resolve, reject) => {
            loader.parse(
                data,
                filePath.split('/').pop(),
                (usd) => {
                    if (shouldFail) {
                        console.log('✗ Unexpected: File loaded successfully (should have failed)');
                    } else {
                        console.log('✓ File loaded successfully');
                    }
                    resolve();
                },
                (error) => {
                    if (shouldFail) {
                        console.log('✓ Expected failure:', error.message || error);
                    } else {
                        console.error('✗ Unexpected failure:', error);
                    }
                    resolve(); // Don't reject so we can continue testing
                }
            );
        });

    } catch (error) {
        console.error('✗ Test error:', error);
    }
}

async function main() {
    console.log('=== Testing Digit Length Guards ===');

    // Test normal file with valid numbers
    await testFile('../../models/test-large-numbers.usda', false);

    // Test file with excessive digits (should fail)
    await testFile('../../models/test-excessive-digits.usda', true);

    // Test files with bone reduction (should still work)
    await testFile('../../models/synthetic-skin-16influences.usda', false);

    console.log('\n=== All tests completed ===');
}

main();