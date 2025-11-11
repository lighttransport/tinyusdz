// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 Syoyo Fujita
// Copyright 2024 Light Transport Entertainment Inc.
//
// Unit tests for PCP implementation

#include "unit-pcp.h"
#include "../../src/tydra/pcp-cache.hh"
#include "../../src/tydra/pcp-layer-stack.hh"
#include "../../src/tydra/pcp-prim-index.hh"
#include "../../src/tydra/pcp-node.hh"
#include "../../src/tydra/pcp-map-function.hh"
#include "../../src/tydra/pcp-dependencies.hh"
#include "../../src/tydra/pcp-compose-site.hh"

#include "../../src/layer.hh"
#include "../../src/stage.hh"
#include "../../src/usda-reader.hh"

#include <chrono>
#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace tinyusdz {
namespace pcp_test {

using namespace tinyusdz::tydra::pcp;

// Implementation class
class PcpTestSuite::Impl {
public:
    // Test data directory
    std::string test_data_dir_;

    // Test layers
    std::unique_ptr<Layer> root_layer_;
    std::unique_ptr<Layer> session_layer_;
    std::unique_ptr<Layer> ref_layer_;
    std::unique_ptr<Layer> payload_layer_;

    Impl() {
        test_data_dir_ = "/tmp/tinyusdz_pcp_test";
        fs::create_directories(test_data_dir_);
    }

    ~Impl() {
        // Cleanup test data
        if (fs::exists(test_data_dir_)) {
            fs::remove_all(test_data_dir_);
        }
    }

