// crate_writer.cpp
// Example: Writing USD Crate files using OpenUSD C++ API

#include <iostream>
#include <string>
#include <vector>

// OpenUSD headers
#include "pxr/pxr.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/sdf/primSpec.h"
#include "pxr/usd/sdf/attributeSpec.h"
#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/fileFormat.h"
#include "pxr/base/vt/value.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/tf/token.h"

PXR_NAMESPACE_USING_DIRECTIVE

void CreateAnimatedCube(const std::string& outputPath) {
    std::cout << "========================================\n";
    std::cout << "Creating Animated Cube: " << outputPath << "\n";
    std::cout << "========================================\n\n";

    // Create a new layer with .usdc format
    SdfLayerRefPtr layer = SdfLayer::CreateNew(outputPath);
    if (!layer) {
        std::cerr << "Error: Failed to create layer\n";
        return;
    }

    // Set layer metadata
    layer->SetDefaultPrim(TfToken("Cube"));
    layer->SetTimeCodesPerSecond(24.0);
    layer->SetFramesPerSecond(24.0);
    layer->SetStartTimeCode(1.0);
    layer->SetEndTimeCode(100.0);

    std::cout << "Layer created with format: " << layer->GetFileFormat()->GetFormatId() << "\n\n";

    // Create root prim
    SdfPath primPath("/Cube");
    SdfPrimSpecHandle prim = SdfPrimSpec::New(layer, "Cube", SdfSpecifierDef, "Mesh");

    std::cout << "Created prim: " << primPath << " (type: Mesh)\n";

    // Add points attribute (mesh vertices)
    SdfAttributeSpecHandle pointsAttr = SdfAttributeSpec::New(
        prim, "points", SdfValueTypeNames->Point3fArray);

    // Static cube vertices
    VtArray<GfVec3f> cubePoints = {
        GfVec3f(-1, -1, -1), GfVec3f( 1, -1, -1), GfVec3f( 1,  1, -1), GfVec3f(-1,  1, -1),
        GfVec3f(-1, -1,  1), GfVec3f( 1, -1,  1), GfVec3f( 1,  1,  1), GfVec3f(-1,  1,  1)
    };
    pointsAttr->SetDefaultValue(VtValue(cubePoints));
    std::cout << "  Added points: " << cubePoints.size() << " vertices\n";

    // Add face vertex counts
    SdfAttributeSpecHandle faceVertexCountsAttr = SdfAttributeSpec::New(
        prim, "faceVertexCounts", SdfValueTypeNames->IntArray);
    VtArray<int> faceVertexCounts = {4, 4, 4, 4, 4, 4};  // 6 quad faces
    faceVertexCountsAttr->SetDefaultValue(VtValue(faceVertexCounts));

    // Add face vertex indices
    SdfAttributeSpecHandle faceVertexIndicesAttr = SdfAttributeSpec::New(
        prim, "faceVertexIndices", SdfValueTypeNames->IntArray);
    VtArray<int> indices = {
        0, 1, 2, 3,  // front
        4, 5, 6, 7,  // back
        0, 4, 5, 1,  // bottom
        2, 6, 7, 3,  // top
        0, 3, 7, 4,  // left
        1, 5, 6, 2   // right
    };
    faceVertexIndicesAttr->SetDefaultValue(VtValue(indices));
    std::cout << "  Added topology: " << faceVertexCounts.size() << " faces\n";

    // Add extent (bounding box)
    SdfAttributeSpecHandle extentAttr = SdfAttributeSpec::New(
        prim, "extent", SdfValueTypeNames->Float3Array);
    VtArray<GfVec3f> extent = {GfVec3f(-1, -1, -1), GfVec3f(1, 1, 1)};
    extentAttr->SetDefaultValue(VtValue(extent));

    // Create animated translate attribute with TimeSamples
    std::cout << "\n--- Creating Animated Translate ---\n";
    SdfPath xformPath("/Cube/Xform");
    SdfPrimSpecHandle xformPrim = SdfPrimSpec::New(layer->GetPrimAtPath(primPath),
                                                   "Xform", SdfSpecifierDef, "Xform");

    SdfAttributeSpecHandle translateAttr = SdfAttributeSpec::New(
        xformPrim, "xformOp:translate", SdfValueTypeNames->Double3);

    // Create time samples for animation
    SdfTimeSampleMap timeSamples;
    for (double frame = 1.0; frame <= 100.0; frame += 5.0) {
        double t = frame / 24.0;  // Convert to seconds
        double x = std::sin(t * 2.0 * M_PI) * 3.0;
        double y = std::cos(t * 2.0 * M_PI) * 3.0;
        double z = t * 0.5;

        timeSamples[frame] = VtValue(GfVec3d(x, y, z));
    }

    translateAttr->SetInfo(SdfFieldKeys->TimeSamples, VtValue(timeSamples));
    std::cout << "  Added " << timeSamples.size() << " time samples for translate\n";
    std::cout << "  Frame range: 1-100 (sampled every 5 frames)\n";

    // Add xformOpOrder
    SdfAttributeSpecHandle xformOpOrderAttr = SdfAttributeSpec::New(
        xformPrim, "xformOpOrder", SdfValueTypeNames->TokenArray);
    VtArray<TfToken> xformOpOrder = {TfToken("xformOp:translate")};
    xformOpOrderAttr->SetDefaultValue(VtValue(xformOpOrder));

    // Add primvars (per-vertex colors) - animated
    std::cout << "\n--- Creating Animated Colors ---\n";
    SdfAttributeSpecHandle displayColorAttr = SdfAttributeSpec::New(
        prim, "primvars:displayColor", SdfValueTypeNames->Color3fArray);

    // Animated colors via time samples
    SdfTimeSampleMap colorTimeSamples;
    for (double frame = 1.0; frame <= 100.0; frame += 10.0) {
        VtArray<GfVec3f> colors(8);
        float t = static_cast<float>(frame / 100.0);
        for (size_t i = 0; i < 8; ++i) {
            float r = 0.5f + 0.5f * std::sin(t * 6.28f + i * 0.785f);
            float g = 0.5f + 0.5f * std::cos(t * 6.28f + i * 0.785f);
            float b = 0.5f + 0.5f * std::sin(t * 6.28f * 2.0f);
            colors[i] = GfVec3f(r, g, b);
        }
        colorTimeSamples[frame] = VtValue(colors);
    }

    displayColorAttr->SetInfo(SdfFieldKeys->TimeSamples, VtValue(colorTimeSamples));
    std::cout << "  Added " << colorTimeSamples.size() << " color keyframes\n";

    // Save the layer
    std::cout << "\n--- Saving File ---\n";
    if (layer->Save()) {
        std::cout << "Successfully saved to: " << outputPath << "\n";

        // Print file size
        std::cout << "File format: " << layer->GetFileFormat()->GetFormatId() << "\n";
    } else {
        std::cerr << "Error: Failed to save layer\n";
    }
}

