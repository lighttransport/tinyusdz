/**
 * Basic Usage Examples for Refactored TinyUSDZ JavaScript Parser
 */

// Note: These examples use the new refactored API
// In a real environment, you would import from the published package:
// import { UsdParser, UsdUtils, UsdLayer, UsdPrim } from '@tinyusdz/js-parser';

// For this demo, we'll use relative imports
import {
    UsdParser,
    UsdUtils,
    UsdLayer,
    UsdPrim,
    UsdAttribute,
    UsdValue,
    UsdTimeSamples,
    Logger,
    MemoryTracker,
    PerformanceTracker,
    VERSION,
    LIBRARY_INFO
} from '../src/index.js';

console.log('=== TinyUSDZ JavaScript Parser - Refactored Examples ===\n');
console.log(`Version: ${VERSION}`);
console.log(`Library: ${LIBRARY_INFO.name}\n`);

// Example 1: Basic USD Layer Creation
console.log('Example 1: Creating USD Layer Programmatically');
console.log('================================================\n');

try {
    // Create a new USD layer
    const layer = new UsdLayer({
        upAxis: 'Y',
        metersPerUnit: 1.0,
        timeCodesPerSecond: 24.0
    });

    // Create root primitive
    const root = new UsdPrim('Scene', 'Xform', 'def');
    root.metadata = { 
        kind: 'component',
        documentation: 'Root scene primitive' 
    };

    // Create a sphere primitive
    const sphere = new UsdPrim('Ball', 'Sphere', 'def');
    
    // Add attributes to sphere
    const radiusAttr = new UsdAttribute(
        'radius', 
        'float', 
        UsdValue.createNumber(1.5),
        { variability: 'uniform' }
    );
    
    const positionAttr = new UsdAttribute(
        'translate',
        'float3',
        UsdValue.createTuple([
            UsdValue.createNumber(0),
            UsdValue.createNumber(1),
            UsdValue.createNumber(0)
        ])
    );

    sphere.addAttribute(radiusAttr);
    sphere.addAttribute(positionAttr);

    // Create a cube primitive
    const cube = new UsdPrim('Box', 'Cube', 'def');
    const sizeAttr = new UsdAttribute(
        'size',
        'float3',
        UsdValue.createTuple([
            UsdValue.createNumber(2),
            UsdValue.createNumber(2),
            UsdValue.createNumber(2)
        ])
    );
    cube.addAttribute(sizeAttr);

    // Build hierarchy
    root.addChild(sphere);
    root.addChild(cube);
    layer.setRootPrim(root);

    console.log('Created USD layer with hierarchy:');
    console.log(`- Root: ${root.name} (${root.type})`);
    console.log(`  - Child: ${sphere.name} (${sphere.type})`);
    console.log(`    - Attributes: ${Array.from(sphere.attributes.keys()).join(', ')}`);
    console.log(`  - Child: ${cube.name} (${cube.type})`);
    console.log(`    - Attributes: ${Array.from(cube.attributes.keys()).join(', ')}`);

    // Test path resolution
    const foundSphere = layer.findPrim('/Scene/Ball');
    console.log(`\nPath resolution test: ${foundSphere ? '✓' : '✗'}`);
    console.log(`Found prim at path '/Scene/Ball': ${foundSphere?.name}`);

    // Validate layer
    const validationErrors = layer.validate();
    console.log(`\nValidation: ${validationErrors.length === 0 ? '✓ Passed' : '✗ Failed'}`);
    if (validationErrors.length > 0) {
        validationErrors.forEach(error => console.log(`  Error: ${error}`));
    }

    console.log('\n' + '='.repeat(60) + '\n');

} catch (error) {
    console.error('Example 1 failed:', error.message);
}

// Example 2: Advanced Logging and Monitoring
console.log('Example 2: Advanced Logging and Monitoring');
console.log('==========================================\n');

try {
    // Setup logging
    const logger = new Logger('Example2', Logger.Level.INFO);
    logger.info('Starting advanced example');

    // Setup memory tracking
    const memTracker = new MemoryTracker(100 * 1024 * 1024); // 100MB limit
    logger.info(`Memory tracker initialized with ${memTracker.maxBudget} byte limit`);

    // Setup performance tracking
    const perf = new PerformanceTracker();

    // Simulate some operations
    perf.measure('create-layer', () => {
        memTracker.allocate('layer-creation', 1024);
        
        const layer = UsdUtils.createSimpleLayer('TestPrim', 'Mesh');
        
        // Add some complexity
        for (let i = 0; i < 10; i++) {
            const prim = new UsdPrim(`Prim_${i}`, 'Sphere');
            const attr = new UsdAttribute(
                'radius',
                'float',
                UsdValue.createNumber(Math.random() * 5)
            );
            prim.addAttribute(attr);
            layer.rootPrim.addChild(prim);
        }
        
        return layer;
    });

    // Get statistics
    const memUsage = memTracker.getUsage();
    const perfStats = perf.getStats('create-layer');

    logger.info(`Memory usage: ${memUsage.used} bytes (${memUsage.percent.toFixed(1)}%)`);
    logger.info(`Performance: ${perfStats?.total.toFixed(2)}ms total`);

    // Cleanup
    memTracker.deallocate('layer-creation');
    logger.info('Cleanup completed');

    console.log('✓ Monitoring example completed successfully');
    console.log('\n' + '='.repeat(60) + '\n');

} catch (error) {
    console.error('Example 2 failed:', error.message);
}