    Layer* CreateLayerFromString(const std::string& name, const std::string& content) {
        std::string path = test_data_dir_ + "/" + name;
        std::ofstream out(path);
        out << content;
        out.close();

        auto layer = std::make_unique<Layer>();
        layer->SetIdentifier(path);

        // Parse USDA content
        UsداReader reader;
        std::string err;
        if (!reader.ReadFromString(content, path, layer.get(), &err)) {
            std::cerr << "Failed to parse layer: " << err << std::endl;
            return nullptr;
        }

        return layer.release();
    }
};

PcpTestSuite::PcpTestSuite() : impl_(std::make_unique<Impl>()) {
    SetupTestLayers();
}

PcpTestSuite::~PcpTestSuite() = default;

// Test runner implementation
TestResult PcpTestSuite::RunTest(
    const std::string& name,
    std::function<bool(std::string&)> test_func) {

    TestResult result;
    result.test_name = name;

    auto start = std::chrono::high_resolution_clock::now();

    try {
        std::string message;
        result.passed = test_func(message);
        result.message = message;
    } catch (const std::exception& e) {
        result.passed = false;
        result.message = "Exception: " + std::string(e.what());
    } catch (...) {
        result.passed = false;
        result.message = "Unknown exception";
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
    result.time_seconds = diff.count();

    return result;
}

// Run all tests
std::vector<TestResult> PcpTestSuite::RunAllTests() {
    std::vector<TestResult> results;

    // Run each category
    auto cache_results = RunCacheTests();
    results.insert(results.end(), cache_results.begin(), cache_results.end());

    auto layer_stack_results = RunLayerStackTests();
    results.insert(results.end(), layer_stack_results.begin(), layer_stack_results.end());

    auto prim_index_results = RunPrimIndexTests();
    results.insert(results.end(), prim_index_results.begin(), prim_index_results.end());

    auto node_results = RunNodeTests();
    results.insert(results.end(), node_results.begin(), node_results.end());

    auto map_func_results = RunMapFunctionTests();
    results.insert(results.end(), map_func_results.begin(), map_func_results.end());

    auto dep_results = RunDependencyTests();
    results.insert(results.end(), dep_results.begin(), dep_results.end());

    auto comp_results = RunCompositionTests();
    results.insert(results.end(), comp_results.begin(), comp_results.end());

    auto inst_results = RunInstanceTests();
    results.insert(results.end(), inst_results.begin(), inst_results.end());

    return results;
}

// Cache Tests
std::vector<TestResult> PcpTestSuite::RunCacheTests() {
    std::vector<TestResult> results;

    results.push_back(TestCacheCreation());
    results.push_back(TestCacheComputePrimIndex());
    results.push_back(TestCacheInvalidation());
    results.push_back(TestCacheVariantFallbacks());
    results.push_back(TestCachePayloadInclusion());
    results.push_back(TestCacheExpressionVariables());

    return results;
}

TestResult PcpTestSuite::TestCacheCreation() {
    return RunTest("Cache Creation", [this](std::string& msg) {
        CacheConfig config;
        config.root_layer = impl_->root_layer_.get();
        config.usd_mode = true;

        auto cache = std::make_unique<Cache>(config);

        if (!cache) {
            msg = "Failed to create cache";
            return false;
        }

        if (cache->GetRootLayer() != impl_->root_layer_.get()) {
            msg = "Root layer mismatch";
            return false;
        }

        if (!cache->IsUsdMode()) {
            msg = "USD mode not set";
            return false;
        }

        msg = "Cache created successfully";
        return true;
    });
}

TestResult PcpTestSuite::TestCacheComputePrimIndex() {
    return RunTest("Cache Compute PrimIndex", [this](std::string& msg) {
        auto cache = CacheFactory::CreateUsdCache(impl_->root_layer_.get());

        Path prim_path("/Model");
        std::vector<Error> errors;

        auto prim_index = cache->ComputePrimIndex(prim_path, {}, &errors);

        if (!prim_index) {
            msg = "Failed to compute prim index";
            return false;
        }

        if (!errors.empty()) {
            msg = "Unexpected errors: " + errors[0].message;
            return false;
        }

        // Check it's cached
        if (!cache->HasPrimIndex(prim_path)) {
            msg = "PrimIndex not cached";
            return false;
        }

        // Get from cache
        auto cached_index = cache->GetPrimIndex(prim_path);
        if (cached_index != prim_index) {
            msg = "Cached index mismatch";
            return false;
        }

        msg = "PrimIndex computed and cached successfully";
        return true;
    });
}

// Layer Stack Tests
std::vector<TestResult> PcpTestSuite::RunLayerStackTests() {
    std::vector<TestResult> results;

    results.push_back(TestLayerStackCreation());
    results.push_back(TestLayerStackSublayers());
    results.push_back(TestLayerStackRelocates());
    results.push_back(TestLayerStackTimeOffset());

    return results;
}

TestResult PcpTestSuite::TestLayerStackCreation() {
    return RunTest("LayerStack Creation", [this](std::string& msg) {
        auto layer_stack = LayerStack::Create(
            impl_->root_layer_.get(),
            impl_->session_layer_.get(),
            "test_stack");

        if (!layer_stack) {
            msg = "Failed to create layer stack";
            return false;
        }

        if (layer_stack->GetRootLayer() != impl_->root_layer_.get()) {
            msg = "Root layer mismatch";
            return false;
        }

        if (layer_stack->GetSessionLayer() != impl_->session_layer_.get()) {
            msg = "Session layer mismatch";
            return false;
        }

        auto& layers = layer_stack->GetLayers();
        if (layers.size() < 1) {
            msg = "No layers in stack";
            return false;
        }

        msg = "LayerStack created successfully";
        return true;
    });
}

// PrimIndex Tests
std::vector<TestResult> PcpTestSuite::RunPrimIndexTests() {
    std::vector<TestResult> results;

    results.push_back(TestPrimIndexBasic());
    results.push_back(TestPrimIndexReferences());
    results.push_back(TestPrimIndexVariants());
    results.push_back(TestPrimIndexErrors());

    return results;
}

TestResult PcpTestSuite::TestPrimIndexBasic() {
    return RunTest("PrimIndex Basic", [this](std::string& msg) {
        auto cache = CacheFactory::CreateUsdCache(impl_->root_layer_.get());

        // Create test content
        std::string usda = R"(
#usda 1.0

def Xform "Model" {
    def Mesh "Geometry" {
        int[] faceVertexCounts = [4, 4, 4]
        point3f[] points = [(0,0,0), (1,0,0), (1,1,0), (0,1,0)]
    }
}
)";

        auto layer = impl_->CreateLayerFromString("basic.usda", usda);
        cache = CacheFactory::CreateUsdCache(layer);

        auto index = cache->ComputePrimIndex(Path("/Model"));

        if (!index) {
            msg = "Failed to compute PrimIndex";
            return false;
        }

        if (!index->HasSpecs()) {
            msg = "PrimIndex has no specs";
            return false;
        }

        auto nodes = index->GetNodesInStrengthOrder();
        if (nodes.empty()) {
            msg = "No nodes in PrimIndex";
            return false;
        }

        // Check root node
        auto root_node = index->GetRootNode();
        if (!root_node.IsValid()) {
            msg = "Invalid root node";
            return false;
        }

        if (root_node.GetPath() != Path("/Model")) {
            msg = "Root node path mismatch";
            return false;
        }

        msg = "Basic PrimIndex created successfully";
        return true;
    });
}

TestResult PcpTestSuite::TestPrimIndexReferences() {
    return RunTest("PrimIndex References", [this](std::string& msg) {
        // Create referenced file
        std::string ref_usda = R"(
#usda 1.0

def Xform "Chair" {
    def Mesh "Seat" {}
    def Mesh "Back" {}
    def Mesh "Legs" {}
}
)";

        auto ref_layer = impl_->CreateLayerFromString("chair.usda", ref_usda);

        // Create main file with reference
        std::string main_usda = R"(
#usda 1.0

def "Room" {
    def "Chair_1" (
        references = @./chair.usda@</Chair>
    ) {
        double3 xformOp:translate = (0, 0, 0)
    }
}
)";

        auto main_layer = impl_->CreateLayerFromString("room.usda", main_usda);
        auto cache = CacheFactory::CreateUsdCache(main_layer);

        std::vector<Error> errors;
        auto index = cache->ComputePrimIndex(Path("/Room/Chair_1"), {}, &errors);

        if (!index) {
            msg = "Failed to compute PrimIndex with reference";
            return false;
        }

        auto refs = index->GetReferences();
        if (refs.empty()) {
            msg = "No references found";
            return false;
        }

        auto nodes = index->GetNodesInStrengthOrder();
        if (nodes.size() < 2) {  // Should have root and reference nodes
            msg = "Not enough nodes for reference";
            return false;
        }

        // Check for reference arc
        bool found_ref = false;
        for (const auto& node : nodes) {
            if (node.GetArcType() == ArcType::Reference) {
                found_ref = true;
                break;
            }
        }

        if (!found_ref) {
            msg = "Reference arc not found";
            return false;
        }

        msg = "Reference composition successful";
        return true;
    });
}

