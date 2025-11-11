// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 Syoyo Fujita
// Copyright 2024 Light Transport Entertainment Inc.
//
// Unit tests for PCP (Prim Cache Population) module
#pragma once

#include <string>
#include <vector>
#include <memory>

namespace tinyusdz {
namespace pcp_test {

// Test result structure
struct TestResult {
    bool passed;
    std::string test_name;
    std::string message;
    double time_seconds;
};

// Test suite for PCP
class PcpTestSuite {
public:
    PcpTestSuite();
    ~PcpTestSuite();

    // Run all tests
    std::vector<TestResult> RunAllTests();

    // Individual test categories
    std::vector<TestResult> RunCacheTests();
    std::vector<TestResult> RunLayerStackTests();
    std::vector<TestResult> RunPrimIndexTests();
    std::vector<TestResult> RunNodeTests();
    std::vector<TestResult> RunMapFunctionTests();
    std::vector<TestResult> RunDependencyTests();
    std::vector<TestResult> RunCompositionTests();
    std::vector<TestResult> RunInstanceTests();

    // Specific tests

    // Cache tests
    TestResult TestCacheCreation();
    TestResult TestCacheComputePrimIndex();
    TestResult TestCacheInvalidation();
    TestResult TestCacheVariantFallbacks();
    TestResult TestCachePayloadInclusion();
    TestResult TestCacheExpressionVariables();
    TestResult TestCacheParallelComputation();

    // Layer stack tests
    TestResult TestLayerStackCreation();
    TestResult TestLayerStackSublayers();
    TestResult TestLayerStackRelocates();
    TestResult TestLayerStackTimeOffset();
    TestResult TestLayerStackMutedLayers();
    TestResult TestLayerStackFieldComposition();

    // Prim index tests
    TestResult TestPrimIndexBasic();
    TestResult TestPrimIndexReferences();
    TestResult TestPrimIndexPayloads();
    TestResult TestPrimIndexInherits();
    TestResult TestPrimIndexSpecializes();
    TestResult TestPrimIndexVariants();
    TestResult TestPrimIndexNesting();
    TestResult TestPrimIndexCycles();
    TestResult TestPrimIndexErrors();

    // Node tests
    TestResult TestNodeCreation();
    TestResult TestNodeTraversal();
    TestResult TestNodeFlags();
    TestResult TestNodePathTranslation();
    TestResult TestNodeArcInformation();
    TestResult TestNodeCulling();

    // Map function tests
    TestResult TestMapFunctionIdentity();
    TestResult TestMapFunctionPathMapping();
    TestResult TestMapFunctionTimeOffset();
    TestResult TestMapFunctionComposition();
    TestResult TestMapFunctionInverse();
    TestResult TestMapFunctionRelocates();

    // Dependency tests
    TestResult TestDependencyTracking();
    TestResult TestDependencyQuery();
    TestResult TestDependencyRemoval();
    TestResult TestDependencyTransitive();
    TestResult TestDependencyExpressions();
    TestResult TestDependencyPayloads();

    // Composition tests (LIVRPS)
    TestResult TestCompositionLIVRPS();
    TestResult TestCompositionLocalOpinions();
    TestResult TestCompositionInheritance();
    TestResult TestCompositionVariantSelection();
    TestResult TestCompositionReferences();
    TestResult TestCompositionPayloads();
    TestResult TestCompositionSpecializes();

    // Instance detection tests
    TestResult TestInstanceKeyComputation();
    TestResult TestInstanceSharing();
    TestResult TestInstanceBlake3Hash();
    TestResult TestInstanceableDetection();

    // Integration tests
    TestResult TestComplexComposition();
    TestResult TestNestedReferences();
    TestResult TestVariantWithReferences();
    TestResult TestPayloadControl();
    TestResult TestChangeProcessing();

    // Benchmark tests
    TestResult BenchmarkPrimIndexCreation();
    TestResult BenchmarkInstanceDetection();
    TestResult BenchmarkDependencyQuery();
    TestResult BenchmarkPathTranslation();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;

    // Helper to run a single test
    TestResult RunTest(
        const std::string& name,
        std::function<bool(std::string&)> test_func);

    // Setup test data
    void SetupTestLayers();
    void SetupTestStages();
    void CleanupTestData();
};

// Helper functions for test assertions
bool AssertEqual(const std::string& expected, const std::string& actual, std::string& message);
bool AssertEqual(int expected, int actual, std::string& message);
bool AssertEqual(size_t expected, size_t actual, std::string& message);
bool AssertEqual(double expected, double actual, double epsilon, std::string& message);
bool AssertTrue(bool condition, const std::string& message);
bool AssertFalse(bool condition, const std::string& message);
bool AssertNotNull(const void* ptr, const std::string& message);
bool AssertNull(const void* ptr, const std::string& message);

// Test data generators
std::string GenerateTestUsda(const std::string& content);
std::string GenerateReferenceUsda();
std::string GeneratePayloadUsda();
std::string GenerateInheritUsda();
std::string GenerateVariantUsda();
std::string GenerateComplexUsda();

// Print test results
void PrintTestResults(const std::vector<TestResult>& results);
void PrintTestSummary(const std::vector<TestResult>& results);

} // namespace pcp_test
} // namespace tinyusdz