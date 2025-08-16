/**
 * Test suite for time samples functionality
 */

const { UsdcTimeSamples, UsdcParser } = require('./usdc-parser.js');
const { UsdcValueReader } = require('./usdc-value-reader.js');
const { BinaryReader } = require('./binary-reader.js');

function testTimeSamplesBasic() {
    console.log('Testing basic time samples functionality...');
    
    const timeSamples = new UsdcTimeSamples();
    
    // Add some sample data
    timeSamples.addSample(0.0, 1.0);
    timeSamples.addSample(1.0, 2.0);
    timeSamples.addSample(2.0, 3.0);
    
    // Test exact time lookup
    console.log(`  Value at time 0.0: ${timeSamples.getSampleAtTime(0.0)} (expected: 1.0)`);
    console.log(`  Value at time 1.0: ${timeSamples.getSampleAtTime(1.0)} (expected: 2.0)`);
    
    // Test interpolation
    const interpolated = timeSamples.getSampleAtTime(0.5);
    console.log(`  Value at time 0.5: ${interpolated} (expected: ~1.5)`);
    
    if (Math.abs(interpolated - 1.5) < 0.001) {
        console.log('  ✓ Basic time samples working');
    } else {
        console.log('  ✗ Basic time samples failed');
    }
    
    console.log('');
}

function testTimeSamplesVectorInterpolation() {
    console.log('Testing vector time samples interpolation...');
    
    const timeSamples = new UsdcTimeSamples();
    
    // Add vector sample data
    timeSamples.addSample(0.0, [0.0, 0.0, 0.0]);
    timeSamples.addSample(1.0, [1.0, 2.0, 3.0]);
    
    // Test vector interpolation
    const interpolated = timeSamples.getSampleAtTime(0.5);
    console.log(`  Vector at time 0.5: [${interpolated.join(', ')}] (expected: [0.5, 1.0, 1.5])`);
    
    const expected = [0.5, 1.0, 1.5];
    let vectorMatch = true;
    if (Array.isArray(interpolated) && interpolated.length === 3) {
        for (let i = 0; i < 3; i++) {
            if (Math.abs(interpolated[i] - expected[i]) > 0.001) {
                vectorMatch = false;
                break;
            }
        }
    } else {
        vectorMatch = false;
    }
    
    if (vectorMatch) {
        console.log('  ✓ Vector interpolation working');
    } else {
        console.log('  ✗ Vector interpolation failed');
    }
    
    console.log('');
}

function testTimeSamplesEdgeCases() {
    console.log('Testing time samples edge cases...');
    
    const timeSamples = new UsdcTimeSamples();
    
    // Test with no samples
    let result = timeSamples.getSampleAtTime(0.0);
    if (result === null) {
        console.log('  ✓ Empty time samples handled correctly');
    } else {
        console.log('  ✗ Empty time samples should return null');
    }
    
    // Test with single sample
    timeSamples.addSample(1.0, 42);
    result = timeSamples.getSampleAtTime(0.5);
    if (result === 42) {
        console.log('  ✓ Single sample handled correctly');
    } else {
        console.log('  ✗ Single sample should return the only value');
    }
    
    // Test extrapolation (beyond available range)
    timeSamples.addSample(2.0, 84);
    result = timeSamples.getSampleAtTime(3.0); // Beyond range
    console.log(`  Extrapolation result: ${result} (should be nearest value)`);
    
    console.log('');
}

function testTimeSamplesValueReader() {
    console.log('Testing time samples value reader integration...');
    
    // Create mock parser and reader for testing
    const mockParser = {
        warning: (msg) => console.log(`    Warning: ${msg}`),
        toc: { sections: [] },
        layer: { tokens: [], strings: [] }
    };
    
    // Create test data for time samples (simplified format)
    const testData = new Uint8Array([
        // Number of samples (2 samples)
        0x02, 0x00, 0x00, 0x00,
        // Sample 1: time = 0.0
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // Sample 1: value rep (inlined float 1.0)
        0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x40,
        // Sample 2: time = 1.0  
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F,
        // Sample 2: value rep (inlined float 2.0)
        0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x40
    ]);
    
    const reader = new BinaryReader(testData);
    mockParser.reader = reader;
    
    const valueReader = new UsdcValueReader(mockParser);
    
    try {
        const result = valueReader.readTimeSamples();
        console.log(`  Time samples type: ${result.type}`);
        
        if (result.type === 'time_samples' && result.value && result.value.samples) {
            console.log(`  Number of samples: ${result.value.samples.length}`);
            console.log('  ✓ Time samples value reader working');
        } else {
            console.log('  ✗ Time samples value reader failed');
        }
    } catch (error) {
        console.log(`  ✗ Error in time samples reader: ${error.message}`);
    }
    
    console.log('');
}

function testTimeSamplesInUsdcParser() {
    console.log('Testing time samples in USDC parser context...');
    
    // Create minimal USDC file with time samples reference
    const headerSize = 24;
    const buffer = new ArrayBuffer(headerSize + 8);
    const view = new DataView(buffer);
    const uint8View = new Uint8Array(buffer);
    
    // Write basic header
    const magic = new TextEncoder().encode('PXR-USDC');
    uint8View.set(magic, 0);
    uint8View[8] = 0; uint8View[9] = 7; uint8View[10] = 1; // version
    view.setBigUint64(16, BigInt(headerSize), true); // TOC offset
    
    // Add empty TOC
    const tocView = new DataView(buffer, headerSize, 8);
    tocView.setBigUint64(0, 0n, true); // 0 sections
    
    const parser = new UsdcParser(new Uint8Array(buffer));
    
    try {
        const layer = parser.parse();
        if (layer && layer.timeSamples instanceof Map) {
            console.log('  ✓ USDC parser supports time samples structure');
        } else {
            console.log('  ✗ USDC parser missing time samples support');
        }
    } catch (error) {
        console.log(`  ✗ USDC parser error: ${error.message}`);
    }
    
    console.log('');
}

function testTimeSamplesPerformance() {
    console.log('Testing time samples performance...');
    
    const timeSamples = new UsdcTimeSamples();
    
    // Add many samples
    const numSamples = 1000;
    const startTime = Date.now();
    
    for (let i = 0; i < numSamples; i++) {
        timeSamples.addSample(i / 10.0, Math.sin(i / 10.0));
    }
    
    const addTime = Date.now() - startTime;
    
    // Test lookup performance
    const lookupStart = Date.now();
    for (let i = 0; i < 100; i++) {
        timeSamples.getSampleAtTime(Math.random() * 100);
    }
    const lookupTime = Date.now() - lookupStart;
    
    console.log(`  Added ${numSamples} samples in ${addTime}ms`);
    console.log(`  100 lookups took ${lookupTime}ms`);
    
    if (addTime < 100 && lookupTime < 50) {
        console.log('  ✓ Time samples performance acceptable');
    } else {
        console.log('  ⚠ Time samples performance could be improved');
    }
    
    console.log('');
}

// Run all tests
function runTests() {
    console.log('=== Time Samples Tests ===\n');
    
    try {
        testTimeSamplesBasic();
        testTimeSamplesVectorInterpolation();
        testTimeSamplesEdgeCases();
        testTimeSamplesValueReader();
        testTimeSamplesInUsdcParser();
        testTimeSamplesPerformance();
        
        console.log('All time samples tests completed!');
    } catch (error) {
        console.error('Time samples test suite failed with error:', error);
    }
}

// Run tests if this file is executed directly
if (require.main === module) {
    runTests();
}

module.exports = { runTests };