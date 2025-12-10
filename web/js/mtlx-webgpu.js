// TinyUSDZ WebGPU + OpenPBR Demo
// High-performance USD viewer using WebGPU with:
// - Uber shader for OpenPBR materials
// - UBO-based material parameters for efficient multi-material rendering
// - Bindless textures via texture arrays
// - Minimal draw calls through material batching

import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';

// ============================================================================
// WGSL Uber Shader for OpenPBR
// ============================================================================

const OPENPBR_SHADER = /* wgsl */`
// ============================================================================
// Uniform Structures
// ============================================================================

struct CameraUniforms {
    viewProjection: mat4x4<f32>,
    view: mat4x4<f32>,
    projection: mat4x4<f32>,
    cameraPosition: vec3<f32>,
    _pad0: f32,
    invViewProjection: mat4x4<f32>,
};

struct SceneUniforms {
    envIntensity: f32,
    exposure: f32,
    toneMapping: u32, // 0=linear, 1=reinhard, 2=aces, 3=agx
    time: f32,
    resolution: vec2<f32>,
    _pad: vec2<f32>,
};

// OpenPBR Material parameters (144 bytes, aligned to 16)
struct MaterialParams {
    // Base layer (48 bytes)
    baseColor: vec3<f32>,
    baseWeight: f32,
    baseMetalness: f32,
    baseDiffuseRoughness: f32,
    _pad0: vec2<f32>,

    // Specular layer (32 bytes)
    specularColor: vec3<f32>,
    specularWeight: f32,
    specularRoughness: f32,
    specularIOR: f32,
    specularAnisotropy: f32,
    _pad1: f32,

    // Transmission layer (16 bytes)
    transmissionWeight: f32,
    transmissionDepth: f32,
    transmissionDispersion: f32,
    _pad2: f32,

    // Coat layer (32 bytes)
    coatColor: vec3<f32>,
    coatWeight: f32,
    coatRoughness: f32,
    coatIOR: f32,
    _pad3: vec2<f32>,

    // Emission layer (16 bytes)
    emissionColor: vec3<f32>,
    emissionLuminance: f32,

    // Sheen/Fuzz layer (16 bytes)
    sheenColor: vec3<f32>,
    sheenWeight: f32,

    // Texture indices (-1 = no texture) (32 bytes)
    baseColorTexIdx: i32,
    normalTexIdx: i32,
    roughnessTexIdx: i32,
    metalnessTexIdx: i32,
    emissiveTexIdx: i32,
    aoTexIdx: i32,
    _texPad: vec2<i32>,
};

// Per-instance data for batched rendering
struct InstanceData {
    modelMatrix: mat4x4<f32>,
    normalMatrix: mat4x4<f32>,
    materialIndex: u32,
    _pad: vec3<u32>,
};

// ============================================================================
// Bindings
// ============================================================================

@group(0) @binding(0) var<uniform> camera: CameraUniforms;
@group(0) @binding(1) var<uniform> scene: SceneUniforms;

// Material UBO array (supports up to 256 materials)
@group(1) @binding(0) var<storage, read> materials: array<MaterialParams>;

// Instance data storage buffer
@group(1) @binding(1) var<storage, read> instances: array<InstanceData>;

// Texture arrays for bindless textures
@group(2) @binding(0) var textureSampler: sampler;
@group(2) @binding(1) var baseColorTextures: texture_2d_array<f32>;
@group(2) @binding(2) var normalTextures: texture_2d_array<f32>;
@group(2) @binding(3) var pbrTextures: texture_2d_array<f32>; // R=roughness, G=metalness, B=ao

// Environment map
@group(3) @binding(0) var envSampler: sampler;
@group(3) @binding(1) var envMap: texture_cube<f32>;
@group(3) @binding(2) var irradianceMap: texture_cube<f32>;
@group(3) @binding(3) var prefilteredMap: texture_cube<f32>;
@group(3) @binding(4) var brdfLUT: texture_2d<f32>;

// ============================================================================
// Vertex Shader
// ============================================================================

struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) uv: vec2<f32>,
    @location(3) tangent: vec4<f32>,
    @builtin(instance_index) instanceIndex: u32,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) worldPosition: vec3<f32>,
    @location(1) worldNormal: vec3<f32>,
    @location(2) uv: vec2<f32>,
    @location(3) @interpolate(flat) materialIndex: u32,
    @location(4) worldTangent: vec3<f32>,
    @location(5) worldBitangent: vec3<f32>,
};

@vertex
fn vertexMain(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;

    let instance = instances[input.instanceIndex];
    let worldPos = instance.modelMatrix * vec4<f32>(input.position, 1.0);

    output.position = camera.viewProjection * worldPos;
    output.worldPosition = worldPos.xyz;
    output.worldNormal = normalize((instance.normalMatrix * vec4<f32>(input.normal, 0.0)).xyz);
    output.uv = input.uv;
    output.materialIndex = instance.materialIndex;

    // Tangent space
    let worldTangent = normalize((instance.modelMatrix * vec4<f32>(input.tangent.xyz, 0.0)).xyz);
    output.worldTangent = worldTangent;
    output.worldBitangent = cross(output.worldNormal, worldTangent) * input.tangent.w;

    return output;
}

// ============================================================================
// PBR Helper Functions
// ============================================================================

const PI: f32 = 3.14159265359;
const INV_PI: f32 = 0.31830988618;

// Fresnel-Schlick approximation
fn fresnelSchlick(cosTheta: f32, F0: vec3<f32>) -> vec3<f32> {
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

fn fresnelSchlickRoughness(cosTheta: f32, F0: vec3<f32>, roughness: f32) -> vec3<f32> {
    return F0 + (max(vec3<f32>(1.0 - roughness), F0) - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// GGX Normal Distribution Function
fn distributionGGX(NdotH: f32, roughness: f32) -> f32 {
    let a = roughness * roughness;
    let a2 = a * a;
    let denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// Smith's geometry function for GGX
fn geometrySchlickGGX(NdotV: f32, roughness: f32) -> f32 {
    let r = roughness + 1.0;
    let k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

fn geometrySmith(NdotV: f32, NdotL: f32, roughness: f32) -> f32 {
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

// Oren-Nayar diffuse for diffuse roughness
fn orenNayarDiffuse(NdotL: f32, NdotV: f32, LdotV: f32, roughness: f32) -> f32 {
    let sigma2 = roughness * roughness;
    let A = 1.0 - 0.5 * sigma2 / (sigma2 + 0.33);
    let B = 0.45 * sigma2 / (sigma2 + 0.09);

    let cosPhiDiff = LdotV - NdotL * NdotV;
    let sinNL = sqrt(saturate(1.0 - NdotL * NdotL));
    let sinNV = sqrt(saturate(1.0 - NdotV * NdotV));
    let s = max(sinNL, sinNV);
    let t = select(0.0, min(sinNL, sinNV) / max(NdotL, NdotV), s > 0.0);

    return A + B * max(0.0, cosPhiDiff) * s * t;
}

// Sample environment map with roughness-based mip level
fn sampleEnvMap(R: vec3<f32>, roughness: f32) -> vec3<f32> {
    // Use prefiltered environment map if available
    let mipLevel = roughness * 7.0; // Assume 8 mip levels
    return textureSampleLevel(prefilteredMap, envSampler, R, mipLevel).rgb;
}

// Sample irradiance map for diffuse IBL
fn sampleIrradiance(N: vec3<f32>) -> vec3<f32> {
    return textureSample(irradianceMap, envSampler, N).rgb;
}

// ============================================================================
// Tone Mapping
// ============================================================================

fn linearToneMap(color: vec3<f32>) -> vec3<f32> {
    return saturate(color);
}

fn reinhardToneMap(color: vec3<f32>) -> vec3<f32> {
    return color / (color + vec3<f32>(1.0));
}

fn acesToneMap(color: vec3<f32>) -> vec3<f32> {
    let a = 2.51;
    let b = 0.03;
    let c = 2.43;
    let d = 0.59;
    let e = 0.14;
    return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}

fn agxToneMap(color: vec3<f32>) -> vec3<f32> {
    // Simplified AgX tonemapping
    let agxMatrix = mat3x3<f32>(
        vec3<f32>(0.842479, 0.0423303, 0.0423765),
        vec3<f32>(0.0784336, 0.878469, 0.0784336),
        vec3<f32>(0.0792227, 0.0791407, 0.879142)
    );
    var result = agxMatrix * color;
    result = max(result, vec3<f32>(0.0));
    result = pow(result, vec3<f32>(1.0 / 2.2));
    return saturate(result);
}

fn applyToneMapping(color: vec3<f32>, mode: u32) -> vec3<f32> {
    switch(mode) {
        case 0u: { return linearToneMap(color); }
        case 1u: { return reinhardToneMap(color); }
        case 2u: { return acesToneMap(color); }
        case 3u: { return agxToneMap(color); }
        default: { return acesToneMap(color); }
    }
}

// sRGB gamma correction
fn linearToSRGB(color: vec3<f32>) -> vec3<f32> {
    let cutoff = color < vec3<f32>(0.0031308);
    let higher = vec3<f32>(1.055) * pow(color, vec3<f32>(1.0/2.4)) - vec3<f32>(0.055);
    let lower = color * vec3<f32>(12.92);
    return select(higher, lower, cutoff);
}

// ============================================================================
// Fragment Shader
// ============================================================================

@fragment
fn fragmentMain(input: VertexOutput) -> @location(0) vec4<f32> {
    let mat = materials[input.materialIndex];

    // Sample textures if available
    var baseColor = mat.baseColor;
    if (mat.baseColorTexIdx >= 0) {
        let texColor = textureSample(baseColorTextures, textureSampler, input.uv, mat.baseColorTexIdx);
        // Texture is in sRGB, convert to linear
        baseColor = pow(texColor.rgb, vec3<f32>(2.2)) * mat.baseColor;
    }

    var roughness = mat.specularRoughness;
    var metalness = mat.baseMetalness;
    var ao = 1.0;
    if (mat.roughnessTexIdx >= 0) {
        let pbrSample = textureSample(pbrTextures, textureSampler, input.uv, mat.roughnessTexIdx);
        roughness = pbrSample.r * mat.specularRoughness;
        metalness = pbrSample.g * mat.baseMetalness;
        ao = pbrSample.b;
    }

    // Normal mapping
    var N = normalize(input.worldNormal);
    if (mat.normalTexIdx >= 0) {
        let tangentNormal = textureSample(normalTextures, textureSampler, input.uv, mat.normalTexIdx).rgb * 2.0 - 1.0;
        let TBN = mat3x3<f32>(
            normalize(input.worldTangent),
            normalize(input.worldBitangent),
            N
        );
        N = normalize(TBN * tangentNormal);
    }

    // View and reflection vectors
    let V = normalize(camera.cameraPosition - input.worldPosition);
    let R = reflect(-V, N);

    let NdotV = max(dot(N, V), 0.001);

    // Calculate F0 (reflectance at normal incidence)
    let dielectricF0 = vec3<f32>(pow((mat.specularIOR - 1.0) / (mat.specularIOR + 1.0), 2.0));
    let F0 = mix(dielectricF0 * mat.specularColor, baseColor, metalness);

    // ========== IBL Lighting ==========

    // Diffuse IBL
    let irradiance = sampleIrradiance(N) * scene.envIntensity;
    let diffuseIBL = irradiance * baseColor * (1.0 - metalness) * mat.baseWeight;

    // Oren-Nayar diffuse roughness factor (simplified for IBL)
    let diffuseRoughnessFactor = 1.0 - mat.baseDiffuseRoughness * 0.5;

    // Specular IBL
    let prefilteredColor = sampleEnvMap(R, roughness) * scene.envIntensity;
    let brdf = textureSample(brdfLUT, textureSampler, vec2<f32>(NdotV, roughness)).rg;
    let F = fresnelSchlickRoughness(NdotV, F0, roughness);
    let specularIBL = prefilteredColor * (F * brdf.x + brdf.y) * mat.specularWeight;

    // ========== Coat Layer ==========
    var coatContrib = vec3<f32>(0.0);
    if (mat.coatWeight > 0.0) {
        let coatF0 = vec3<f32>(pow((mat.coatIOR - 1.0) / (mat.coatIOR + 1.0), 2.0));
        let coatF = fresnelSchlickRoughness(NdotV, coatF0, mat.coatRoughness);
        let coatSpecular = sampleEnvMap(R, mat.coatRoughness) * coatF;
        coatContrib = coatSpecular * mat.coatColor * mat.coatWeight * scene.envIntensity;
    }

    // ========== Sheen/Fuzz Layer ==========
    var sheenContrib = vec3<f32>(0.0);
    if (mat.sheenWeight > 0.0) {
        // Simple sheen approximation
        let sheenFresnel = pow(1.0 - NdotV, 5.0);
        sheenContrib = mat.sheenColor * sheenFresnel * mat.sheenWeight * irradiance;
    }

    // ========== Emission ==========
    let emission = mat.emissionColor * mat.emissionLuminance;

    // ========== Combine ==========
    var finalColor = (diffuseIBL * diffuseRoughnessFactor + specularIBL) * ao;
    finalColor += coatContrib;
    finalColor += sheenContrib;
    finalColor += emission;

    // ========== Transmission (simple approximation) ==========
    if (mat.transmissionWeight > 0.0) {
        // Simple refraction approximation
        let transmissionColor = sampleEnvMap(-V, roughness) * baseColor;
        finalColor = mix(finalColor, transmissionColor, mat.transmissionWeight * (1.0 - metalness));
    }

    // Apply exposure
    finalColor *= scene.exposure;

    // Tone mapping
    finalColor = applyToneMapping(finalColor, scene.toneMapping);

    // Gamma correction (linear to sRGB)
    finalColor = linearToSRGB(finalColor);

    return vec4<f32>(finalColor, 1.0);
}
`;

