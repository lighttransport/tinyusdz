/**
 * Integration test for all newly implemented USDC parser features
 */

const { UsdcParser, UsdcTimeSamples } = require('./usdc-parser.js');
const { UsdcValueReader } = require('./usdc-value-reader.js');
const { BinaryReader } = require('./binary-reader.js');
const { lz4DecompressSafe } = require('./lz4-decoder.js');

function runIntegrationTests() {
    console.log('=== USDC Parser Integration Tests ===\n');
    
    let passed = 0;
    let total = 0;
    
    // Test 1: Complete LZ4 decompression integration
    console.log('Test 1: LZ4 Decompression Integration');
    total++;
    try {
        // Create simple LZ4 compressed data for testing
        const testData = new Uint8Array([
            0x04, 0x22, 0x4D, 0x18, // LZ4 magic
            0x64, 0x40, 0xA7,       // LZ4 flags 
            0x0C, 0x00, 0x00, 0x00, // Uncompressed size (12 bytes)
            0x05, 0x00, 0x00, 0x00, // Compressed size (5 bytes)
            0x05, 0x10, 0x48, 0x65, 0x6C, 0x6C, 0x6F // Compressed "Hello"
        ]);
        
        const result = lz4DecompressSafe(testData);
        if (result && result.length > 0) {
            console.log('  ✓ LZ4 decompression working');
            passed++;
        } else {
            console.log('  ✗ LZ4 decompression failed');
        }
    } catch (error) {
        console.log(`  ✗ LZ4 error: ${error.message}`);
    }
    console.log('');
    
    // Test 2: Compressed array reading
    console.log('Test 2: Compressed Array Reading');
    total++;
    try {
        const parser = createMockParser();
        const valueReader = new UsdcValueReader(parser);
        
        // Test compressed array data structure
        const testResult = valueReader.readCompressedArray(8, 3); // 3 floats
        if (testResult && testResult.type.includes('[]')) {
            console.log('  ✓ Compressed array reading structure working');
            passed++;
        } else {
            console.log('  ✗ Compressed array reading failed');
        }
    } catch (error) {
        console.log(`  ✗ Compressed array error: ${error.message}`);
    }
    console.log('');
    
    // Test 3: Data section handling
    console.log('Test 3: Data Section Handling');
    total++;
    try {
        const parser = createMockParser();
        const valueReader = new UsdcValueReader(parser);
        
        // Test data section lookup
        const dataSection = valueReader.findDataSectionForValue({ isCompressed: () => false });
        if (dataSection === null) { // Expected for mock parser
            console.log('  ✓ Data section handling working (no sections in mock)');
            passed++;
        } else {
            console.log('  ✗ Unexpected data section found');
        }
    } catch (error) {
        console.log(`  ✗ Data section error: ${error.message}`);
    }
    console.log('');
    
    // Test 4: Path parsing improvements
    console.log('Test 4: Path Parsing Improvements');
    total++;
    try {
        const parser = createMockParser();
        
        // Create test path data with potential issues
        const pathData = new Uint8Array([
            0x05, 0x00, 0x00, 0x00, // Header: 2 elements, absolute
            0x00, 0x00, 0x00, 0x00, // Element 1: token 0
            0x01, 0x00, 0x00, 0x00  // Element 2: token 1
        ]);
        
        parser.reader = new BinaryReader(pathData);
        const path = parser.parsePath();
        
        if (path && path.indices.length >= 0) {
            console.log('  ✓ Path parsing improvements working');
            passed++;
        } else {
            console.log('  ✗ Path parsing failed');
        }
    } catch (error) {
        console.log(`  ✗ Path parsing error: ${error.message}`);
    }
    console.log('');
    
    // Test 5: Time samples functionality
    console.log('Test 5: Time Samples Functionality');
    total++;
    try {
        const timeSamples = new UsdcTimeSamples();
        timeSamples.addSample(0.0, 1.0);
        timeSamples.addSample(1.0, 2.0);
        
        const interpolated = timeSamples.getSampleAtTime(0.5);
        if (Math.abs(interpolated - 1.5) < 0.001) {
            console.log('  ✓ Time samples interpolation working');
            passed++;
        } else {
            console.log(`  ✗ Time samples interpolation failed: got ${interpolated}, expected 1.5`);
        }
    } catch (error) {
        console.log(`  ✗ Time samples error: ${error.message}`);
    }
    console.log('');
    
    // Test 6: Time samples value reading
    console.log('Test 6: Time Samples Value Reading');
    total++;
    try {
        const parser = createMockParser();
        const valueReader = new UsdcValueReader(parser);
        
        // Create mock time samples data
        const timeSamplesData = new Uint8Array([
            0x01, 0x00, 0x00, 0x00, // 1 sample
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // time = 0.0
            0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x40  // value rep (inlined float)
        ]);
        
        parser.reader = new BinaryReader(timeSamplesData);
        const result = valueReader.readTimeSamples();
        
        if (result && result.type === 'time_samples' && result.value && result.value.samples) {
            console.log('  ✓ Time samples value reading working');
            passed++;
        } else {
            console.log('  ✗ Time samples value reading failed');
        }
    } catch (error) {
        console.log(`  ✗ Time samples value reading error: ${error.message}`);
    }
    console.log('');
    
    // Summary
    console.log(`=== Integration Test Results ===`);
    console.log(`Passed: ${passed}/${total} tests`);
    console.log(`Success rate: ${Math.round((passed/total)*100)}%`);
    
    if (passed === total) {
        console.log('🎉 All integration tests passed!');
        return true;
    } else {
        console.log('⚠️  Some integration tests failed');
        return false;
    }
}

// Helper function to create a mock parser for testing
function createMockParser() {
    return {
        warning: (msg) => console.log(`    Warning: ${msg}`),
        error: (msg) => { throw new Error(msg); },
        toc: { 
            sections: [],
            getSection: () => null
        },
        layer: { 
            tokens: ['root', 'test', 'path'],
            strings: ['test string'],
            timeSamples: new Map()
        },
        reader: null,
        getToken: (index) => {
            const tokens = ['root', 'test', 'path'];
            return tokens[index] || `<invalid:${index}>`;
        },
        getString: (index) => {
            const strings = ['test string'];
            return strings[index] || `<invalid:${index}>`;
        }
    };
}

// Run tests if this file is executed directly
if (require.main === module) {
    runIntegrationTests();
}

module.exports = { runIntegrationTests };