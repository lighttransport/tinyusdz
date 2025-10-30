// Example C++ application comparing OpenUSD and TinyUSDZ
#include <iostream>
#include <memory>
#include <string>

// OpenUSD headers
#include <pxr/pxr.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/base/tf/diagnostic.h>

// TinyUSDZ headers
#include "tinyusdz.hh"
#include "tydra/render-data.hh"

PXR_NAMESPACE_USING_DIRECTIVE

void analyzeWithOpenUSD(const std::string& filepath) {
    std::cout << "\n=== OpenUSD Analysis ===" << std::endl;

    // Open the USD stage
    auto stage = UsdStage::Open(filepath);
    if (!stage) {
        std::cerr << "Failed to open stage with OpenUSD: " << filepath << std::endl;
        return;
    }

    std::cout << "Successfully opened: " << filepath << std::endl;
    std::cout << "Root layer: " << stage->GetRootLayer()->GetIdentifier() << std::endl;

    // Get metadata
    std::cout << "Up axis: " << UsdGeomGetStageUpAxis(stage) << std::endl;
    std::cout << "Meters per unit: " << UsdGeomGetStageMetersPerUnit(stage) << std::endl;

    // Count and list prims
    size_t primCount = 0;
    std::cout << "\nPrims in stage:" << std::endl;

    auto range = stage->Traverse();
    for (auto it = range.begin(); it != range.end(); ++it) {
        UsdPrim prim = *it;
        primCount++;

        std::cout << "  " << prim.GetPath().GetString()
                  << " [" << prim.GetTypeName() << "]";

        // Check if it's a mesh
        if (prim.IsA<UsdGeomMesh>()) {
            UsdGeomMesh mesh(prim);
            VtIntArray faceVertexCounts;
            mesh.GetFaceVertexCountsAttr().Get(&faceVertexCounts);
            std::cout << " - " << faceVertexCounts.size() << " faces";
        }

        std::cout << std::endl;

        // Only show first 10 prims
        if (primCount >= 10) {
            std::cout << "  ... (and more)" << std::endl;
            break;
        }
    }

    // Count total prims
    size_t totalPrims = std::distance(stage->Traverse().begin(),
                                      stage->Traverse().end());
    std::cout << "Total prims: " << totalPrims << std::endl;
}

void analyzeWithTinyUSDZ(const std::string& filepath) {
    std::cout << "\n=== TinyUSDZ Analysis ===" << std::endl;

    tinyusdz::Stage stage;
    std::string warn, err;

    // Load the USD file
    bool ret = tinyusdz::LoadUSDFromFile(filepath, &stage, &warn, &err);

    if (!warn.empty()) {
        std::cout << "Warnings: " << warn << std::endl;
    }

    if (!err.empty()) {
        std::cerr << "Errors: " << err << std::endl;
    }

    if (!ret) {
        std::cerr << "Failed to load USD file with TinyUSDZ" << std::endl;
        return;
    }

    std::cout << "Successfully loaded: " << filepath << std::endl;

    // Get metadata
    if (stage.metas().upAxis.has_value()) {
        std::cout << "Up axis: " << stage.metas().upAxis.value() << std::endl;
    }

    if (stage.metas().metersPerUnit.has_value()) {
        std::cout << "Meters per unit: " << stage.metas().metersPerUnit.value() << std::endl;
    }

    // Show root prims
    std::cout << "\nRoot prims:" << std::endl;
    size_t primCount = 0;

    for (const auto& rootPrim : stage.root_prims()) {
        std::cout << "  /" << rootPrim.element_name();

        // Get prim type
        if (rootPrim.is_model()) {
            std::cout << " [Model]";
        } else if (rootPrim.is_xform()) {
            std::cout << " [Xform]";
        }

        std::cout << std::endl;
        primCount++;

        // Count children (simplified)
        if (!rootPrim.children().empty()) {
            std::cout << "    Has " << rootPrim.children().size() << " children" << std::endl;
            primCount += rootPrim.children().size();
        }
    }

    std::cout << "Estimated prim count: " << primCount << std::endl;

    // Try Tydra conversion
    std::cout << "\nTrying Tydra conversion..." << std::endl;
    tinyusdz::tydra::RenderScene renderScene;
    tinyusdz::tydra::RenderSceneConverter converter;

    if (converter.ConvertToRenderScene(stage, &renderScene)) {
        std::cout << "Tydra conversion successful!" << std::endl;
        std::cout << "  Meshes: " << renderScene.meshes.size() << std::endl;
        std::cout << "  Materials: " << renderScene.materials.size() << std::endl;
        std::cout << "  Nodes: " << renderScene.nodes.size() << std::endl;
    } else {
        std::cout << "Tydra conversion not performed (might not be a renderable scene)" << std::endl;
    }
}

int main(int argc, char* argv[]) {
    std::cout << "USD Comparison Tool (OpenUSD + TinyUSDZ)" << std::endl;
    std::cout << "=========================================" << std::endl;

    std::string filepath;

    if (argc > 1) {
        filepath = argv[1];
    } else {
        // Default test file
        filepath = "../../models/suzanne.usda";
        std::cout << "No file specified, using default: " << filepath << std::endl;
    }

    // Analyze with both libraries
    try {
        analyzeWithOpenUSD(filepath);
    } catch (const std::exception& e) {
        std::cerr << "OpenUSD exception: " << e.what() << std::endl;
    }

    try {
        analyzeWithTinyUSDZ(filepath);
    } catch (const std::exception& e) {
        std::cerr << "TinyUSDZ exception: " << e.what() << std::endl;
    }

    std::cout << "\n=========================================" << std::endl;
    std::cout << "Comparison complete!" << std::endl;

    return 0;
}