// Fallback simple shader when env maps are not loaded
const SIMPLE_SHADER = /* wgsl */`
struct CameraUniforms {
    viewProjection: mat4x4<f32>,
    view: mat4x4<f32>,
    projection: mat4x4<f32>,
    cameraPosition: vec3<f32>,
    _pad0: f32,
    invViewProjection: mat4x4<f32>,
};

struct SceneUniforms {
    envIntensity: f32,
    exposure: f32,
    toneMapping: u32,
    time: f32,
    resolution: vec2<f32>,
    _pad: vec2<f32>,
};

struct MaterialParams {
    baseColor: vec3<f32>,
    baseWeight: f32,
    baseMetalness: f32,
    baseDiffuseRoughness: f32,
    _pad0: vec2<f32>,
    specularColor: vec3<f32>,
    specularWeight: f32,
    specularRoughness: f32,
    specularIOR: f32,
    specularAnisotropy: f32,
    _pad1: f32,
    transmissionWeight: f32,
    transmissionDepth: f32,
    transmissionDispersion: f32,
    _pad2: f32,
    coatColor: vec3<f32>,
    coatWeight: f32,
    coatRoughness: f32,
    coatIOR: f32,
    _pad3: vec2<f32>,
    emissionColor: vec3<f32>,
    emissionLuminance: f32,
    sheenColor: vec3<f32>,
    sheenWeight: f32,
    baseColorTexIdx: i32,
    normalTexIdx: i32,
    roughnessTexIdx: i32,
    metalnessTexIdx: i32,
    emissiveTexIdx: i32,
    aoTexIdx: i32,
    _texPad: vec2<i32>,
};

struct InstanceData {
    modelMatrix: mat4x4<f32>,
    normalMatrix: mat4x4<f32>,
    materialIndex: u32,
    _pad: vec3<u32>,
};

@group(0) @binding(0) var<uniform> camera: CameraUniforms;
@group(0) @binding(1) var<uniform> scene: SceneUniforms;
@group(1) @binding(0) var<storage, read> materials: array<MaterialParams>;
@group(1) @binding(1) var<storage, read> instances: array<InstanceData>;

struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) uv: vec2<f32>,
    @location(3) tangent: vec4<f32>,
    @builtin(instance_index) instanceIndex: u32,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) worldPosition: vec3<f32>,
    @location(1) worldNormal: vec3<f32>,
    @location(2) uv: vec2<f32>,
    @location(3) @interpolate(flat) materialIndex: u32,
};

@vertex
fn vertexMain(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    let instance = instances[input.instanceIndex];
    let worldPos = instance.modelMatrix * vec4<f32>(input.position, 1.0);
    output.position = camera.viewProjection * worldPos;
    output.worldPosition = worldPos.xyz;
    output.worldNormal = normalize((instance.normalMatrix * vec4<f32>(input.normal, 0.0)).xyz);
    output.uv = input.uv;
    output.materialIndex = instance.materialIndex;
    return output;
}

const PI: f32 = 3.14159265359;

fn fresnelSchlick(cosTheta: f32, F0: vec3<f32>) -> vec3<f32> {
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

@fragment
fn fragmentMain(input: VertexOutput) -> @location(0) vec4<f32> {
    let mat = materials[input.materialIndex];
    let baseColor = mat.baseColor;
    let roughness = mat.specularRoughness;
    let metalness = mat.baseMetalness;

    let N = normalize(input.worldNormal);
    let V = normalize(camera.cameraPosition - input.worldPosition);
    let NdotV = max(dot(N, V), 0.001);

    // Simple directional light
    let lightDir = normalize(vec3<f32>(1.0, 1.0, 1.0));
    let NdotL = max(dot(N, lightDir), 0.0);
    let H = normalize(V + lightDir);
    let NdotH = max(dot(N, H), 0.0);

    let F0 = mix(vec3<f32>(0.04), baseColor, metalness);
    let F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    // Diffuse
    let diffuse = baseColor * (1.0 - metalness) * NdotL * INV_PI;

    // Specular (simple)
    let specPower = (1.0 - roughness) * 64.0 + 2.0;
    let specular = F * pow(NdotH, specPower) * NdotL;

    // Ambient
    let ambient = baseColor * 0.03;

    // Emission
    let emission = mat.emissionColor * mat.emissionLuminance;

    var finalColor = ambient + diffuse + specular + emission;
    finalColor *= scene.exposure;

    // Gamma correction
    finalColor = pow(finalColor, vec3<f32>(1.0/2.2));

    return vec4<f32>(finalColor, 1.0);
}

const INV_PI: f32 = 0.31830988618;
`;

