# Task Queue

A simple, lock-free task queue implementation for C++14 with two variants:
- **TaskQueue**: C function pointer version (`void (*)(void*)`)
- **TaskQueueFunc**: `std::function<void()>` version

## Features

- **Lock-free when possible**: Uses compiler builtin atomics on GCC/Clang/MSVC
- **Automatic fallback**: Falls back to `std::mutex` + `std::atomic` when builtins unavailable
- **Fixed-size ring buffer**: Pre-allocated, no dynamic allocation during runtime
- **Thread-safe**: Safe for multiple producers and consumers
- **Simple API**: Push, Pop, Size, Empty, Clear operations

## Compiler Detection

The implementation automatically detects compiler support:
- GCC and Clang: Uses `__atomic_*` builtins
- MSVC 2015+: Uses compiler intrinsics
- Others: Falls back to mutex-based implementation

## API

### TaskQueue (C Function Pointer Version)

```cpp
// Create queue with capacity
TaskQueue queue(1024);

// Define task function
void my_task(void* user_data) {
  // Process task
}

// Push task
void* data = ...;
if (queue.Push(my_task, data)) {
  // Task queued successfully
}

// Pop and execute task
TaskItem task;
if (queue.Pop(task)) {
  if (task.func) {
    task.func(task.user_data);
  }
}

// Query state
size_t size = queue.Size();
bool empty = queue.Empty();
size_t cap = queue.Capacity();

// Clear all tasks
queue.Clear();
```

### TaskQueueFunc (std::function Version)

```cpp
// Create queue with capacity
TaskQueueFunc queue(1024);

// Push lambda tasks
queue.Push([]() {
  std::cout << "Hello from task!" << std::endl;
});

// Push with captures
int value = 42;
queue.Push([value]() {
  std::cout << "Value: " << value << std::endl;
});

// Pop and execute task
TaskItemFunc task;
if (queue.Pop(task)) {
  if (task.func) {
    task.func();
  }
}
```

## Building

```bash
# Build example
make

# Run example
make run

# Build debug version with ThreadSanitizer
make debug

# Build without exceptions and RTTI (for embedded/constrained environments)
make no-except

# Run no-exceptions build
make run-no-except

# Clean
make clean
```

## No Exceptions, No RTTI

The implementation is designed to work without C++ exceptions or RTTI:
- **No exceptions**: Uses return values (bool) for error handling
- **No RTTI**: No `dynamic_cast`, `typeid`, or other RTTI features
- **Suitable for**: Embedded systems, game engines, performance-critical code

To verify:
```bash
g++ -std=c++17 -fno-exceptions -fno-rtti -c task-queue.hh
```

## Example Output

```
========================================
  Task Queue Example and Tests
========================================

=== Build Configuration ===
  Lock-free atomics: ENABLED (using compiler builtins)
  Compiler: GCC 11.4

=== Test: Basic Operations ===
  Counter value: 60 (expected 60)
  PASSED

=== Test: std::function Version ===
  Counter value: 100 (expected 100)
  PASSED

=== Test: Queue Full Behavior ===
  Pushed 8 tasks (capacity: 8)
  Queue cleared successfully
  PASSED

=== Test: Multi-threaded Producer-Consumer ===
  Counter value: 4000 (expected 4000)
  PASSED

========================================
  All tests PASSED!
========================================
```

## Implementation Details

### Lock-Free Version

When compiler builtins are available, the implementation uses:
- `__atomic_load_n()` with `__ATOMIC_ACQUIRE`
- `__atomic_store_n()` with `__ATOMIC_RELEASE`
- Plain `uint64_t` for position counters (no `std::atomic` wrapper)

This provides true lock-free operation for single producer/single consumer scenarios
and minimal contention for multiple producers/consumers.

### Mutex Fallback Version

When builtins are not available:
- Uses `std::atomic<uint64_t>` for position counters
- Uses `std::mutex` to protect the entire Push/Pop operation
- Provides correct behavior but with lock contention overhead

### Ring Buffer Design

- Fixed-size circular buffer using modulo indexing
- Write position increases on Push, read position on Pop
- Full condition: `(write_pos - read_pos) > capacity`
- Empty condition: `read_pos >= write_pos`

## Performance Considerations

1. **Capacity**: Choose capacity based on expected burst size. Too small = frequent full queue rejections. Too large = wasted memory.

2. **False Sharing**: On high-contention scenarios, consider padding the position variables to cache line boundaries (64 bytes).

3. **std::function Overhead**: The function version has overhead from `std::function` type erasure. Use the C function pointer version for maximum performance.

4. **Memory Order**: Uses acquire/release semantics for correctness without unnecessary barriers.

## Thread Safety

- **Multiple producers**: Safe, but may experience contention on write position
- **Multiple consumers**: Safe, but may experience contention on read position
- **Mixed**: Safe for any combination of producer/consumer threads

Note: In lock-free mode, `Size()` returns an approximate value due to relaxed ordering between reads of write_pos and read_pos.

## Limitations

1. **Fixed capacity**: Cannot grow dynamically
2. **No blocking**: Push returns false when full, Pop returns false when empty
3. **No priorities**: FIFO order only
4. **ABA problem**: Not addressed (acceptable for this use case with monotonic counters)

## Use Cases

- Thread pool task distribution
- Event dispatch systems
- Lock-free message passing
- Producer-consumer patterns
- Async I/O completion handlers

## License

Same as TinyUSDZ (MIT or Apache 2.0)
