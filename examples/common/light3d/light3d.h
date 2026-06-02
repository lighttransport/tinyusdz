#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace light3d {

// Version information
constexpr int VERSION_MAJOR = 0;
constexpr int VERSION_MINOR = 1;
constexpr int VERSION_PATCH = 0;

// Platform detection
#if defined(__EMSCRIPTEN__)
    #define LIGHT3D_PLATFORM_WASM
#elif defined(_WIN32) || defined(_WIN64)
    #define LIGHT3D_PLATFORM_WINDOWS
#elif defined(__APPLE__)
    #define LIGHT3D_PLATFORM_MACOS
#elif defined(__linux__)
    #define LIGHT3D_PLATFORM_LINUX
#endif

// Rendering backend types
enum class RenderBackend {
    None,
    OpenGL,
    Metal,
    Vulkan,
    WebGL,
    WebGPU
};

// Basic 3D math structures
struct Vec3 {
    float x, y, z;
    
    Vec3() : x(0.0f), y(0.0f), z(0.0f) {}
    Vec3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
};

struct Vec4 {
    float x, y, z, w;
    
    Vec4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
    Vec4(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
};

struct Color {
    float r, g, b, a;
    
    Color() : r(1.0f), g(1.0f), b(1.0f), a(1.0f) {}
    Color(float _r, float _g, float _b, float _a = 1.0f) : r(_r), g(_g), b(_b), a(_a) {}
};

// Forward declarations
class Renderer;
class Scene;
class Camera;
class Geometry;
class Material;
class Mesh;

// Scene graph forward declarations
class Stage;
class Timeline;
class Prim;
class Xform;
class MeshPrim;
class SkeletonPrim;
class SkelAnimationPrim;

// Engine configuration
struct EngineConfig {
    RenderBackend backend = RenderBackend::None;
    uint32_t width = 800;
    uint32_t height = 600;
    bool vsync = true;
    std::string title = "Light3D Application";
};

// Main engine class
class Engine {
public:
    Engine() = default;
    virtual ~Engine() = default;
    
    virtual bool initialize(const EngineConfig& config) = 0;
    virtual void shutdown() = 0;
    virtual bool isRunning() const = 0;
    virtual void update(float deltaTime) = 0;
    virtual void render() = 0;
    
    virtual Renderer* getRenderer() = 0;
    virtual Stage* getStage() = 0;
    virtual Timeline* getTimeline() = 0;

    static std::unique_ptr<Engine> create(RenderBackend backend);
};

// Renderer interface
class Renderer {
public:
    virtual ~Renderer() = default;
    
    virtual bool initialize(uint32_t width, uint32_t height) = 0;
    virtual void shutdown() = 0;
    virtual void resize(uint32_t width, uint32_t height) = 0;
    
    virtual void clear(const Color& color) = 0;
    virtual void present() = 0;
    
    virtual RenderBackend getBackend() const = 0;
};

// Camera class
class Camera {
public:
    Camera() = default;
    virtual ~Camera() = default;
    
    Vec3 position{0.0f, 0.0f, 5.0f};
    Vec3 target{0.0f, 0.0f, 0.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
    
    float fov = 60.0f;
    float aspectRatio = 16.0f / 9.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
};

// Scene class
class Scene {
public:
    Scene() = default;
    virtual ~Scene() = default;
    
    void setBackgroundColor(const Color& color) { backgroundColor = color; }
    const Color& getBackgroundColor() const { return backgroundColor; }
    
private:
    Color backgroundColor{0.1f, 0.1f, 0.1f, 1.0f};
};

// Utility functions
inline std::string getVersionString() {
    return std::to_string(VERSION_MAJOR) + "." + 
           std::to_string(VERSION_MINOR) + "." + 
           std::to_string(VERSION_PATCH);
}

inline const char* getRenderBackendName(RenderBackend backend) {
    switch(backend) {
        case RenderBackend::OpenGL: return "OpenGL";
        case RenderBackend::Metal: return "Metal";
        case RenderBackend::Vulkan: return "Vulkan";
        case RenderBackend::WebGL: return "WebGL";
        case RenderBackend::WebGPU: return "WebGPU";
        default: return "None";
    }
}

} // namespace light3d
