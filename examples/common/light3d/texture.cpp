#include "light3d/texture.h"

namespace light3d {

// NOTE: tusdview does NOT load image files through light3d. Texture pixels come
// already-decoded from the tinyusdz Tydra RenderScene (TextureImage / BufferData),
// so the original stb_image-based loadImage was removed to avoid duplicating
// tinyusdz's own STB_IMAGE_IMPLEMENTATION (which would clash at link time).
// The declaration is kept for API compatibility; it returns an invalid Image.
Image loadImage(const std::string& /*filepath*/, int /*desiredChannels*/) {
    return Image{};
}

// --- TextureLibrary ---

int TextureLibrary::addTexture(Image image, const std::string& name) {
    int id = static_cast<int>(textures_.size());
    if (!name.empty()) {
        nameToIndex_[name] = id;
    }
    textures_.push_back(std::move(image));
    return id;
}

const Image* TextureLibrary::getTexture(int id) const {
    if (id < 0 || id >= static_cast<int>(textures_.size())) return nullptr;
    return &textures_[id];
}

Image* TextureLibrary::getTexture(int id) {
    if (id < 0 || id >= static_cast<int>(textures_.size())) return nullptr;
    return &textures_[id];
}

const Image* TextureLibrary::findByName(const std::string& name) const {
    auto it = nameToIndex_.find(name);
    if (it == nameToIndex_.end()) return nullptr;
    return &textures_[it->second];
}

int TextureLibrary::findIdByName(const std::string& name) const {
    auto it = nameToIndex_.find(name);
    if (it == nameToIndex_.end()) return -1;
    return it->second;
}

} // namespace light3d
