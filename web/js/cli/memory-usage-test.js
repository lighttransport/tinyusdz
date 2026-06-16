// Memory usage test for TinyUSDZ WebAssembly module
// Tests the estimate_memory_usage() function exposed from web/binding.cc
// Usage: bun run cli memory-usage-test.js or npm run cli memory-usage-test.js

import { TinyUSDZLoader } from '../src/tinyusdz/TinyUSDZLoader.js';

async function testMemoryUsage() {
    console.log('TinyUSDZ Memory Usage Test');
    console.log('==========================\n');
    
    try {
        // Initialize the WASM module
        console.log('Initializing TinyUSDZ WASM module...');
        const loader = new TinyUSDZLoader();
        await loader.init({useMemory64: false});
        console.log('WASM module initialized successfully\n');
        
        // Create a TinyUSDZLoaderNative instance to access test functions
        const usd = new loader.native_.TinyUSDZLoaderNative();
        
        // Check if the memory usage test function is available
        if (typeof usd.testValueMemoryUsage === 'function') {
            console.log('Running memory usage tests with different array sizes...\n');
            
            // Test with different array sizes
            const defaultArraySize = 10000;
            const arraySizes = [100, 10000, 100000, 10000000];
            
            for (const arraySize of arraySizes) {
                console.log(`\n${'='.repeat(60)}`);
                console.log(`Testing with array size: ${arraySize.toLocaleString()}`);
                console.log('='.repeat(60));
                
                try {
                    // Call the C++ testValueMemoryUsage function with specific array size
                    const testResults = usd.testValueMemoryUsage(arraySize);
                    
                    if (testResults && testResults.tests) {
                        // Show only array-related tests for brevity
                        const arrayTests = testResults.tests.filter(test => 
                            test.name.includes('array'));
                        
                        console.log('\nArray-related memory usage:');
                        arrayTests.forEach(test => {
                            console.log(`  ${test.name}: ${formatBytes(test.bytes)}`);
                        });
                        
                        console.log(`\nTotal memory for all ${testResults.totalTests} tests: ${formatBytes(testResults.totalMemory)}`);
                    } else {
                        console.log('Test completed - check console for detailed output');
                    }
                } catch (error) {
                    console.error(`Error testing with array size ${arraySize}:`, error.message);
                }
            }
            
            // Also run one test with default size to show all test types
            console.log(`\n${'='.repeat(60)}`);
            console.log('Full test with default array size (showing all value types)');
            console.log('='.repeat(60));
            
            const defaultResults = usd.testValueMemoryUsage(defaultArraySize);
            if (defaultResults && defaultResults.tests) {
                defaultResults.tests.forEach((test, index) => {
                    console.log(`${index + 1}. ${test.name}: ${formatBytes(test.bytes)}`);
                });
                console.log(`\nTotal tests: ${defaultResults.totalTests}`);
                console.log(`Total memory: ${formatBytes(defaultResults.totalMemory)}`);
            }
            
        } else {
            console.log('Memory usage test function not found');
            console.log('Available methods on TinyUSDZLoaderNative:', Object.getOwnPropertyNames(Object.getPrototypeOf(usd)).filter(name => name !== 'constructor'));
        }
        
        // Additional manual tests using the Value API if available
        console.log('\n=== Manual Value Creation Tests ===');
        
        // Test basic memory reporting for the process
        if (typeof process !== 'undefined' && process.memoryUsage) {
            const memUsage = process.memoryUsage();
            console.log('\nNode.js Memory Usage:');
            console.log(`RSS: ${(memUsage.rss / 1024 / 1024).toFixed(2)} MB`);
            console.log(`Heap Used: ${(memUsage.heapUsed / 1024 / 1024).toFixed(2)} MB`);
            console.log(`Heap Total: ${(memUsage.heapTotal / 1024 / 1024).toFixed(2)} MB`);
            console.log(`External: ${(memUsage.external / 1024 / 1024).toFixed(2)} MB`);
        }
        
    } catch (error) {
        console.error('Error during memory usage test:', error);
        process.exit(1);
    }
}

// Helper function to format bytes
function formatBytes(bytes) {
    if (bytes === 0) return '0 Bytes';
    
    const k = 1024;
    const sizes = ['Bytes', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
}


// Run the test
//uif (import.meta.url === `file://${process.argv[1]}`) {
    testMemoryUsage();
//}

export { testMemoryUsage, formatBytes };