TestResult PcpTestSuite::TestPrimIndexVariants() {
    return RunTest("PrimIndex Variants", [this](std::string& msg) {
        std::string usda = R"(
#usda 1.0

def Xform "Model" (
    variantSets = ["quality"]
) {
    variantSet "quality" = {
        "low" {
            def Mesh "Geometry" {
                int complexity = 1
            }
        }
        "medium" {
            def Mesh "Geometry" {
                int complexity = 2
            }
        }
        "high" {
            def Mesh "Geometry" {
                int complexity = 3
            }
        }
    }
}
)";

        auto layer = impl_->CreateLayerFromString("variants.usda", usda);

        CacheConfig config;
        config.root_layer = layer;
        config.usd_mode = true;
        // Set variant fallback to "medium"
        config.variant_fallbacks = {{"quality", {"medium"}}};

        auto cache = std::make_unique<Cache>(config);

        auto index = cache->ComputePrimIndex(Path("/Model"));

        if (!index) {
            msg = "Failed to compute PrimIndex with variants";
            return false;
        }

        auto selections = index->GetVariantSelections();
        if (selections.empty()) {
            msg = "No variant selections found";
            return false;
        }

        // Check for variant arc
        bool found_variant = false;
        for (const auto& node : index->GetNodesInStrengthOrder()) {
            if (node.GetArcType() == ArcType::Variant) {
                found_variant = true;
                break;
            }
        }

        if (!found_variant) {
            msg = "Variant arc not found";
            return false;
        }

        msg = "Variant composition successful";
        return true;
    });
}

