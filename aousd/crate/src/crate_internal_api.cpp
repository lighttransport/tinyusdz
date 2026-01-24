// crate_internal_api.cpp
// Advanced example: Using OpenUSD internal Crate API directly
// This demonstrates low-level ValueRep, TimeSamples, and CrateFile access

#include <iostream>
#include <fstream>
#include <string>
#include <vector>

// OpenUSD internal headers (not public API, for educational purposes)
#include "pxr/pxr.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/primSpec.h"
#include "pxr/usd/sdf/attributeSpec.h"
#include "pxr/usd/sdf/fileFormat.h"
#include "pxr/usd/sdf/types.h"
#include "pxr/base/vt/value.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/tf/token.h"

// Note: These internal headers may not be installed by default
// This example shows what's possible, but for production use SdfLayer API
// #include "pxr/usd/sdf/crateFile.h"
// #include "pxr/usd/sdf/crateData.h"

PXR_NAMESPACE_USING_DIRECTIVE

// Helper to inspect file format details
void InspectFileFormat(const std::string& filePath) {
    std::cout << "========================================\n";
    std::cout << "File Format Inspection: " << filePath << "\n";
    std::cout << "========================================\n\n";

    // Check if file exists
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        std::cerr << "Error: File not found\n";
        return;
    }

    // Read bootstrap header (first 64 bytes)
    char header[64];
    file.read(header, 64);

    std::cout << "--- Bootstrap Header ---\n";

    // Check magic (first 8 bytes should be "PXR-USDC")
    std::string magic(header, 8);
    std::cout << "Magic: ";
    for (int i = 0; i < 8; ++i) {
        if (std::isprint(header[i])) {
            std::cout << header[i];
        } else {
            std::cout << "\\x" << std::hex << (int)(unsigned char)header[i] << std::dec;
        }
    }
    std::cout << "\n";

    if (magic == "PXR-USDC") {
        std::cout << "  ✓ Valid Crate file\n";

        // Read version (next 8 bytes)
        uint8_t major = static_cast<uint8_t>(header[8]);
        uint8_t minor = static_cast<uint8_t>(header[9]);
        uint8_t patch = static_cast<uint8_t>(header[10]);

        std::cout << "Version: " << (int)major << "." << (int)minor << "." << (int)patch << "\n";

        // Read TOC offset (bytes 16-24, int64_t)
        int64_t tocOffset;
        std::memcpy(&tocOffset, header + 16, sizeof(int64_t));
        std::cout << "TOC Offset: 0x" << std::hex << tocOffset << std::dec
                  << " (" << tocOffset << " bytes)\n";

        // Get file size
        file.seekg(0, std::ios::end);
        size_t fileSize = file.tellg();
        std::cout << "File Size: " << fileSize << " bytes\n";

        std::cout << "\n--- Format Details ---\n";
        std::cout << "Bootstrap: 64 bytes\n";
        std::cout << "Value Data: 0x40 - 0x" << std::hex << tocOffset << std::dec << "\n";
        std::cout << "Structural Sections: 0x" << std::hex << tocOffset << " - 0x"
                  << fileSize << std::dec << "\n";

    } else {
        std::cout << "  ✗ Not a Crate file (wrong magic)\n";
    }

    file.close();
}

