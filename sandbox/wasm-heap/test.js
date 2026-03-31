const MemoryTestModule = require('./memory_test.js');

function formatBytes(bytes) {
    return (bytes / 1024 / 1024).toFixed(2) + ' MB';
}

function printMemoryUsage(label) {
    const usage = process.memoryUsage();
    console.log(`\n=== ${label} ===`);
    console.log(`RSS (Resident Set Size): ${formatBytes(usage.rss)}`);
    console.log(`Heap Total: ${formatBytes(usage.heapTotal)}`);
    console.log(`Heap Used: ${formatBytes(usage.heapUsed)}`);
    console.log(`External: ${formatBytes(usage.external)}`);
}

async function runTest() {
    console.log('Loading WASM module...');
    const Module = await MemoryTestModule();
    
    printMemoryUsage('Initial Memory Usage');
    
    console.log('\nCreating MemoryAllocator instance...');
    const allocator = new Module.MemoryAllocator();
    
    printMemoryUsage('After Creating Allocator');
    
    console.log('\nAllocating 100MB...');
    const chunks1 = allocator.allocate_100mb();
    console.log(`Chunks allocated: ${chunks1}`);
    console.log(`Total allocated by C++: ${formatBytes(allocator.get_total_allocated())}`);
    
    printMemoryUsage('After 100MB Allocation');
    
    console.log('\nAllocating 105MB...');
    const chunks2 = allocator.allocate_105mb();
    console.log(`Chunks allocated: ${chunks2}`);
    console.log(`Total allocated by C++: ${formatBytes(allocator.get_total_allocated())}`);
    
    printMemoryUsage('After 105MB Allocation (Total: ~205MB)');
    
    console.log('\nClearing all allocations...');
    allocator.clear_all();
    console.log(`Chunks remaining: ${allocator.get_chunk_count()}`);
    console.log(`Total allocated by C++: ${formatBytes(allocator.get_total_allocated())}`);
    
    printMemoryUsage('After Clearing Allocations');
    
    console.log('\nTest completed!');
}

runTest().catch(console.error);