TestResult PcpTestSuite::TestPrimIndexErrors() {
    return RunTest("PrimIndex Errors", [this](std::string& msg) {
        // Create file with invalid reference
        std::string usda = R"(
#usda 1.0

def "Model" (
    references = @./nonexistent.usda@</Missing>
) {
}
)";

        auto layer = impl_->CreateLayerFromString("errors.usda", usda);
        auto cache = CacheFactory::CreateUsdCache(layer);

        std::vector<Error> errors;
        auto index = cache->ComputePrimIndex(Path("/Model"), {}, &errors);

        // Should still create index but with errors
        if (!index) {
            msg = "Failed to create PrimIndex with errors";
            return false;
        }

        if (errors.empty()) {
            msg = "Expected errors not reported";
            return false;
        }

        bool found_unresolved = false;
        for (const auto& error : errors) {
            if (error.type == ErrorType::UnresolvedAssetPath) {
                found_unresolved = true;
                break;
            }
        }

        if (!found_unresolved) {
            msg = "Unresolved asset error not found";
            return false;
        }

        msg = "Error handling successful";
        return true;
    });
}

// Node Tests
std::vector<TestResult> PcpTestSuite::RunNodeTests() {
    std::vector<TestResult> results;

    results.push_back(TestNodeCreation());
    results.push_back(TestNodeTraversal());
    results.push_back(TestNodePathTranslation());

    return results;
}

TestResult PcpTestSuite::TestNodeTraversal() {
    return RunTest("Node Traversal", [this](std::string& msg) {
        auto cache = CacheFactory::CreateUsdCache(impl_->root_layer_.get());
        auto index = cache->ComputePrimIndex(Path("/Model"));

        if (!index) {
            msg = "Failed to create PrimIndex";
            return false;
        }

        auto root = index->GetRootNode();

        // Test depth-first traversal
        std::vector<NodeRef> depth_first_nodes;
        VisitNodes(root, [&](NodeRef node) {
            depth_first_nodes.push_back(node);
            return true;
        }, NodeIterator::Order::DepthFirst);

        if (depth_first_nodes.empty()) {
            msg = "Depth-first traversal found no nodes";
            return false;
        }

        // Test strength order traversal
        std::vector<NodeRef> strength_nodes;
        VisitNodes(root, [&](NodeRef node) {
            strength_nodes.push_back(node);
            return true;
        }, NodeIterator::Order::StrengthOrder);

        if (strength_nodes.empty()) {
            msg = "Strength order traversal found no nodes";
            return false;
        }

        msg = "Node traversal successful";
        return true;
    });
}

// Map Function Tests
std::vector<TestResult> PcpTestSuite::RunMapFunctionTests() {
    std::vector<TestResult> results;

    results.push_back(TestMapFunctionIdentity());
    results.push_back(TestMapFunctionPathMapping());
    results.push_back(TestMapFunctionComposition());

    return results;
}

TestResult PcpTestSuite::TestMapFunctionIdentity() {
    return RunTest("MapFunction Identity", [](std::string& msg) {
        auto map = MapFunction::CreateIdentity();

        if (!map) {
            msg = "Failed to create identity map";
            return false;
        }

        if (!map->IsIdentity()) {
            msg = "Map not marked as identity";
            return false;
        }

        Path test_path("/Model/Mesh");
        Path mapped = map->MapPath(test_path);

        if (mapped != test_path) {
            msg = "Identity map changed path";
            return false;
        }

        double test_time = 10.0;
        double mapped_time = map->MapTime(test_time);

        if (mapped_time != test_time) {
            msg = "Identity map changed time";
            return false;
        }

        msg = "Identity map function works correctly";
        return true;
    });
}

TestResult PcpTestSuite::TestMapFunctionPathMapping() {
    return RunTest("MapFunction Path Mapping", [](std::string& msg) {
        Path source("/Model");
        Path target("/World/Props/Model_1");

        auto map = MapFunction::CreatePathMap(source, target);

        if (!map) {
            msg = "Failed to create path map";
            return false;
        }

        if (map->IsIdentity()) {
            msg = "Path map marked as identity";
            return false;
        }

        // Test forward mapping
        Path test_path("/Model/Mesh");
        Path expected("/World/Props/Model_1/Mesh");
        Path mapped = map->MapPath(test_path);

        if (mapped != expected) {
            msg = "Forward mapping failed: " + mapped.full_path_name() +
                  " != " + expected.full_path_name();
            return false;
        }

        // Test reverse mapping
        Path reverse = map->MapPathReverse(mapped);
        if (reverse != test_path) {
            msg = "Reverse mapping failed";
            return false;
        }

        msg = "Path mapping works correctly";
        return true;
    });
}

