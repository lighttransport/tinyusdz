function formatBytes(bytes) {
    return (bytes / 1024 / 1024).toFixed(2) + ' MB';
}

function printMemoryUsage(label) {
    const usage = process.memoryUsage();
    console.log(`\n=== ${label} ===`);
    console.log(`RSS: ${formatBytes(usage.rss)}`);
    console.log(`Heap Total: ${formatBytes(usage.heapTotal)}`);
    console.log(`Heap Used: ${formatBytes(usage.heapUsed)}`);
    console.log(`External: ${formatBytes(usage.external)}`);
}

function printAllocatorStatus(allocator, label) {
    console.log(`\n--- ${label} ---`);
    console.log(`Chunks: ${allocator.get_chunk_count()}`);
    console.log(`Total allocated: ${formatBytes(allocator.get_total_allocated())}`);
}

async function testMallocImplementation(moduleName, jsFile) {
    console.log(`\n${'='.repeat(60)}`);
    console.log(`TESTING ${moduleName.toUpperCase()}`);
    console.log(`${'='.repeat(60)}`);
    
    const Module = await require(jsFile)();
    const allocator = new Module.MemoryAllocator();
    
    printMemoryUsage(`${moduleName} - Initial`);
    
    // Test sequence: 100MB → 20MB → free 100MB → 105MB
    console.log('\n1. Allocate 100MB');
    allocator.allocate_100mb();
    printAllocatorStatus(allocator, `${moduleName} - After 100MB`);
    const usage1 = process.memoryUsage();
    
    console.log('\n2. Allocate 20MB');
    allocator.allocate_20mb();
    printAllocatorStatus(allocator, `${moduleName} - After 20MB (120MB total)`);
    const usage2 = process.memoryUsage();
    
    console.log('\n3. Free 100MB chunk');
    const released = allocator.release_chunk(0);
    console.log(`Release successful: ${released}`);
    printAllocatorStatus(allocator, `${moduleName} - After freeing 100MB`);
    const usage3 = process.memoryUsage();
    
    console.log('\n4. Allocate 105MB');
    allocator.allocate_105mb();
    printAllocatorStatus(allocator, `${moduleName} - After 105MB (125MB total)`);
    const usage4 = process.memoryUsage();
    
    printMemoryUsage(`${moduleName} - Final`);
    
    // Calculate memory growth for analysis
    const growth1 = usage1.rss - 50.5 * 1024 * 1024; // Subtract baseline
    const growth4 = usage4.rss - 50.5 * 1024 * 1024;
    const reuse_efficiency = (growth1 + 25*1024*1024 - growth4) / (105*1024*1024) * 100; // How much of 105MB was reused
    
    console.log(`\n--- ${moduleName} SUMMARY ---`);
    console.log(`Peak RSS (step 2): ${formatBytes(usage2.rss)}`);
    console.log(`Final RSS (step 4): ${formatBytes(usage4.rss)}`);
    console.log(`Memory reuse efficiency: ${reuse_efficiency.toFixed(1)}%`);
    
    return {
        name: moduleName,
        peakRSS: usage2.rss,
        finalRSS: usage4.rss,
        reuseEfficiency: reuse_efficiency
    };
}

async function runMallocComparison() {
    console.log('MALLOC IMPLEMENTATION COMPARISON');
    console.log('Test sequence: 100MB → 20MB → free 100MB → 105MB');
    
    const results = [];
    
    try {
        results.push(await testMallocImplementation('dlmalloc', './memory_test_dlmalloc.js'));
    } catch (e) {
        console.log('dlmalloc test failed:', e.message);
    }
    
    try {
        results.push(await testMallocImplementation('emmalloc', './memory_test_emmalloc.js'));
    } catch (e) {
        console.log('emmalloc test failed:', e.message);
    }
    
    try {
        results.push(await testMallocImplementation('mimalloc', './memory_test_mimalloc.js'));
    } catch (e) {
        console.log('mimalloc test failed:', e.message);
    }
    
    // Final comparison
    console.log(`\n${'='.repeat(60)}`);
    console.log('FINAL COMPARISON');
    console.log(`${'='.repeat(60)}`);
    
    console.log('Malloc\t\tPeak RSS\tFinal RSS\tReuse Eff.');
    console.log('-'.repeat(50));
    
    results.forEach(result => {
        console.log(`${result.name}\t\t${formatBytes(result.peakRSS)}\t\t${formatBytes(result.finalRSS)}\t\t${result.reuseEfficiency.toFixed(1)}%`);
    });
    
    // Find best performer
    const bestReuse = results.reduce((best, current) => 
        current.reuseEfficiency > best.reuseEfficiency ? current : best
    );
    
    console.log(`\nBest for memory reuse: ${bestReuse.name} (${bestReuse.reuseEfficiency.toFixed(1)}% efficiency)`);
}

runMallocComparison().catch(console.error);