// ============================================================================
// WebGPU Renderer Class
// ============================================================================

class WebGPURenderer {
    constructor(canvas) {
        this.canvas = canvas;
        this.device = null;
        this.context = null;
        this.format = null;

        // Pipelines
        this.pipeline = null;
        this.simplePipeline = null;

        // Buffers
        this.cameraBuffer = null;
        this.sceneBuffer = null;
        this.materialBuffer = null;
        this.instanceBuffer = null;

        // Bind groups
        this.cameraBindGroup = null;
        this.materialBindGroup = null;
        this.textureBindGroup = null;
        this.envBindGroup = null;

        // Texture arrays
        this.baseColorTextureArray = null;
        this.normalTextureArray = null;
        this.pbrTextureArray = null;
        this.textureArraySize = 0;

        // Environment maps
        this.envMap = null;
        this.irradianceMap = null;
        this.prefilteredMap = null;
        this.brdfLUT = null;
        this.envLoaded = false;

        // Depth buffer
        this.depthTexture = null;

        // Scene data
        this.meshes = [];
        this.materials = [];
        this.instances = [];

        // Camera
        this.camera = {
            position: [3, 2, 5],
            target: [0, 0, 0],
            up: [0, 1, 0],
            fov: 45,
            near: 0.1,
            far: 1000
        };

        // Scene settings
        this.settings = {
            envIntensity: 1.0,
            exposure: 1.0,
            toneMapping: 2, // ACES
            showWireframe: false,
            showNormals: false
        };

        // Stats
        this.stats = {
            fps: 0,
            gpuTime: 0,
            uboUpdates: 0,
            drawCalls: 0,
            triangles: 0
        };

        this.lastFrameTime = performance.now();
        this.frameCount = 0;
    }

    async init() {
        if (!navigator.gpu) {
            throw new Error('WebGPU not supported');
        }

        const adapter = await navigator.gpu.requestAdapter({
            powerPreference: 'high-performance'
        });

        if (!adapter) {
            throw new Error('No WebGPU adapter found');
        }

        this.device = await adapter.requestDevice({
            requiredFeatures: [],
            requiredLimits: {
                maxStorageBufferBindingSize: 256 * 1024 * 1024, // 256MB for large scenes
                maxBufferSize: 256 * 1024 * 1024
            }
        });

        this.context = this.canvas.getContext('webgpu');
        this.format = navigator.gpu.getPreferredCanvasFormat();

        this.context.configure({
            device: this.device,
            format: this.format,
            alphaMode: 'premultiplied'
        });

        await this.createBuffers();
        await this.createPipelines();
        this.createDepthBuffer();

        console.log('WebGPU initialized');
    }

    async createBuffers() {
        // Camera uniform buffer (256 bytes aligned)
        this.cameraBuffer = this.device.createBuffer({
            size: 256,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
        });

        // Scene uniform buffer
        this.sceneBuffer = this.device.createBuffer({
            size: 32,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
        });

        // Material storage buffer (supports up to 256 materials, 192 bytes each)
        this.materialBuffer = this.device.createBuffer({
            size: 256 * 192,
            usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST
        });

        // Instance storage buffer (supports up to 65536 instances)
        this.instanceBuffer = this.device.createBuffer({
            size: 65536 * 144, // 144 bytes per instance
            usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST
        });

        // Create placeholder textures for bind groups
        await this.createPlaceholderTextures();
    }

