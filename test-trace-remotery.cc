#include "src/logger.hh"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <cmath>

// Test function that performs some computation
void computeIntensive(int iterations) {
    TUSDZ_TRACE_TAG("compute", "intensive");
    
    double sum = 0;
    for (int i = 0; i < iterations; ++i) {
        sum += std::sin(i) * std::cos(i);
    }
}

// Test function with nested traces
void nestedFunction() {
    TUSDZ_TRACE("nested_outer");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    {
        TUSDZ_TRACE_TAG("nested", "inner1");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    
    {
        TUSDZ_TRACE_TAG("nested", "inner2");
        std::this_thread::sleep_for(std::chrono::milliseconds(7));
    }
}

// Test function that uses function-level tracing
void functionLevelTrace() {
    TUSDZ_TRACE_FUNCTION();
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
}

// Test parallel execution
void parallelWork(int thread_id) {
    TUSDZ_TRACE_TAG("parallel", std::string("thread_" + std::to_string(thread_id)));
    
    // Simulate work
    std::this_thread::sleep_for(std::chrono::milliseconds(20 + thread_id * 5));
    computeIntensive(10000 * thread_id);
}

int main(int argc, char* argv[]) {
    std::cout << "TinyUSDZ Trace with Remotery Support Test\n";
    std::cout << "==========================================\n\n";
    
    // Check command line arguments
    bool use_remotery = false;
    bool use_json = false;
    bool enable_event_logging = false;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--remotery") {
            use_remotery = true;
        } else if (arg == "--json") {
            use_json = true;
        } else if (arg == "--events") {
            enable_event_logging = true;
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --remotery    Use Remotery backend for tracing\n";
            std::cout << "  --json        Output in JSON format (internal tracing only)\n";
            std::cout << "  --events      Enable event logging\n";
            std::cout << "  --help        Show this help message\n";
            return 0;
        }
    }
    
    // Configure tracing
    TUSDZ_TRACE_ENABLE();
    
#ifdef RMT_ENABLED
    if (use_remotery) {
        if (TUSDZ_TRACE_IS_REMOTERY_AVAILABLE()) {
            std::cout << "Using Remotery backend for tracing\n";
            TUSDZ_TRACE_USE_REMOTERY();
            std::cout << "Remotery web viewer should be available at: http://localhost:17815/\n\n";
        } else {
            std::cout << "Remotery is not available, falling back to internal tracing\n";
            use_remotery = false;
        }
    } else {
        std::cout << "Using internal tracing system\n";
    }
#else
    std::cout << "Remotery support not compiled in (build with -DTINYUSDZ_WITH_REMOTERY=ON)\n";
    std::cout << "Using internal tracing system\n";
#endif
    
    if (!use_remotery) {
        if (use_json) {
            TUSDZ_TRACE_SET_FORMAT_JSON();
            std::cout << "Output format: JSON\n";
        } else {
            TUSDZ_TRACE_SET_FORMAT_TEXT();
            std::cout << "Output format: Plain Text\n";
        }
        
        if (enable_event_logging) {
            if (use_json) {
                TUSDZ_TRACE_SET_EVENT_LOGGING_JSON();
            } else {
                TUSDZ_TRACE_SET_EVENT_LOGGING_TEXT();
            }
            std::cout << "Event logging enabled\n";
        }
    }
    
    std::cout << "\nStarting trace tests...\n\n";
    
    // Test 1: Basic tracing
    {
        TUSDZ_TRACE("test_basic");
        std::cout << "Running basic test...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // Test 2: Tagged tracing
    {
        TUSDZ_TRACE_TAG("test", "tagged");
        std::cout << "Running tagged test...\n";
        computeIntensive(100000);
    }
    
    // Test 3: Nested tracing
    {
        TUSDZ_TRACE("test_nested");
        std::cout << "Running nested test...\n";
        nestedFunction();
    }
    
    // Test 4: Function-level tracing
    {
        std::cout << "Running function-level trace test...\n";
        functionLevelTrace();
    }
    
    // Test 5: Parallel execution
    {
        TUSDZ_TRACE("test_parallel");
        std::cout << "Running parallel test with 4 threads...\n";
        
        std::vector<std::thread> threads;
        for (int i = 0; i < 4; ++i) {
            threads.emplace_back(parallelWork, i);
        }
        
        for (auto& t : threads) {
            t.join();
        }
    }
    
    // Test 6: Selective tag enable/disable (only for internal tracing)
    if (!use_remotery) {
        std::cout << "\nTesting selective tag filtering...\n";
        
        TUSDZ_TRACE_DISABLE_TAG("filtered");
        
        {
            TUSDZ_TRACE("allowed");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        {
            TUSDZ_TRACE("filtered");  // This should be filtered out
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
#ifdef RMT_ENABLED
    // Test 7: Direct Remotery macros (if using Remotery)
    if (use_remotery) {
        std::cout << "\nTesting direct Remotery macros...\n";
        
        // Note: Remotery macros require valid C identifiers
        TUSDZ_REMOTERY_BEGIN(direct_remotery_test);
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        computeIntensive(50000);
        TUSDZ_REMOTERY_END();
        
        {
            TUSDZ_REMOTERY_SCOPE(scoped_remotery_test);
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
    }
#endif
    
    std::cout << "\nAll tests completed!\n\n";
    
    // Print summary (only for internal tracing)
    if (!use_remotery) {
        std::cout << "Trace Summary:\n";
        std::cout << "==============\n";
        TUSDZ_TRACE_SUMMARY();
        
        if (use_json) {
            std::cout << "\nDetailed JSON output:\n";
            TUSDZ_TRACE_DETAILED_JSON();
        }
    } else {
        std::cout << "Trace data has been sent to Remotery.\n";
        std::cout << "Open http://localhost:17815/ in a web browser to view the profiling data.\n";
        std::cout << "Press Enter to exit...\n";
        std::cin.get();
    }
    
    return 0;
}