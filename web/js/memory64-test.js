import wabt from 'wabt';

// Create a WebAssembly module with >4GB memory
async function createLargeMemoryModule() {
    // WAT (WebAssembly Text) code with 64-bit memory
    const wat = `
    (module
        ;; Declare 64-bit memory with 100,000 pages (6.4GB)
        (memory i64 100000 100000)
        (export "memory" (memory 0))
        
        ;; Function to write at 64-bit address
        (func $write (param $addr i64) (param $value i32)
            (i32.store (local.get $addr) (local.get $value))
        )
        (export "write" (func $write))
        
        ;; Function to read from 64-bit address
        (func $read (param $addr i64) (result i32)
            (i32.load (local.get $addr))
        )
        (export "read" (func $read))
    )`;
    
    // You'd need to compile this WAT to WASM
    // In production, use wat2wasm or wabt
    const wabtModule = await wabt();
    const wasm = wabtModule.parseWat('memory64.wat', wat, {
        enable_memory64: true
    });
    const { buffer } = wasm.toBinary({});
    
    // Instantiate the module
    const module = await WebAssembly.compile(buffer);
    const instance = await WebAssembly.instantiate(module);
    
    return instance.exports;
}

// Use the module
async function testLargeMemory() {
    const { memory, write, read } = await createLargeMemoryModule();
    
    // Access memory beyond 4GB boundary
    const largeAddress = 0x100000000n; // 4GB boundary (use BigInt)
    
    write(largeAddress, 42);
    console.log('Value at 4GB boundary:', read(largeAddress)); // 42
    
    // Direct memory access
    const buffer = new Uint8Array(memory.buffer);
    console.log('Memory size:', buffer.length, 'bytes');
}

testLargeMemory().catch(console.error);