    async createPlaceholderTextures() {
        // Create 1x1 placeholder textures
        const placeholderData = new Uint8Array([255, 255, 255, 255]);

        // Placeholder texture array (single layer)
        this.baseColorTextureArray = this.device.createTexture({
            size: [1, 1, 1],
            format: 'rgba8unorm',
            usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST
        });

        this.normalTextureArray = this.device.createTexture({
            size: [1, 1, 1],
            format: 'rgba8unorm',
            usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST
        });

        this.pbrTextureArray = this.device.createTexture({
            size: [1, 1, 1],
            format: 'rgba8unorm',
            usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST
        });

        // Placeholder cube map
        this.envMap = this.device.createTexture({
            size: [1, 1, 6],
            format: 'rgba8unorm',
            usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
            dimension: '2d'
        });

        this.irradianceMap = this.device.createTexture({
            size: [1, 1, 6],
            format: 'rgba8unorm',
            usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
            dimension: '2d'
        });

        this.prefilteredMap = this.device.createTexture({
            size: [1, 1, 6],
            format: 'rgba8unorm',
            usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
            dimension: '2d'
        });

        // Placeholder BRDF LUT
        this.brdfLUT = this.device.createTexture({
            size: [1, 1],
            format: 'rgba8unorm',
            usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST
        });

        // Create sampler
        this.textureSampler = this.device.createSampler({
            magFilter: 'linear',
            minFilter: 'linear',
            mipmapFilter: 'linear',
            addressModeU: 'repeat',
            addressModeV: 'repeat'
        });

        this.envSampler = this.device.createSampler({
            magFilter: 'linear',
            minFilter: 'linear',
            mipmapFilter: 'linear',
            addressModeU: 'clamp-to-edge',
            addressModeV: 'clamp-to-edge'
        });
    }

    async createPipelines() {
        // Create bind group layouts
        const cameraBindGroupLayout = this.device.createBindGroupLayout({
            entries: [
                { binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: 'uniform' } },
                { binding: 1, visibility: GPUShaderStage.FRAGMENT, buffer: { type: 'uniform' } }
            ]
        });

