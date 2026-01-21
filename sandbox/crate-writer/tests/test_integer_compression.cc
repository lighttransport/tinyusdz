#include <iostream>
#include <vector>
#include <cstring>
#include <iomanip>

// TinyUSDZ integer compression
#include "integerCoding.h"

void PrintHex(const char* label, const char* data, size_t size) {
  std::cout << label << " (" << size << " bytes):\n";
  for (size_t i = 0; i < size; i++) {
    if (i % 16 == 0) {
      std::cout << std::setw(8) << std::setfill('0') << std::hex << i << "  ";
    }
    std::cout << std::setw(2) << std::setfill('0') << std::hex
              << (static_cast<unsigned int>(static_cast<unsigned char>(data[i]))) << " ";
    if ((i + 1) % 16 == 0 || i == size - 1) {
      std::cout << "\n";
    }
  }
  std::cout << std::dec << "\n";
}

int main() {
  std::cout << "=== TinyUSDZ Integer Compression Test ===\n\n";

  // Test Case 1: Simple 2-element array [0, 1]
  {
    std::cout << "Test Case 1: [0, 1]\n";
    std::cout << "-------------------------\n";

    std::vector<int32_t> input = {0, 1};
    std::vector<char> compressed(1024);  // Large buffer
    std::string err;

    size_t compressedSize = tinyusdz::Usd_IntegerCompression::CompressToBuffer(
        input.data(),
        input.size(),
        compressed.data(),
        &err);

    if (compressedSize == 0) {
      std::cerr << "Compression failed: " << err << "\n";
      return 1;
    }

    std::cout << "Input: [0, 1]\n";
    std::cout << "Uncompressed size: " << (input.size() * sizeof(int32_t)) << " bytes\n";
    std::cout << "Compressed size: " << compressedSize << " bytes\n\n";

    PrintHex("Compressed data", compressed.data(), compressedSize);

    // Try to decompress
    std::vector<int32_t> decompressed(input.size());
    std::vector<char> workingSpace(
        tinyusdz::Usd_IntegerCompression::GetDecompressionWorkingSpaceSize(input.size()));

    size_t decompressedSize = tinyusdz::Usd_IntegerCompression::DecompressFromBuffer(
        compressed.data(),
        compressedSize,
        decompressed.data(),
        input.size(),
        &err,
        workingSpace.data());

    if (decompressedSize == 0) {
      std::cerr << "Decompression failed: " << err << "\n";
      return 1;
    }

    std::cout << "Decompressed successfully!\n";
    std::cout << "Decompressed data: [";
    for (size_t i = 0; i < decompressed.size(); i++) {
      std::cout << decompressed[i];
      if (i < decompressed.size() - 1) std::cout << ", ";
    }
    std::cout << "]\n\n";

    // Verify
    bool match = true;
    for (size_t i = 0; i < input.size(); i++) {
      if (input[i] != decompressed[i]) {
        std::cerr << "Mismatch at index " << i << ": expected "
                  << input[i] << ", got " << decompressed[i] << "\n";
        match = false;
      }
    }

    if (match) {
      std::cout << "✅ Round-trip successful!\n\n";
    } else {
      std::cout << "❌ Round-trip FAILED!\n\n";
      return 1;
    }
  }

  // Test Case 2: Path indices from simple_output.usdc [0, 1]
  {
    std::cout << "Test Case 2: Path indices [0, 1]\n";
    std::cout << "-------------------------\n";

    // These should be the actual path indices from the minimal example
    std::vector<int32_t> pathIndices = {0, 1};
    std::vector<char> compressed(1024);
    std::string err;

    size_t compressedSize = tinyusdz::Usd_IntegerCompression::CompressToBuffer(
        pathIndices.data(),
        pathIndices.size(),
        compressed.data(),
        &err);

    if (compressedSize == 0) {
      std::cerr << "Compression failed: " << err << "\n";
      return 1;
    }

    std::cout << "Path indices: [0, 1]\n";
    std::cout << "Compressed size: " << compressedSize << " bytes\n\n";

    PrintHex("Compressed path indices", compressed.data(), compressedSize);
  }

  // Test Case 3: Element token indices [0, 0]
  {
    std::cout << "Test Case 3: Element token indices [0, 0]\n";
    std::cout << "-------------------------\n";

    std::vector<int32_t> elementTokens = {0, 0};
    std::vector<char> compressed(1024);
    std::string err;

    size_t compressedSize = tinyusdz::Usd_IntegerCompression::CompressToBuffer(
        elementTokens.data(),
        elementTokens.size(),
        compressed.data(),
        &err);

    if (compressedSize == 0) {
      std::cerr << "Compression failed: " << err << "\n";
      return 1;
    }

    std::cout << "Element tokens: [0, 0]\n";
    std::cout << "Compressed size: " << compressedSize << " bytes\n\n";

    PrintHex("Compressed element tokens", compressed.data(), compressedSize);
  }

  // Test Case 4: Jumps [-1, -1]
  {
    std::cout << "Test Case 4: Jumps [-1, -1]\n";
    std::cout << "-------------------------\n";

    std::vector<int32_t> jumps = {-1, -1};
    std::vector<char> compressed(1024);
    std::string err;

    size_t compressedSize = tinyusdz::Usd_IntegerCompression::CompressToBuffer(
        jumps.data(),
        jumps.size(),
        compressed.data(),
        &err);

    if (compressedSize == 0) {
      std::cerr << "Compression failed: " << err << "\n";
      return 1;
    }

    std::cout << "Jumps: [-1, -1]\n";
    std::cout << "Compressed size: " << compressedSize << " bytes\n\n";

    PrintHex("Compressed jumps", compressed.data(), compressedSize);
  }

  std::cout << "\n=== All tests completed ===\n";
  return 0;
}