void CreateComplexScene(const std::string& outputPath) {
    std::cout << "\n========================================\n";
    std::cout << "Creating Complex Scene: " << outputPath << "\n";
    std::cout << "========================================\n\n";

    SdfLayerRefPtr layer = SdfLayer::CreateNew(outputPath);
    if (!layer) {
        std::cerr << "Error: Failed to create layer\n";
        return;
    }

    layer->SetDefaultPrim(TfToken("Scene"));
    layer->SetTimeCodesPerSecond(24.0);

    // Create scene root
    SdfPrimSpecHandle root = SdfPrimSpec::New(layer, "Scene", SdfSpecifierDef, "Xform");

    // Create multiple animated spheres
    std::cout << "Creating 5 animated spheres...\n";
    for (int i = 0; i < 5; ++i) {
        std::string sphereName = "Sphere" + std::to_string(i);
        SdfPath spherePath = root->GetPath().AppendChild(TfToken(sphereName));

        SdfPrimSpecHandle sphere = SdfPrimSpec::New(
            root, sphereName, SdfSpecifierDef, "Sphere");

        // Radius
        SdfAttributeSpecHandle radiusAttr = SdfAttributeSpec::New(
            sphere, "radius", SdfValueTypeNames->Double);
        radiusAttr->SetDefaultValue(VtValue(1.0));

        // Animated translate
        SdfAttributeSpecHandle translateAttr = SdfAttributeSpec::New(
            sphere, "xformOp:translate", SdfValueTypeNames->Double3);

        SdfTimeSampleMap translateSamples;
        for (double frame = 1.0; frame <= 50.0; frame += 2.0) {
            double angle = (frame / 50.0) * 2.0 * M_PI + (i * 2.0 * M_PI / 5.0);
            double radius = 5.0;
            double x = radius * std::cos(angle);
            double y = radius * std::sin(angle);
            double z = std::sin(frame / 10.0) * 2.0;

            translateSamples[frame] = VtValue(GfVec3d(x, y, z));
        }

        translateAttr->SetInfo(SdfFieldKeys->TimeSamples, VtValue(translateSamples));

        // xformOpOrder
        SdfAttributeSpecHandle xformOpOrderAttr = SdfAttributeSpec::New(
            sphere, "xformOpOrder", SdfValueTypeNames->TokenArray);
        VtArray<TfToken> xformOpOrder = {TfToken("xformOp:translate")};
        xformOpOrderAttr->SetDefaultValue(VtValue(xformOpOrder));

        std::cout << "  Created " << sphereName << " with "
                  << translateSamples.size() << " keyframes\n";
    }

    if (layer->Save()) {
        std::cout << "\nSuccessfully saved complex scene to: " << outputPath << "\n";
    } else {
        std::cerr << "Error: Failed to save layer\n";
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <output.usdc> [complex]\n";
        std::cerr << "  Add 'complex' argument to create complex scene\n";
        return 1;
    }

    std::string outputPath = argv[1];
    bool createComplex = (argc > 2 && std::string(argv[2]) == "complex");

    try {
        if (createComplex) {
            CreateComplexScene(outputPath);
        } else {
            CreateAnimatedCube(outputPath);
        }

        std::cout << "\n========================================\n";
        std::cout << "Success!\n";
        std::cout << "========================================\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