        const materialBindGroupLayout = this.device.createBindGroupLayout({
            entries: [
                { binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: 'read-only-storage' } },
                { binding: 1, visibility: GPUShaderStage.VERTEX, buffer: { type: 'read-only-storage' } }
            ]
        });

        // Simplified texture bind group (textures without cube maps)
        const textureBindGroupLayout = this.device.createBindGroupLayout({
            entries: [
                { binding: 0, visibility: GPUShaderStage.FRAGMENT, sampler: { type: 'filtering' } },
                { binding: 1, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: 'float', viewDimension: '2d-array' } },
                { binding: 2, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: 'float', viewDimension: '2d-array' } },
                { binding: 3, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: 'float', viewDimension: '2d-array' } }
            ]
        });

        // Pipeline layout for simple shader
        const simplePipelineLayout = this.device.createPipelineLayout({
            bindGroupLayouts: [cameraBindGroupLayout, materialBindGroupLayout]
        });

        // Create simple pipeline (without IBL)
        const simpleShaderModule = this.device.createShaderModule({
            code: SIMPLE_SHADER
        });

        this.simplePipeline = this.device.createRenderPipeline({
            layout: simplePipelineLayout,
            vertex: {
                module: simpleShaderModule,
                entryPoint: 'vertexMain',
                buffers: [{
                    arrayStride: 48, // position(12) + normal(12) + uv(8) + tangent(16)
                    attributes: [
                        { shaderLocation: 0, offset: 0, format: 'float32x3' },  // position
                        { shaderLocation: 1, offset: 12, format: 'float32x3' }, // normal
                        { shaderLocation: 2, offset: 24, format: 'float32x2' }, // uv
                        { shaderLocation: 3, offset: 32, format: 'float32x4' }  // tangent
                    ]
                }]
            },
            fragment: {
                module: simpleShaderModule,
                entryPoint: 'fragmentMain',
                targets: [{ format: this.format }]
            },
            primitive: {
                topology: 'triangle-list',
                cullMode: 'back'
            },
            depthStencil: {
                depthWriteEnabled: true,
                depthCompare: 'less',
                format: 'depth24plus'
            }
        });

        // Create bind groups
        this.cameraBindGroup = this.device.createBindGroup({
            layout: cameraBindGroupLayout,
            entries: [
                { binding: 0, resource: { buffer: this.cameraBuffer } },
                { binding: 1, resource: { buffer: this.sceneBuffer } }
            ]
        });

        this.materialBindGroup = this.device.createBindGroup({
            layout: materialBindGroupLayout,
            entries: [
                { binding: 0, resource: { buffer: this.materialBuffer } },
                { binding: 1, resource: { buffer: this.instanceBuffer } }
            ]
        });
    }

    createDepthBuffer() {
        if (this.depthTexture) {
            this.depthTexture.destroy();
        }

        this.depthTexture = this.device.createTexture({
            size: [this.canvas.width, this.canvas.height],
            format: 'depth24plus',
            usage: GPUTextureUsage.RENDER_ATTACHMENT
        });
    }

    resize(width, height) {
        this.canvas.width = width;
        this.canvas.height = height;
        this.createDepthBuffer();
    }

    updateCameraUniforms() {
        const aspect = this.canvas.width / this.canvas.height;
        const fovRad = this.camera.fov * Math.PI / 180;

        // Create matrices
        const view = mat4LookAt(this.camera.position, this.camera.target, this.camera.up);
        const projection = mat4Perspective(fovRad, aspect, this.camera.near, this.camera.far);
        const viewProjection = mat4Multiply(projection, view);
        const invViewProjection = mat4Inverse(viewProjection);

        // Pack into buffer (256 bytes)
        const data = new Float32Array(64);
        data.set(viewProjection, 0);      // viewProjection: 64 bytes
        data.set(view, 16);               // view: 64 bytes
        data.set(projection, 32);         // projection: 64 bytes
        data.set(this.camera.position, 48); // cameraPosition: 12 bytes + padding
        data.set(invViewProjection, 52);  // invViewProjection: 64 bytes

        this.device.queue.writeBuffer(this.cameraBuffer, 0, data);
    }

    updateSceneUniforms() {
        const data = new Float32Array(8);
        data[0] = this.settings.envIntensity;
        data[1] = this.settings.exposure;
        data[2] = this.settings.toneMapping;
        data[3] = performance.now() / 1000;
        data[4] = this.canvas.width;
        data[5] = this.canvas.height;

        this.device.queue.writeBuffer(this.sceneBuffer, 0, data);
        this.stats.uboUpdates++;
    }

    updateMaterials(materials) {
        this.materials = materials;

        // Each material is 192 bytes (aligned)
        const data = new Float32Array(materials.length * 48);

        for (let i = 0; i < materials.length; i++) {
            const mat = materials[i];
            const offset = i * 48;

            // Base layer
            data[offset + 0] = mat.baseColor?.[0] ?? 0.8;
            data[offset + 1] = mat.baseColor?.[1] ?? 0.8;
            data[offset + 2] = mat.baseColor?.[2] ?? 0.8;
            data[offset + 3] = mat.baseWeight ?? 1.0;
            data[offset + 4] = mat.baseMetalness ?? 0.0;
            data[offset + 5] = mat.baseDiffuseRoughness ?? 0.0;

            // Specular layer
            data[offset + 8] = mat.specularColor?.[0] ?? 1.0;
            data[offset + 9] = mat.specularColor?.[1] ?? 1.0;
            data[offset + 10] = mat.specularColor?.[2] ?? 1.0;
            data[offset + 11] = mat.specularWeight ?? 1.0;
            data[offset + 12] = mat.specularRoughness ?? 0.5;
            data[offset + 13] = mat.specularIOR ?? 1.5;
            data[offset + 14] = mat.specularAnisotropy ?? 0.0;

            // Transmission layer
            data[offset + 16] = mat.transmissionWeight ?? 0.0;
            data[offset + 17] = mat.transmissionDepth ?? 0.0;
            data[offset + 18] = mat.transmissionDispersion ?? 0.0;

            // Coat layer
            data[offset + 20] = mat.coatColor?.[0] ?? 1.0;
            data[offset + 21] = mat.coatColor?.[1] ?? 1.0;
            data[offset + 22] = mat.coatColor?.[2] ?? 1.0;
            data[offset + 23] = mat.coatWeight ?? 0.0;
            data[offset + 24] = mat.coatRoughness ?? 0.1;
            data[offset + 25] = mat.coatIOR ?? 1.5;

            // Emission layer
            data[offset + 28] = mat.emissionColor?.[0] ?? 0.0;
            data[offset + 29] = mat.emissionColor?.[1] ?? 0.0;
            data[offset + 30] = mat.emissionColor?.[2] ?? 0.0;
            data[offset + 31] = mat.emissionLuminance ?? 0.0;

            // Sheen layer
            data[offset + 32] = mat.sheenColor?.[0] ?? 1.0;
            data[offset + 33] = mat.sheenColor?.[1] ?? 1.0;
            data[offset + 34] = mat.sheenColor?.[2] ?? 1.0;
            data[offset + 35] = mat.sheenWeight ?? 0.0;

            // Texture indices (as int32, reinterpret as float32 for buffer)
            const intView = new Int32Array(data.buffer, (offset + 36) * 4, 8);
            intView[0] = mat.baseColorTexIdx ?? -1;
            intView[1] = mat.normalTexIdx ?? -1;
            intView[2] = mat.roughnessTexIdx ?? -1;
            intView[3] = mat.metalnessTexIdx ?? -1;
            intView[4] = mat.emissiveTexIdx ?? -1;
            intView[5] = mat.aoTexIdx ?? -1;
        }

        this.device.queue.writeBuffer(this.materialBuffer, 0, data);
        this.stats.uboUpdates++;
    }

    updateInstances(instances) {
        this.instances = instances;

        // Each instance is 144 bytes (modelMatrix: 64, normalMatrix: 64, materialIndex: 4, padding: 12)
        const data = new Float32Array(instances.length * 36);
        const intView = new Uint32Array(data.buffer);

        for (let i = 0; i < instances.length; i++) {
            const inst = instances[i];
            const offset = i * 36;

            // Model matrix
            data.set(inst.modelMatrix || mat4Identity(), offset);

            // Normal matrix (inverse transpose of model matrix)
            const normalMatrix = inst.normalMatrix || mat4Identity();
            data.set(normalMatrix, offset + 16);

            // Material index
            intView[offset + 32] = inst.materialIndex ?? 0;
        }

        this.device.queue.writeBuffer(this.instanceBuffer, 0, data);
        this.stats.uboUpdates++;
    }

    createMeshBuffers(meshData) {
        const { positions, normals, uvs, tangents, indices } = meshData;

        // Interleave vertex data: position(3) + normal(3) + uv(2) + tangent(4) = 12 floats = 48 bytes
        const vertexCount = positions.length / 3;
        const vertexData = new Float32Array(vertexCount * 12);

        for (let i = 0; i < vertexCount; i++) {
            const vOffset = i * 12;
            const pOffset = i * 3;
            const uvOffset = i * 2;
            const tOffset = i * 4;

            // Position
            vertexData[vOffset + 0] = positions[pOffset + 0];
            vertexData[vOffset + 1] = positions[pOffset + 1];
            vertexData[vOffset + 2] = positions[pOffset + 2];

            // Normal
            vertexData[vOffset + 3] = normals?.[pOffset + 0] ?? 0;
            vertexData[vOffset + 4] = normals?.[pOffset + 1] ?? 1;
            vertexData[vOffset + 5] = normals?.[pOffset + 2] ?? 0;

            // UV
            vertexData[vOffset + 6] = uvs?.[uvOffset + 0] ?? 0;
            vertexData[vOffset + 7] = uvs?.[uvOffset + 1] ?? 0;

            // Tangent
            vertexData[vOffset + 8] = tangents?.[tOffset + 0] ?? 1;
            vertexData[vOffset + 9] = tangents?.[tOffset + 1] ?? 0;
            vertexData[vOffset + 10] = tangents?.[tOffset + 2] ?? 0;
            vertexData[vOffset + 11] = tangents?.[tOffset + 3] ?? 1;
        }

        const vertexBuffer = this.device.createBuffer({
            size: vertexData.byteLength,
            usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
            mappedAtCreation: true
        });
        new Float32Array(vertexBuffer.getMappedRange()).set(vertexData);
        vertexBuffer.unmap();

        let indexBuffer = null;
        let indexCount = 0;

        if (indices && indices.length > 0) {
            indexCount = indices.length;
            indexBuffer = this.device.createBuffer({
                size: indices.byteLength,
                usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST,
                mappedAtCreation: true
            });
            new Uint32Array(indexBuffer.getMappedRange()).set(indices);
            indexBuffer.unmap();
        }

        return {
            vertexBuffer,
            indexBuffer,
            vertexCount,
            indexCount
        };
    }

    render() {
        const now = performance.now();
        this.frameCount++;

        if (now - this.lastFrameTime >= 1000) {
            this.stats.fps = Math.round(this.frameCount * 1000 / (now - this.lastFrameTime));
            this.frameCount = 0;
            this.lastFrameTime = now;
        }

        this.stats.uboUpdates = 0;
        this.stats.drawCalls = 0;
        this.stats.triangles = 0;

        this.updateCameraUniforms();
        this.updateSceneUniforms();

        const commandEncoder = this.device.createCommandEncoder();

        const renderPass = commandEncoder.beginRenderPass({
            colorAttachments: [{
                view: this.context.getCurrentTexture().createView(),
                clearValue: { r: 0.1, g: 0.1, b: 0.1, a: 1 },
                loadOp: 'clear',
                storeOp: 'store'
            }],
            depthStencilAttachment: {
                view: this.depthTexture.createView(),
                depthClearValue: 1.0,
                depthLoadOp: 'clear',
                depthStoreOp: 'store'
            }
        });

        renderPass.setPipeline(this.simplePipeline);
        renderPass.setBindGroup(0, this.cameraBindGroup);
        renderPass.setBindGroup(1, this.materialBindGroup);

        // Draw all meshes
        for (const mesh of this.meshes) {
            renderPass.setVertexBuffer(0, mesh.vertexBuffer);

            if (mesh.indexBuffer) {
                renderPass.setIndexBuffer(mesh.indexBuffer, 'uint32');
                renderPass.drawIndexed(mesh.indexCount, mesh.instanceCount || 1, 0, 0, mesh.firstInstance || 0);
                this.stats.triangles += mesh.indexCount / 3;
            } else {
                renderPass.draw(mesh.vertexCount, mesh.instanceCount || 1, 0, mesh.firstInstance || 0);
                this.stats.triangles += mesh.vertexCount / 3;
            }
            this.stats.drawCalls++;
        }

        renderPass.end();
        this.device.queue.submit([commandEncoder.finish()]);
    }

    destroy() {
        this.cameraBuffer?.destroy();
        this.sceneBuffer?.destroy();
        this.materialBuffer?.destroy();
        this.instanceBuffer?.destroy();
        this.depthTexture?.destroy();

        for (const mesh of this.meshes) {
            mesh.vertexBuffer?.destroy();
            mesh.indexBuffer?.destroy();
        }

        this.meshes = [];
    }
}

// ============================================================================
// Matrix Utility Functions
// ============================================================================

function mat4Identity() {
    return new Float32Array([
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    ]);
}

function mat4Perspective(fov, aspect, near, far) {
    const f = 1.0 / Math.tan(fov / 2);
    const nf = 1 / (near - far);

    return new Float32Array([
        f / aspect, 0, 0, 0,
        0, f, 0, 0,
        0, 0, (far + near) * nf, -1,
        0, 0, 2 * far * near * nf, 0
    ]);
}