// Analyze TimeSamples in a layer
void AnalyzeTimeSamples(const std::string& filePath) {
    std::cout << "\n========================================\n";
    std::cout << "TimeSamples Analysis: " << filePath << "\n";
    std::cout << "========================================\n\n";

    SdfLayerRefPtr layer = SdfLayer::FindOrOpen(filePath);
    if (!layer) {
        std::cerr << "Error: Failed to open file\n";
        return;
    }

    size_t totalTimeSamples = 0;
    size_t attributesWithTimeSamples = 0;

    std::cout << "--- Scanning for Animated Attributes ---\n\n";

    // Recursive function to traverse all prims
    std::function<void(const SdfPrimSpecHandle&, const std::string&)> traverse;
    traverse = [&](const SdfPrimSpecHandle& prim, const std::string& indent) {
        const auto& attrs = prim->GetAttributes();

        for (const auto& attr : attrs) {
            if (attr->HasInfo(SdfFieldKeys->TimeSamples)) {
                VtValue timeSamplesValue = attr->GetInfo(SdfFieldKeys->TimeSamples);

                if (timeSamplesValue.IsHolding<SdfTimeSampleMap>()) {
                    const auto& timeSamples = timeSamplesValue.Get<SdfTimeSampleMap>();

                    std::cout << indent << attr->GetPath() << "\n";
                    std::cout << indent << "  Samples: " << timeSamples.size() << "\n";

                    if (!timeSamples.empty()) {
                        double firstTime = timeSamples.begin()->first;
                        double lastTime = timeSamples.rbegin()->first;
                        std::cout << indent << "  Time Range: [" << firstTime
                                  << " - " << lastTime << "]\n";

                        // Analyze value types
                        const VtValue& firstValue = timeSamples.begin()->second;
                        std::cout << indent << "  Value Type: "
                                  << firstValue.GetTypeName() << "\n";

                        // Check for uniform sampling
                        if (timeSamples.size() > 2) {
                            auto it = timeSamples.begin();
                            double t0 = it->first;
                            ++it;
                            double t1 = it->first;
                            double interval = t1 - t0;

                            bool uniform = true;
                            double prevTime = t1;
                            ++it;

                            while (it != timeSamples.end()) {
                                double currentInterval = it->first - prevTime;
                                if (std::abs(currentInterval - interval) > 1e-6) {
                                    uniform = false;
                                    break;
                                }
                                prevTime = it->first;
                                ++it;
                            }

                            std::cout << indent << "  Sampling: "
                                      << (uniform ? "Uniform" : "Non-uniform")
                                      << " (interval: " << interval << ")\n";
                        }

                        totalTimeSamples += timeSamples.size();
                        attributesWithTimeSamples++;
                    }

                    std::cout << "\n";
                }
            }
        }

        // Recurse to children
        for (const auto& child : prim->GetNameChildren()) {
            traverse(child, indent + "  ");
        }
    };

    // Traverse all root prims
    for (const auto& prim : layer->GetRootPrims()) {
        traverse(prim, "");
    }

    std::cout << "--- Summary ---\n";
    std::cout << "Total animated attributes: " << attributesWithTimeSamples << "\n";
    std::cout << "Total time samples: " << totalTimeSamples << "\n";
    if (attributesWithTimeSamples > 0) {
        std::cout << "Average samples per attribute: "
                  << (totalTimeSamples / attributesWithTimeSamples) << "\n";
    }
}

// Compare file sizes between formats
void CompareFormats(const std::string& inputPath) {
    std::cout << "\n========================================\n";
    std::cout << "Format Comparison\n";
    std::cout << "========================================\n\n";

    SdfLayerRefPtr layer = SdfLayer::FindOrOpen(inputPath);
    if (!layer) {
        std::cerr << "Error: Failed to open input file\n";
        return;
    }

    // Get original file size
    std::ifstream originalFile(inputPath, std::ios::binary | std::ios::ate);
    size_t originalSize = originalFile.tellg();
    originalFile.close();

    std::string originalFormat = layer->GetFileFormat()->GetFormatId();
    std::cout << "Original Format: " << originalFormat << "\n";
    std::cout << "Original Size: " << originalSize << " bytes\n\n";

    // Export to different formats for comparison
    std::vector<std::pair<std::string, std::string>> formats = {
        {"usda", inputPath + ".ascii.usda"},
        {"usdc", inputPath + ".binary.usdc"}
    };

    for (const auto& [formatId, outputPath] : formats) {
        std::cout << "Exporting to " << formatId << "...\n";

        SdfLayerRefPtr exportLayer = SdfLayer::CreateNew(outputPath);
        if (exportLayer) {
            exportLayer->TransferContent(layer);

            if (exportLayer->Save()) {
                std::ifstream exportFile(outputPath, std::ios::binary | std::ios::ate);
                size_t exportSize = exportFile.tellg();
                exportFile.close();

                double ratio = (double)exportSize / originalSize;
                std::cout << "  File: " << outputPath << "\n";
                std::cout << "  Size: " << exportSize << " bytes\n";
                std::cout << "  Ratio: " << (ratio * 100.0) << "% of original\n";

                if (exportSize < originalSize) {
                    size_t savings = originalSize - exportSize;
                    std::cout << "  Savings: " << savings << " bytes ("
                              << ((1.0 - ratio) * 100.0) << "% smaller)\n";
                }

                std::cout << "\n";
            }
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <command> <file.usdc>\n\n";
        std::cerr << "Commands:\n";
        std::cerr << "  inspect <file>     - Inspect binary format details\n";
        std::cerr << "  timesamples <file> - Analyze TimeSamples data\n";
        std::cerr << "  compare <file>     - Compare format sizes\n";
        return 1;
    }

    std::string command = argv[1];

    try {
        if (command == "inspect" && argc >= 3) {
            InspectFileFormat(argv[2]);
        }
        else if (command == "timesamples" && argc >= 3) {
            AnalyzeTimeSamples(argv[2]);
        }
        else if (command == "compare" && argc >= 3) {
            CompareFormats(argv[2]);
        }
        else {
            std::cerr << "Error: Invalid command or missing file argument\n";
            return 1;
        }

        std::cout << "\n========================================\n";
        std::cout << "Done!\n";
        std::cout << "========================================\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