// Instance Tests
std::vector<TestResult> PcpTestSuite::RunInstanceTests() {
    std::vector<TestResult> results;

    results.push_back(TestInstanceKeyComputation());
    results.push_back(TestInstanceSharing());
    results.push_back(TestInstanceBlake3Hash());

    return results;
}

TestResult PcpTestSuite::TestInstanceKeyComputation() {
    return RunTest("Instance Key Computation", [this](std::string& msg) {
        std::string usda = R"(
#usda 1.0

def Xform "Instance1" (
    instanceable = true
) {
    def Mesh "Mesh" {}
}

def Xform "Instance2" (
    instanceable = true
) {
    def Mesh "Mesh" {}
}
)";

        auto layer = impl_->CreateLayerFromString("instances.usda", usda);
        auto cache = CacheFactory::CreateUsdCache(layer);

        auto index1 = cache->ComputePrimIndex(Path("/Instance1"));
        auto index2 = cache->ComputePrimIndex(Path("/Instance2"));

        if (!index1 || !index2) {
            msg = "Failed to compute instance indexes";
            return false;
        }

        auto key1 = index1->ComputeInstanceKey();
        auto key2 = index2->ComputeInstanceKey();

        // Should have same composition structure
        if (key1.blake3_hash != key2.blake3_hash) {
            msg = "Instance keys don't match for identical composition";
            return false;
        }

        msg = "Instance key computation successful";
        return true;
    });
}

TestResult PcpTestSuite::TestInstanceBlake3Hash() {
    return RunTest("Instance BLAKE3 Hash", [this](std::string& msg) {
        auto layer = impl_->root_layer_.get();
        auto cache = CacheFactory::CreateUsdCache(layer);

        auto index = cache->ComputePrimIndex(Path("/Model"));

        if (!index) {
            msg = "Failed to compute PrimIndex";
            return false;
        }

        auto key = index->ComputeInstanceKey();

        // BLAKE3 hash should be 32 bytes
        if (key.blake3_hash.size() != 32) {
            msg = "BLAKE3 hash wrong size: " + std::to_string(key.blake3_hash.size());
            return false;
        }

        // Hash should not be all zeros
        bool all_zero = true;
        for (uint8_t byte : key.blake3_hash) {
            if (byte != 0) {
                all_zero = false;
                break;
            }
        }

        if (all_zero) {
            msg = "BLAKE3 hash is all zeros";
            return false;
        }

        msg = "BLAKE3 hash computed correctly";
        return true;
    });
}

// Composition Tests (LIVRPS)
std::vector<TestResult> PcpTestSuite::RunCompositionTests() {
    std::vector<TestResult> results;

    results.push_back(TestCompositionLIVRPS());
    results.push_back(TestCompositionLocalOpinions());
    results.push_back(TestCompositionReferences());

    return results;
}

TestResult PcpTestSuite::TestCompositionLIVRPS() {
    return RunTest("Composition LIVRPS Order", [this](std::string& msg) {
        // Create a scene with all arc types to test LIVRPS ordering
        std::string usda = R"(
#usda 1.0

class "_BaseClass" {
    string classAttr = "fromClass"
}

def Xform "Model" (
    inherits = </_BaseClass>
    references = @./ref.usda@</RefModel>
    variantSets = ["variant"]
) {
    string localAttr = "fromLocal"
    string classAttr = "localOverride"

    variantSet "variant" = {
        "selected" {
            string variantAttr = "fromVariant"
        }
    }
}
)";

        auto layer = impl_->CreateLayerFromString("livrps.usda", usda);
        auto cache = CacheFactory::CreateUsdCache(layer);

        auto index = cache->ComputePrimIndex(Path("/Model"));

        if (!index) {
            msg = "Failed to compute PrimIndex";
            return false;
        }

        auto nodes = index->GetNodesInStrengthOrder();

        // Verify arc type ordering
        ArcType prev_type = ArcType::Root;
        for (const auto& node : nodes) {
            ArcType curr_type = node.GetArcType();

            // Check LIVRPS ordering (stronger types should come first)
            if (curr_type != ArcType::Root && !IsStrongerThan(prev_type, curr_type)) {
                msg = "LIVRPS ordering violated";
                return false;
            }

            prev_type = curr_type;
        }

        msg = "LIVRPS ordering correct";
        return true;
    });
}