function mat4LookAt(eye, target, up) {
    const zAxis = normalize3(subtract3(eye, target));
    const xAxis = normalize3(cross3(up, zAxis));
    const yAxis = cross3(zAxis, xAxis);

    return new Float32Array([
        xAxis[0], yAxis[0], zAxis[0], 0,
        xAxis[1], yAxis[1], zAxis[1], 0,
        xAxis[2], yAxis[2], zAxis[2], 0,
        -dot3(xAxis, eye), -dot3(yAxis, eye), -dot3(zAxis, eye), 1
    ]);
}

function mat4Multiply(a, b) {
    const result = new Float32Array(16);
    for (let i = 0; i < 4; i++) {
        for (let j = 0; j < 4; j++) {
            result[j * 4 + i] =
                a[i] * b[j * 4] +
                a[i + 4] * b[j * 4 + 1] +
                a[i + 8] * b[j * 4 + 2] +
                a[i + 12] * b[j * 4 + 3];
        }
    }
    return result;
}

function mat4Inverse(m) {
    const result = new Float32Array(16);
    const inv = new Float32Array(16);

    inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
    inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];

    let det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (Math.abs(det) < 1e-10) {
        return mat4Identity();
    }

    det = 1.0 / det;
    for (let i = 0; i < 16; i++) {
        result[i] = inv[i] * det;
    }

    return result;
}

function mat4Translation(x, y, z) {
    return new Float32Array([
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        x, y, z, 1
    ]);
}

function mat4Scale(x, y, z) {
    return new Float32Array([
        x, 0, 0, 0,
        0, y, 0, 0,
        0, 0, z, 0,
        0, 0, 0, 1
    ]);
}

function mat4RotationY(angle) {
    const c = Math.cos(angle);
    const s = Math.sin(angle);
    return new Float32Array([
        c, 0, -s, 0,
        0, 1, 0, 0,
        s, 0, c, 0,
        0, 0, 0, 1
    ]);
}

function normalize3(v) {
    const len = Math.sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (len < 1e-10) return [0, 0, 0];
    return [v[0]/len, v[1]/len, v[2]/len];
}

function subtract3(a, b) {
    return [a[0]-b[0], a[1]-b[1], a[2]-b[2]];
}

function cross3(a, b) {
    return [
        a[1]*b[2] - a[2]*b[1],
        a[2]*b[0] - a[0]*b[2],
        a[0]*b[1] - a[1]*b[0]
    ];
}

