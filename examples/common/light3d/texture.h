#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace light3d {

struct Image {
    int width = 0;
    int height = 0;
    int channels = 0;  // 1=gray, 2=gray+alpha, 3=RGB, 4=RGBA
    std::vector<uint8_t> data;

    bool isValid() const { return width > 0 && height > 0 && !data.empty(); }
};

// Load an image from disk. Returns an invalid Image on failure.
// desiredChannels: 0 = use file's channel count, 1-4 = force that many channels.
Image loadImage(const std::string& filepath, int desiredChannels = 0);

class TextureLibrary {
public:
    // Add a texture (image + name). Returns the assigned ID.
    int addTexture(Image image, const std::string& name = "");

    // Retrieve texture by ID. Returns nullptr if not found.
    const Image* getTexture(int id) const;
    Image* getTexture(int id);

    // Find texture by name. Returns nullptr if not found.
    const Image* findByName(const std::string& name) const;

    // Find texture ID by name. Returns -1 if not found.
    int findIdByName(const std::string& name) const;

    int count() const { return static_cast<int>(textures_.size()); }

    const std::vector<Image>& textures() const { return textures_; }

private:
    std::vector<Image> textures_;
    std::unordered_map<std::string, int> nameToIndex_;
};

} // namespace light3d
