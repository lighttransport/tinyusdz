// crate_reader.cpp
// Example: Reading USD Crate files using OpenUSD C++ API

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
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/tf/token.h"

PXR_NAMESPACE_USING_DIRECTIVE

// Forward declaration
void TraversePrim(const SdfPrimSpecHandle& prim, int indent);

void PrintValue(const VtValue& value) {
    if (value.IsHolding<int>()) {
        std::cout << value.Get<int>();
    }
    else if (value.IsHolding<float>()) {
        std::cout << value.Get<float>();
    }
    else if (value.IsHolding<double>()) {
        std::cout << value.Get<double>();
    }
    else if (value.IsHolding<std::string>()) {
        std::cout << "\"" << value.Get<std::string>() << "\"";
    }
    else if (value.IsHolding<TfToken>()) {
        std::cout << value.Get<TfToken>().GetString();
    }
    else if (value.IsHolding<GfVec3f>()) {
        const auto& v = value.Get<GfVec3f>();
        std::cout << "(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
    }
    else if (value.IsHolding<GfMatrix4d>()) {
        std::cout << "<GfMatrix4d>";
    }
    else if (value.IsHolding<VtArray<float>>()) {
        const auto& arr = value.Get<VtArray<float>>();
        std::cout << "[" << arr.size() << " floats]";
    }
    else if (value.IsHolding<VtArray<GfVec3f>>()) {
        const auto& arr = value.Get<VtArray<GfVec3f>>();
        std::cout << "[" << arr.size() << " Vec3f]";
    }
    else {
        std::cout << "<" << value.GetTypeName() << ">";
    }
}

void ReadCrateFile(const std::string& filePath) {
    std::cout << "========================================\n";
    std::cout << "Reading Crate File: " << filePath << "\n";
    std::cout << "========================================\n\n";

    // Open the layer (works for both .usdc and .usda)
    SdfLayerRefPtr layer = SdfLayer::FindOrOpen(filePath);
    if (!layer) {
        std::cerr << "Error: Failed to open file: " << filePath << "\n";
        return;
    }

    std::cout << "File format: " << layer->GetFileFormat()->GetFormatId() << "\n";
    std::cout << "Layer identifier: " << layer->GetIdentifier() << "\n\n";

    // Print root layer metadata
    std::cout << "--- Layer Metadata ---\n";
    if (layer->HasDefaultPrim()) {
        std::cout << "  defaultPrim: " << layer->GetDefaultPrim() << "\n";
    }
    if (layer->HasTimeCodesPerSecond()) {
        std::cout << "  timeCodesPerSecond: " << layer->GetTimeCodesPerSecond() << "\n";
    }
    if (layer->HasFramesPerSecond()) {
        std::cout << "  framesPerSecond: " << layer->GetFramesPerSecond() << "\n";
    }
    std::cout << "\n";

    // Traverse all prims
    std::cout << "--- Prims ---\n";
    auto prims = layer->GetRootPrims();

    for (const auto& prim : prims) {
        TraversePrim(prim, 0);
    }
}

void TraversePrim(const SdfPrimSpecHandle& prim, int indent) {
    std::string indentStr(indent * 2, ' ');

    std::cout << indentStr << "Prim: " << prim->GetPath() << "\n";
    std::cout << indentStr << "  Type: " << prim->GetTypeName() << "\n";
    std::cout << indentStr << "  Specifier: " << TfEnum::GetName(prim->GetSpecifier()) << "\n";

    // Print attributes
    const auto& attrs = prim->GetAttributes();
    if (!attrs.empty()) {
        std::cout << indentStr << "  Attributes:\n";
        for (const auto& attr : attrs) {
            std::cout << indentStr << "    " << attr->GetName() << ": ";

            // Check if attribute has default value
            if (attr->HasDefaultValue()) {
                PrintValue(attr->GetDefaultValue());
                std::cout << "\n";
            }

            // Check if attribute has time samples
            if (attr->HasInfo(SdfFieldKeys->TimeSamples)) {
                VtValue timeSamplesValue = attr->GetInfo(SdfFieldKeys->TimeSamples);
                if (timeSamplesValue.IsHolding<SdfTimeSampleMap>()) {
                    const auto& timeSamples = timeSamplesValue.Get<SdfTimeSampleMap>();
                    std::cout << indentStr << "      TimeSamples: " << timeSamples.size() << " samples\n";

                    // Print first few samples
                    int count = 0;
                    for (const auto& [time, value] : timeSamples) {
                        if (count++ >= 3) {
                            std::cout << indentStr << "        ...\n";
                            break;
                        }
                        std::cout << indentStr << "        t=" << time << ": ";
                        PrintValue(value);
                        std::cout << "\n";
                    }
                }
            }
        }
    }

    // Traverse children
    const auto& children = prim->GetNameChildren();
    for (const auto& child : children) {
        TraversePrim(child, indent + 1);
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <file.usdc>\n";
        return 1;
    }

    std::string filePath = argv[1];

    try {
        ReadCrateFile(filePath);
        std::cout << "\nSuccess!\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