function dot3(a, b) {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

function length3(v) {
    return Math.sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
}

// ============================================================================
// Orbit Camera Controls
// ============================================================================

class OrbitControls {
    constructor(camera, canvas) {
        this.camera = camera;
        this.canvas = canvas;

        this.spherical = { radius: 5, phi: Math.PI / 4, theta: Math.PI / 4 };
        this.target = [0, 0, 0];

        this.rotateSpeed = 0.005;
        this.zoomSpeed = 0.001;
        this.panSpeed = 0.005;

        this.isDragging = false;
        this.isPanning = false;
        this.lastMouse = { x: 0, y: 0 };

        this.setupEventListeners();
        this.update();
    }

    setupEventListeners() {
        this.canvas.addEventListener('mousedown', this.onMouseDown.bind(this));
        this.canvas.addEventListener('mousemove', this.onMouseMove.bind(this));
        this.canvas.addEventListener('mouseup', this.onMouseUp.bind(this));
        this.canvas.addEventListener('wheel', this.onWheel.bind(this));
        this.canvas.addEventListener('contextmenu', e => e.preventDefault());
    }

    onMouseDown(e) {
        if (e.button === 0) {
            this.isDragging = true;
        } else if (e.button === 2) {
            this.isPanning = true;
        }
        this.lastMouse = { x: e.clientX, y: e.clientY };
    }

    onMouseMove(e) {
        const dx = e.clientX - this.lastMouse.x;
        const dy = e.clientY - this.lastMouse.y;
        this.lastMouse = { x: e.clientX, y: e.clientY };

        if (this.isDragging) {
            this.spherical.theta -= dx * this.rotateSpeed;
            this.spherical.phi -= dy * this.rotateSpeed;
            this.spherical.phi = Math.max(0.01, Math.min(Math.PI - 0.01, this.spherical.phi));
            this.update();
        } else if (this.isPanning) {
            const right = normalize3(cross3(subtract3(this.camera.position, this.target), this.camera.up));
            const up = this.camera.up;

            const panX = dx * this.panSpeed * this.spherical.radius;
            const panY = dy * this.panSpeed * this.spherical.radius;

            this.target[0] -= right[0] * panX - up[0] * panY;
            this.target[1] -= right[1] * panX - up[1] * panY;
            this.target[2] -= right[2] * panX - up[2] * panY;
            this.update();
        }
    }

    onMouseUp() {
        this.isDragging = false;
        this.isPanning = false;
    }

    onWheel(e) {
        e.preventDefault();
        this.spherical.radius *= 1 + e.deltaY * this.zoomSpeed;
        this.spherical.radius = Math.max(0.1, Math.min(1000, this.spherical.radius));
        this.update();
    }

    update() {
        const sinPhi = Math.sin(this.spherical.phi);
        const cosPhi = Math.cos(this.spherical.phi);
        const sinTheta = Math.sin(this.spherical.theta);
        const cosTheta = Math.cos(this.spherical.theta);

        this.camera.position = [
            this.target[0] + this.spherical.radius * sinPhi * cosTheta,
            this.target[1] + this.spherical.radius * cosPhi,
            this.target[2] + this.spherical.radius * sinPhi * sinTheta
        ];

        this.camera.target = this.target;
    }

    fitToBox(min, max) {
        const center = [
            (min[0] + max[0]) / 2,
            (min[1] + max[1]) / 2,
            (min[2] + max[2]) / 2
        ];

        const size = [
            max[0] - min[0],
            max[1] - min[1],
            max[2] - min[2]
        ];

        const maxDim = Math.max(size[0], size[1], size[2]);
        this.target = center;
        this.spherical.radius = maxDim * 2;
        this.update();
    }
}

// ============================================================================
// Application
// ============================================================================

class Application {
    constructor() {
        this.canvas = document.getElementById('webgpu-canvas');
        this.renderer = null;
        this.controls = null;
        this.loader = null;
        this.nativeLoader = null;
        this.animationId = null;

        this.settings = {
            envMapPreset: 'goegap_1k',
            envIntensity: 1.0,
            exposure: 1.0,
            toneMapping: 'aces',
            showWireframe: false,
            showNormals: false
        };
    }

    async init() {
        try {
            // Check WebGPU support
            if (!navigator.gpu) {
                document.getElementById('error-message').style.display = 'block';
                throw new Error('WebGPU not supported');
            }

            this.updateStatus('Initializing WebGPU...');

            // Initialize renderer
            this.renderer = new WebGPURenderer(this.canvas);
            await this.renderer.init();

            // Setup controls
            this.controls = new OrbitControls(this.renderer.camera, this.canvas);

            // Initialize TinyUSDZ loader
            this.updateStatus('Initializing TinyUSDZ WASM...');
            this.loader = new TinyUSDZLoader(null, { maxMemoryLimitMB: 512 });
            await this.loader.init({ useMemory64: false });

            // Setup UI
            this.setupUI();
            this.setupEventListeners();

            // Handle resize
            this.resize();
            window.addEventListener('resize', () => this.resize());

            // Load default scene
            await this.loadDefaultScene();

            // Start render loop
            this.animate();

            this.updateStatus('Ready');

        } catch (error) {
            console.error('Initialization failed:', error);
            this.updateStatus('Error: ' + error.message);
        }
    }

    setupUI() {
        // Environment select
        document.getElementById('env-select').addEventListener('change', (e) => {
            this.settings.envMapPreset = e.target.value;
        });

        // Env intensity
        const envIntensitySlider = document.getElementById('env-intensity');
        const envIntensityValue = document.getElementById('env-intensity-value');
        envIntensitySlider.addEventListener('input', (e) => {
            this.settings.envIntensity = parseFloat(e.target.value);
            this.renderer.settings.envIntensity = this.settings.envIntensity;
            envIntensityValue.textContent = this.settings.envIntensity.toFixed(1);
        });

        // Exposure
        const exposureSlider = document.getElementById('exposure');
        const exposureValue = document.getElementById('exposure-value');
        exposureSlider.addEventListener('input', (e) => {
            this.settings.exposure = parseFloat(e.target.value);
            this.renderer.settings.exposure = this.settings.exposure;
            exposureValue.textContent = this.settings.exposure.toFixed(1);
        });

        // Tone mapping
        const tonemapSelect = document.getElementById('tonemap-select');
        tonemapSelect.addEventListener('change', (e) => {
            const mapping = { linear: 0, reinhard: 1, aces: 2, agx: 3 };
            this.settings.toneMapping = e.target.value;
            this.renderer.settings.toneMapping = mapping[e.target.value] ?? 2;
        });

        // Wireframe
        document.getElementById('show-wireframe').addEventListener('change', (e) => {
            this.settings.showWireframe = e.target.checked;
            this.renderer.settings.showWireframe = e.target.checked;
        });

        // Normals
        document.getElementById('show-normals').addEventListener('change', (e) => {
            this.settings.showNormals = e.target.checked;
            this.renderer.settings.showNormals = e.target.checked;
        });
    }

    setupEventListeners() {
        // File input
        const fileInput = document.getElementById('file-input');
        fileInput.addEventListener('change', (e) => {
            if (e.target.files.length > 0) {
                this.loadUSDFile(e.target.files[0]);
            }
            e.target.value = '';
        });

        // Drag and drop
        const container = document.getElementById('canvas-container');
        container.addEventListener('dragover', (e) => {
            e.preventDefault();
            e.dataTransfer.dropEffect = 'copy';
            container.classList.add('drag-over');
        });

        container.addEventListener('dragleave', (e) => {
            e.preventDefault();
            container.classList.remove('drag-over');
        });

        container.addEventListener('drop', (e) => {
            e.preventDefault();
            container.classList.remove('drag-over');

            const files = e.dataTransfer.files;
            if (files.length > 0) {
                const file = files[0];
                const ext = file.name.toLowerCase().split('.').pop();
                if (['usd', 'usda', 'usdc', 'usdz'].includes(ext)) {
                    this.loadUSDFile(file);
                } else {
                    this.updateStatus('Please drop a USD file (.usd, .usda, .usdc, .usdz)');
                }
            }
        });
    }

    resize() {
        const width = window.innerWidth;
        const height = window.innerHeight;
        this.canvas.width = width * window.devicePixelRatio;
        this.canvas.height = height * window.devicePixelRatio;
        this.canvas.style.width = width + 'px';
        this.canvas.style.height = height + 'px';
        this.renderer.resize(this.canvas.width, this.canvas.height);
    }

    async loadDefaultScene() {
        this.updateStatus('Loading default teapot...');

        try {
            const response = await fetch('./assets/fancy-teapot-mtlx.usdz');
            if (!response.ok) throw new Error('Failed to fetch default scene');

            const arrayBuffer = await response.arrayBuffer();
            const data = new Uint8Array(arrayBuffer);
            await this.loadUSDFromData(data, 'fancy-teapot-mtlx.usdz');
        } catch (error) {
            console.error('Failed to load default scene:', error);
            this.updateStatus('Failed to load default scene, creating fallback...');
            this.createFallbackScene();
        }
    }

    async loadUSDFile(file) {
        this.updateStatus(`Loading: ${file.name}...`);

        try {
            const arrayBuffer = await file.arrayBuffer();
            const data = new Uint8Array(arrayBuffer);
            await this.loadUSDFromData(data, file.name);
        } catch (error) {
            console.error('Failed to load USD file:', error);
            this.updateStatus('Error: ' + error.message);
        }
    }

    async loadUSDFromData(data, filename) {
        // Clear previous scene
        this.renderer.destroy();
        this.renderer.meshes = [];

        // Create new native loader
        this.nativeLoader = new this.loader.native_.TinyUSDZLoaderNative();

        const success = this.nativeLoader.loadFromBinary(data, filename);
        if (!success) {
            this.updateStatus('Failed to parse USD file');
            return;
        }

        const numMeshes = this.nativeLoader.numMeshes();
        const numMaterials = this.nativeLoader.numMaterials();

        this.updateStatus(`Processing: ${numMeshes} meshes, ${numMaterials} materials...`);

        // Load materials
        const materials = [];
        for (let i = 0; i < numMaterials; i++) {
            try {
                const result = this.nativeLoader.getMaterialWithFormat(i, 'json');
                if (!result.error) {
                    const matData = JSON.parse(result.data);
                    materials.push(this.convertMaterialToWebGPU(matData, i));
                } else {
                    materials.push(this.createDefaultMaterial());
                }
            } catch (e) {
                console.warn(`Failed to get material ${i}:`, e);
                materials.push(this.createDefaultMaterial());
            }
        }

        // Ensure at least one default material
        if (materials.length === 0) {
            materials.push(this.createDefaultMaterial());
        }

        this.renderer.updateMaterials(materials);

        // Load meshes and create instances
        const instances = [];
        let boundingBox = { min: [Infinity, Infinity, Infinity], max: [-Infinity, -Infinity, -Infinity] };

        for (let i = 0; i < numMeshes; i++) {
            const meshData = this.nativeLoader.getMesh(i);
            if (!meshData) continue;

            // Convert mesh to WebGPU buffers
            const gpuMesh = this.renderer.createMeshBuffers({
                positions: meshData.points,
                normals: meshData.normals,
                uvs: meshData.texcoords,
                tangents: meshData.tangents,
                indices: meshData.faceVertexIndices
            });

            // Determine material index
            let materialIndex = 0;
            if (meshData.materialId !== undefined && meshData.materialId >= 0 && meshData.materialId < materials.length) {
                materialIndex = meshData.materialId;
            }

            // Create instance
            instances.push({
                modelMatrix: mat4Identity(),
                normalMatrix: mat4Identity(),
                materialIndex: materialIndex
            });

            gpuMesh.instanceCount = 1;
            gpuMesh.firstInstance = instances.length - 1;
            this.renderer.meshes.push(gpuMesh);

            // Update bounding box
            for (let j = 0; j < meshData.points.length; j += 3) {
                boundingBox.min[0] = Math.min(boundingBox.min[0], meshData.points[j]);
                boundingBox.min[1] = Math.min(boundingBox.min[1], meshData.points[j + 1]);
                boundingBox.min[2] = Math.min(boundingBox.min[2], meshData.points[j + 2]);
                boundingBox.max[0] = Math.max(boundingBox.max[0], meshData.points[j]);
                boundingBox.max[1] = Math.max(boundingBox.max[1], meshData.points[j + 1]);
                boundingBox.max[2] = Math.max(boundingBox.max[2], meshData.points[j + 2]);
            }
        }

        this.renderer.updateInstances(instances);

        // Fit camera to scene
        if (boundingBox.min[0] !== Infinity) {
            this.controls.fitToBox(boundingBox.min, boundingBox.max);
        }

        // Update UI
        document.getElementById('model-info').style.display = 'block';
        document.getElementById('mesh-count').textContent = numMeshes;
        document.getElementById('material-count').textContent = numMaterials;
        document.getElementById('draw-call-count').textContent = this.renderer.meshes.length;
        document.getElementById('triangle-count').textContent = this.renderer.stats.triangles;

        this.updateStatus(`Loaded: ${numMeshes} meshes, ${numMaterials} materials`);
    }

    convertMaterialToWebGPU(matData, index) {
        const material = this.createDefaultMaterial();

        // Check for OpenPBR data
        if (matData.hasOpenPBR && matData.openPBRShader) {
            const pbr = matData.openPBRShader;

            // Base layer
            if (pbr.base_color) {
                material.baseColor = pbr.base_color;
            }
            material.baseWeight = pbr.base_weight ?? 1.0;
            material.baseMetalness = pbr.base_metalness ?? 0.0;
            material.baseDiffuseRoughness = pbr.base_diffuse_roughness ?? 0.0;

            // Specular layer
            if (pbr.specular_color) {
                material.specularColor = pbr.specular_color;
            }
            material.specularWeight = pbr.specular_weight ?? 1.0;
            material.specularRoughness = pbr.specular_roughness ?? 0.5;
            material.specularIOR = pbr.specular_ior ?? 1.5;
            material.specularAnisotropy = pbr.specular_anisotropy ?? 0.0;

            // Transmission layer
            material.transmissionWeight = pbr.transmission_weight ?? 0.0;
            material.transmissionDepth = pbr.transmission_depth ?? 0.0;
            material.transmissionDispersion = pbr.transmission_dispersion ?? 0.0;

            // Coat layer
            if (pbr.coat_color) {
                material.coatColor = pbr.coat_color;
            }
            material.coatWeight = pbr.coat_weight ?? 0.0;
            material.coatRoughness = pbr.coat_roughness ?? 0.1;
            material.coatIOR = pbr.coat_ior ?? 1.5;

            // Emission layer
            if (pbr.emission_color) {
                material.emissionColor = pbr.emission_color;
            }
            material.emissionLuminance = pbr.emission_luminance ?? 0.0;

            // Fuzz/Sheen layer
            if (pbr.fuzz_color) {
                material.sheenColor = pbr.fuzz_color;
            }
            material.sheenWeight = pbr.fuzz_weight ?? 0.0;

            console.log(`Material ${index}: OpenPBR`, material);
        }
        // Fallback to UsdPreviewSurface
        else if (matData.hasUsdPreviewSurface) {
            if (matData.diffuseColor) {
                material.baseColor = matData.diffuseColor;
            }
            material.baseMetalness = matData.metallic ?? 0.0;
            material.specularRoughness = matData.roughness ?? 0.5;
            material.specularIOR = matData.ior ?? 1.5;
            material.coatWeight = matData.clearcoat ?? 0.0;
            material.coatRoughness = matData.clearcoatRoughness ?? 0.1;

            if (matData.emissiveColor) {
                material.emissionColor = matData.emissiveColor;
                material.emissionLuminance = 1.0;
            }

            console.log(`Material ${index}: UsdPreviewSurface`, material);
        }

        return material;
    }

    createDefaultMaterial() {
        return {
            baseColor: [0.8, 0.8, 0.8],
            baseWeight: 1.0,
            baseMetalness: 0.0,
            baseDiffuseRoughness: 0.0,
            specularColor: [1.0, 1.0, 1.0],
            specularWeight: 1.0,
            specularRoughness: 0.5,
            specularIOR: 1.5,
            specularAnisotropy: 0.0,
            transmissionWeight: 0.0,
            transmissionDepth: 0.0,
            transmissionDispersion: 0.0,
            coatColor: [1.0, 1.0, 1.0],
            coatWeight: 0.0,
            coatRoughness: 0.1,
            coatIOR: 1.5,
            emissionColor: [0.0, 0.0, 0.0],
            emissionLuminance: 0.0,
            sheenColor: [1.0, 1.0, 1.0],
            sheenWeight: 0.0,
            baseColorTexIdx: -1,
            normalTexIdx: -1,
            roughnessTexIdx: -1,
            metalnessTexIdx: -1,
            emissiveTexIdx: -1,
            aoTexIdx: -1
        };
    }

    createFallbackScene() {
        // Create a simple sphere
        const segments = 32;
        const rings = 16;
        const positions = [];
        const normals = [];
        const uvs = [];
        const indices = [];

        for (let y = 0; y <= rings; y++) {
            for (let x = 0; x <= segments; x++) {
                const u = x / segments;
                const v = y / rings;
                const theta = u * Math.PI * 2;
                const phi = v * Math.PI;

                const px = Math.sin(phi) * Math.cos(theta);
                const py = Math.cos(phi);
                const pz = Math.sin(phi) * Math.sin(theta);

                positions.push(px, py, pz);
                normals.push(px, py, pz);
                uvs.push(u, 1 - v);
            }
        }

        for (let y = 0; y < rings; y++) {
            for (let x = 0; x < segments; x++) {
                const a = y * (segments + 1) + x;
                const b = a + segments + 1;
                indices.push(a, b, a + 1);
                indices.push(b, b + 1, a + 1);
            }
        }

        const gpuMesh = this.renderer.createMeshBuffers({
            positions: new Float32Array(positions),
            normals: new Float32Array(normals),
            uvs: new Float32Array(uvs),
            tangents: null,
            indices: new Uint32Array(indices)
        });

        // Create default material
        const material = this.createDefaultMaterial();
        material.baseColor = [0.9, 0.7, 0.3]; // Gold
        material.baseMetalness = 0.8;
        material.specularRoughness = 0.3;

        this.renderer.updateMaterials([material]);

        // Create instance
        this.renderer.updateInstances([{
            modelMatrix: mat4Identity(),
            normalMatrix: mat4Identity(),
            materialIndex: 0
        }]);

        gpuMesh.instanceCount = 1;
        gpuMesh.firstInstance = 0;
        this.renderer.meshes.push(gpuMesh);

        // Fit camera
        this.controls.fitToBox([-1, -1, -1], [1, 1, 1]);

        document.getElementById('model-info').style.display = 'block';
        document.getElementById('mesh-count').textContent = '1';
        document.getElementById('material-count').textContent = '1';

        this.updateStatus('Fallback scene loaded');
    }

    animate() {
        this.animationId = requestAnimationFrame(() => this.animate());

        this.renderer.render();

        // Update stats display
        document.getElementById('fps').textContent = this.renderer.stats.fps;
        document.getElementById('gpu-time').textContent = this.renderer.stats.gpuTime.toFixed(2) + 'ms';
        document.getElementById('ubo-updates').textContent = this.renderer.stats.uboUpdates;
        document.getElementById('draw-call-count').textContent = this.renderer.stats.drawCalls;
        document.getElementById('triangle-count').textContent = this.renderer.stats.triangles;
    }

    updateStatus(message) {
        const statusEl = document.getElementById('status');
        if (statusEl) {
            statusEl.textContent = message;
        }
        console.log(message);
    }
}

// ============================================================================
// Global Functions
// ============================================================================

window.loadFile = () => document.getElementById('file-input').click();

// ============================================================================
// Start Application
// ============================================================================

const app = new Application();
app.init().catch(err => {
    console.error('Failed to initialize:', err);
    document.getElementById('status').textContent = 'Error: ' + err.message;
});
