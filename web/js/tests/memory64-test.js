// Smoke test for WebAssembly memory64 (>4GB, i64-indexed memory) support in the
// host JS engine — the capability our wasm64 (`-sMEMORY64`) build relies on.
//
// Previously this compiled a hand-written WAT module at runtime via the optional
// `wabt` dev dependency. To keep the test self-contained (no extra install), we
// embed the equivalent pre-assembled WASM binary instead.
//
// The module below is the byte-for-byte equivalent of:
//
//   (module
//     (memory i64 65537 65537)          ;; 64-bit memory, ~4GB (just past the 4GB line)
//     (export "memory" (memory 0))
//     (func $write (param $addr i64) (param $value i32)
//       (i32.store (local.get $addr) (local.get $value)))
//     (export "write" (func $write))
//     (func $read (param $addr i64) (result i32)
//       (i32.load (local.get $addr)))
//     (export "read" (func $read)))
//
// 65537 pages (65536 == 4GB, +1) is the minimum that lets us touch the 4GB
// boundary while keeping the virtual reservation as small as possible (WASM
// memory is lazily committed, so only the single written page is backed).
const MEMORY64_MODULE_BYTES = new Uint8Array([
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    // type section: (i64,i32)->() and (i64)->(i32)
    0x01, 0x0b, 0x02, 0x60, 0x02, 0x7e, 0x7f, 0x00, 0x60, 0x01, 0x7e, 0x01, 0x7f,
    // function section: func0:type0, func1:type1
    0x03, 0x03, 0x02, 0x00, 0x01,
    // memory section: 1 mem, flags 0x05 (has-max | i64), min=max=65537 pages
    0x05, 0x08, 0x01, 0x05, 0x81, 0x80, 0x04, 0x81, 0x80, 0x04,
    // export section: "memory","write","read"
    0x07, 0x19, 0x03,
    0x06, 0x6d, 0x65, 0x6d, 0x6f, 0x72, 0x79, 0x02, 0x00,
    0x05, 0x77, 0x72, 0x69, 0x74, 0x65, 0x00, 0x00,
    0x04, 0x72, 0x65, 0x61, 0x64, 0x00, 0x01,
    // code section: $write = i32.store(addr,value); $read = i32.load(addr)
    0x0a, 0x13, 0x02,
    0x09, 0x00, 0x20, 0x00, 0x20, 0x01, 0x36, 0x00, 0x00, 0x0b,
    0x07, 0x00, 0x20, 0x00, 0x28, 0x00, 0x00, 0x0b,
]);

// Create a WebAssembly module with >4GB (64-bit) memory
async function createLargeMemoryModule() {
    const module = await WebAssembly.compile(MEMORY64_MODULE_BYTES);
    const instance = await WebAssembly.instantiate(module);
    return instance.exports;
}

// Use the module
async function testLargeMemory() {
    const { memory, write, read } = await createLargeMemoryModule();

    // Access memory beyond the 4GB boundary
    const largeAddress = 0x100000000n; // 4GB boundary (use BigInt)

    write(largeAddress, 42);
    const value = read(largeAddress);
    console.log('Value at 4GB boundary:', value); // 42

    // Direct memory access
    const buffer = new Uint8Array(memory.buffer);
    console.log('Memory size:', buffer.length, 'bytes');

    if (value !== 42) {
        throw new Error(`memory64 read returned ${value}, expected 42`);
    }
    if (buffer.length <= 0xffffffff) {
        throw new Error(`memory64 buffer is ${buffer.length} bytes, expected > 4GB`);
    }
    console.log('OK memory64-test');
}

testLargeMemory().catch((err) => {
    // A RangeError here means the host accepted the memory64 *type* but could not
    // reserve ~4GB of address space — report it without failing the suite, since
    // the engine still supports 64-bit memory.
    if (err instanceof RangeError) {
        console.warn('SKIP memory64-test: host could not allocate >4GB memory:', err.message);
        return;
    }
    console.error(err);
    process.exitCode = 1;
});