// Dependency Tests
std::vector<TestResult> PcpTestSuite::RunDependencyTests() {
    std::vector<TestResult> results;

    results.push_back(TestDependencyTracking());
    results.push_back(TestDependencyQuery());

    return results;
}

TestResult PcpTestSuite::TestDependencyTracking() {
    return RunTest("Dependency Tracking", [this](std::string& msg) {
        auto cache = CacheFactory::CreateUsdCache(impl_->root_layer_.get());

        // Compute several prim indexes
        cache->ComputePrimIndex(Path("/Model"));
        cache->ComputePrimIndex(Path("/Model/Child"));

        auto& deps = cache->GetDependencies();

        // Check that dependencies were recorded
        if (!deps.HasDependencies(Path("/Model/Child"))) {
            msg = "No dependencies recorded for child";
            return false;
        }

        // Child should depend on parent
        auto sites = deps.GetSitesDependedOnBy(Path("/Model/Child"));

        bool found_parent = false;
        for (const auto& site : sites) {
            if (site.path == Path("/Model")) {
                found_parent = true;
                break;
            }
        }

        if (!found_parent) {
            msg = "Child doesn't depend on parent";
            return false;
        }

        msg = "Dependencies tracked correctly";
        return true;
    });
}

// Helper functions
void PcpTestSuite::SetupTestLayers() {
    // Create basic root layer
    std::string root_usda = R"(
#usda 1.0

def Xform "Model" {
    def Mesh "Child" {
    }
}
)";

    impl_->root_layer_.reset(impl_->CreateLayerFromString("root.usda", root_usda));

    // Create session layer
    std::string session_usda = R"(
#usda 1.0

over "Model" {
    string sessionOverride = "fromSession"
}
)";

    impl_->session_layer_.reset(impl_->CreateLayerFromString("session.usda", session_usda));
}

// Test assertions
bool AssertEqual(const std::string& expected, const std::string& actual, std::string& message) {
    if (expected != actual) {
        message = "Expected: '" + expected + "', got: '" + actual + "'";
        return false;
    }
    return true;
}

bool AssertEqual(int expected, int actual, std::string& message) {
    if (expected != actual) {
        message = "Expected: " + std::to_string(expected) + ", got: " + std::to_string(actual);
        return false;
    }
    return true;
}

bool AssertEqual(size_t expected, size_t actual, std::string& message) {
    if (expected != actual) {
        message = "Expected: " + std::to_string(expected) + ", got: " + std::to_string(actual);
        return false;
    }
    return true;
}

bool AssertTrue(bool condition, const std::string& message) {
    return condition;
}

bool AssertFalse(bool condition, const std::string& message) {
    return !condition;
}

// Print results
void PrintTestResults(const std::vector<TestResult>& results) {
    for (const auto& result : results) {
        std::cout << "[" << (result.passed ? "PASS" : "FAIL") << "] "
                  << result.test_name
                  << " (" << result.time_seconds * 1000 << " ms)"
                  << std::endl;

        if (!result.passed) {
            std::cout << "  Error: " << result.message << std::endl;
        }
    }
}

void PrintTestSummary(const std::vector<TestResult>& results) {
    int passed = 0;
    int failed = 0;
    double total_time = 0.0;

    for (const auto& result : results) {
        if (result.passed) {
            passed++;
        } else {
            failed++;
        }
        total_time += result.time_seconds;
    }

    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "Total: " << results.size() << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    std::cout << "Total time: " << total_time * 1000 << " ms" << std::endl;
    std::cout << "Success rate: " << (passed * 100.0 / results.size()) << "%" << std::endl;
}

} // namespace pcp_test
} // namespace tinyusdz

// Main test runner
int main(int argc, char** argv) {
    using namespace tinyusdz::pcp_test;

    std::cout << "=== TinyUSDZ PCP Unit Tests ===" << std::endl;

    PcpTestSuite suite;
    auto results = suite.RunAllTests();

    PrintTestResults(results);
    PrintTestSummary(results);

    // Return non-zero if any tests failed
    for (const auto& result : results) {
        if (!result.passed) {
            return 1;
        }
    }

    return 0;
}