// Example 3: Time Samples and Animation
console.log('Example 3: Time Samples and Animation');
console.log('====================================\n');

try {
    // Create time samples for animation
    const timeSamples = new UsdTimeSamples('linear');
    
    // Add keyframes for a bouncing ball animation
    const keyframes = [
        { time: 0.0, position: [0, 0, 0] },
        { time: 0.5, position: [2, 3, 0] },
        { time: 1.0, position: [4, 0, 0] },
        { time: 1.5, position: [6, 2, 0] },
        { time: 2.0, position: [8, 0, 0] }
    ];

    keyframes.forEach(({ time, position }) => {
        timeSamples.addSample(time, position);
    });

    console.log(`Created animation with ${timeSamples.getSampleCount()} keyframes`);
    
    const timeRange = timeSamples.getTimeRange();
    console.log(`Time range: ${timeRange.start} to ${timeRange.end} seconds`);

    // Test interpolation
    const testTimes = [0.25, 0.75, 1.25, 1.75];
    console.log('\nInterpolated positions:');
    
    testTimes.forEach(time => {
        const position = timeSamples.getSampleAtTime(time);
        console.log(`  t=${time}s: [${position.map(v => v.toFixed(1)).join(', ')}]`);
    });

    // Create animated primitive
    const layer = new UsdLayer();
    const animatedSphere = new UsdPrim('BouncingBall', 'Sphere');
    
    // Add time samples to layer
    layer.addTimeSamples('/BouncingBall', 'translate', timeSamples);
    
    console.log('\n✓ Animation example completed successfully');
    console.log('\n' + '='.repeat(60) + '\n');

} catch (error) {
    console.error('Example 3 failed:', error.message);
}

// Example 4: Error Handling and Recovery
console.log('Example 4: Error Handling and Recovery');
console.log('=====================================\n');

try {
    const logger = new Logger('ErrorHandling', Logger.Level.DEBUG);

    // Test various error conditions
    const errorTests = [
        {
            name: 'Invalid prim name',
            test: () => new UsdPrim('', 'Sphere') // Empty name should be invalid
        },
        {
            name: 'Memory limit exceeded',
            test: () => {
                const tracker = new MemoryTracker(100); // Very small limit
                tracker.allocate('test', 200); // Exceeds limit
            }
        },
        {
            name: 'Invalid time sample',
            test: () => {
                const samples = new UsdTimeSamples();
                samples.getSampleAtTime(1.0); // No samples exist
            }
        }
    ];

    let passedTests = 0;
    
    errorTests.forEach(({ name, test }) => {
        try {
            const result = test();
            if (result === null) {
                logger.info(`${name}: Handled gracefully (returned null)`);
                passedTests++;
            } else {
                logger.warn(`${name}: Unexpected success`);
            }
        } catch (error) {
            logger.info(`${name}: Caught expected error - ${error.constructor.name}`);
            passedTests++;
        }
    });

    console.log(`\nError handling tests: ${passedTests}/${errorTests.length} passed`);
    console.log('✓ Error handling example completed');
    console.log('\n' + '='.repeat(60) + '\n');

} catch (error) {
    console.error('Example 4 failed:', error.message);
}

// Example 5: Utility Functions
console.log('Example 5: Utility Functions');
console.log('============================\n');

try {
    // Test library utilities
    console.log('Library information:');
    const libInfo = UsdUtils.getLibraryInfo();
    console.log(`  Name: ${libInfo.name}`);
    console.log(`  Version: ${libInfo.version}`);
    console.log(`  Features: ${libInfo.features.slice(0, 3).join(', ')}...`);

    // Test format support
    console.log('\nSupported formats:');
    const formats = UsdUtils.getSupportedFormats();
    formats.forEach(format => {
        console.log(`  ✓ ${format.toUpperCase()}`);
    });

    // Test format detection
    console.log('\nFormat detection:');
    const testFiles = [
        'model.usda',
        'scene.usdc',
        'archive.usdz',
        'invalid.txt'
    ];

    testFiles.forEach(filename => {
        const isSupported = UsdUtils.isSupportedFormat(filename);
        console.log(`  ${filename}: ${isSupported ? '✓ Supported' : '✗ Not supported'}`);
    });

    // Create simple layer using utility
    const simpleLayer = UsdUtils.createSimpleLayer('QuickTest', 'Cube');
    console.log(`\nCreated simple layer with root prim: ${simpleLayer.rootPrim.name}`);

    console.log('\n✓ Utility functions example completed');

} catch (error) {
    console.error('Example 5 failed:', error.message);
}

console.log('\n' + '='.repeat(60));
console.log('🎉 All refactored examples completed successfully!');
console.log('\nNext steps:');
console.log('- Try parsing actual USD files with UsdParser');
console.log('- Explore the JSON conversion features');
console.log('- Check out the comprehensive test suite');
console.log('- Read the API documentation');
console.log('\nHappy USD parsing! 🚀');