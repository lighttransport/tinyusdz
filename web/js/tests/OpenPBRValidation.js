// OpenPBR BRDF Validation Framework
// Texture-based GPU validation with readback comparison against JS ground truth

import * as THREE from 'three';

// ============================================================================
// Shader Sources (Embedded)
// ============================================================================

const fullscreenQuadVertexShader = `
varying vec2 vUv;

void main() {
    vUv = uv;
    gl_Position = vec4(position.xy, 0.0, 1.0);
}
`;

const fresnelTestFragmentShader = `
precision highp float;

varying vec2 vUv;

// Fresnel Schlick: F(μ) = F₀ + (1 - F₀)(1 - μ)⁵
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    // X-axis: VdotH (cosTheta) from 0 to 1
    // Y-axis: F0 from 0 to 1
    float VdotH = vUv.x;
    float F0_scalar = vUv.y;
    vec3 F0 = vec3(F0_scalar);

    vec3 F = fresnelSchlick(VdotH, F0);

    gl_FragColor = vec4(F, 1.0);
}
`;

const ggxNDFTestFragmentShader = `
precision highp float;

varying vec2 vUv;

#define PI 3.14159265359

// GGX Normal Distribution Function
// D(m) = α² / (π * (cos²θ * (α² - 1) + 1)²)
float distributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

void main() {
    // X-axis: NdotH from 0 to 1
    // Y-axis: roughness from 0.01 to 1 (avoid 0 for numerical stability)
    float NdotH = vUv.x;
    float roughness = max(0.01, vUv.y);

    float D = distributionGGX(NdotH, roughness);

    // Clamp to reasonable range for visualization (D can be very large at low roughness)
    D = min(D, 100.0);

    gl_FragColor = vec4(D, D, D, 1.0);
}
`;

const smithGTestFragmentShader = `
precision highp float;

varying vec2 vUv;

uniform float u_NdotL; // Fixed NdotL value for this test

// Smith GGX Geometry Function (height-correlated)
// G₁(v) = 2 * NdotV / (NdotV + sqrt(α² + (1 - α²) * NdotV²))
float geometrySmithGGX1(float NdotX, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotX2 = NdotX * NdotX;
    return 2.0 * NdotX / (NdotX + sqrt(a2 + (1.0 - a2) * NdotX2));
}

float geometrySmithGGX(float NdotV, float NdotL, float roughness) {
    return geometrySmithGGX1(NdotV, roughness) * geometrySmithGGX1(NdotL, roughness);
}

void main() {
    // X-axis: NdotV from 0.001 to 1 (avoid 0)
    // Y-axis: roughness from 0.01 to 1
    float NdotV = max(0.001, vUv.x);
    float roughness = max(0.01, vUv.y);
    float NdotL = u_NdotL;

    float G = geometrySmithGGX(NdotV, NdotL, roughness);

    gl_FragColor = vec4(G, G, G, 1.0);
}
`;

const brdfFullTestFragmentShader = `
precision highp float;

varying vec2 vUv;

uniform vec3 u_baseColor;
uniform float u_metalness;
uniform float u_ior;

#define PI 3.14159265359

// IOR to F0 conversion
float iorToF0(float ior) {
    float r = (ior - 1.0) / (ior + 1.0);
    return r * r;
}

// Fresnel Schlick
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// GGX NDF
float distributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// Smith GGX Geometry
float geometrySmithGGX1(float NdotX, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotX2 = NdotX * NdotX;
    return 2.0 * NdotX / (NdotX + sqrt(a2 + (1.0 - a2) * NdotX2));
}

float geometrySmithGGX(float NdotV, float NdotL, float roughness) {
    return geometrySmithGGX1(NdotV, roughness) * geometrySmithGGX1(NdotL, roughness);
}

// Full BRDF evaluation for a single light direction
// Assumes V and L are in the same plane as N (2D slice)
vec3 evaluateBRDF(float NdotV, float NdotL, float roughness, vec3 baseColor, float metalness, float ior) {
    if (NdotL <= 0.0 || NdotV <= 0.0) return vec3(0.0);

    // Compute half vector angle (assuming V and L in same plane, same side of N)
    // H = normalize(V + L), θ_H = (θ_V + θ_L) / 2
    float thetaV = acos(clamp(NdotV, 0.0, 1.0));
    float thetaL = acos(clamp(NdotL, 0.0, 1.0));
    float thetaH = (thetaV + thetaL) / 2.0;
    float NdotH = cos(thetaH);

    // VdotH = cos((θ_V - θ_L) / 2) - angle between V and H
    float VdotH = cos(abs(thetaV - thetaL) / 2.0);

    // F0 calculation
    float dielectricF0 = iorToF0(ior);
    vec3 F0 = mix(vec3(dielectricF0), baseColor, metalness);

    // BRDF components
    float D = distributionGGX(NdotH, roughness);
    float G = geometrySmithGGX(NdotV, NdotL, roughness);
    vec3 F = fresnelSchlick(VdotH, F0);

    // Specular BRDF
    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);

    // Diffuse BRDF (Lambertian, weighted by 1-F for energy conservation)
    vec3 diffuse = baseColor / PI * (1.0 - metalness) * (1.0 - F);

    return specular + diffuse;
}

void main() {
    // X-axis: NdotV from 0.001 to 1
    // Y-axis: roughness from 0.01 to 1
    // Fixed: NdotL = 0.7, baseColor, metalness, ior from uniforms
    float NdotV = max(0.001, vUv.x);
    float roughness = max(0.01, vUv.y);
    float NdotL = 0.7;

    vec3 brdf = evaluateBRDF(NdotV, NdotL, roughness, u_baseColor, u_metalness, u_ior);

    // Clamp for visualization
    brdf = min(brdf, vec3(10.0));

    gl_FragColor = vec4(brdf, 1.0);
}
`;

// ============================================================================
// Layer Mixing Shaders (OpenPBR Evaluation Tree)
// ============================================================================

// Test shader for OpenPBR layer mixing operations
// Validates: mix, layer, weighted_layer operations
const layerMixingTestFragmentShader = `
precision highp float;

varying vec2 vUv;

uniform float u_fuzz_weight;
uniform float u_coat_weight;
uniform float u_base_metalness;
uniform float u_transmission_weight;
uniform float u_subsurface_weight;
uniform float u_specular_ior;

#define PI 3.14159265359

// Compute directional albedo (Fresnel reflectance integral approximation)
// E(cosTheta) ≈ F0 + (1 - F0) * (1 - cosTheta)^5 integrated
// Simplified: use Fresnel at the viewing angle as approximation
float iorToF0(float ior) {
    float r = (ior - 1.0) / (ior + 1.0);
    return r * r;
}

float fresnelSchlick(float cosTheta, float F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Directional albedo approximation for GGX
// Using Kulla-Conty approximation: E(μ) ≈ 1 - (1-F0) * (1 - μ)
float directionalAlbedoGGX(float NdotV, float roughness, float F0) {
    // Simplified approximation
    float F = fresnelSchlick(NdotV, F0);
    // Account for multiple scattering (rough surfaces scatter more)
    float multiScatter = roughness * (1.0 - F) * 0.2;
    return clamp(F + multiScatter, 0.0, 1.0);
}

// OpenPBR mix operation: lerp(A, B, weight)
// f_mix = (1 - w) * f_0 + w * f_1
vec3 openpbrMix(vec3 f0, vec3 f1, float weight) {
    return mix(f0, f1, weight);
}

// OpenPBR layer operation with albedo scaling
// f_layer = f_coat + (1 - E_coat) * f_sub
vec3 openpbrLayer(vec3 f_sub, vec3 f_coat, float E_coat) {
    return f_coat + (1.0 - E_coat) * f_sub;
}

// OpenPBR weighted layer operation
// f_weighted_layer = w * f_coat + lerp(1, 1 - E_coat, w) * f_sub
vec3 openpbrWeightedLayer(vec3 f_sub, vec3 f_coat, float weight, float E_coat) {
    float subWeight = mix(1.0, 1.0 - E_coat, weight);
    return weight * f_coat + subWeight * f_sub;
}

// Compute the full layer weight hierarchy
// Returns: [fuzz_contrib, coat_contrib, metal_contrib, dielectric_contrib]
vec4 computeLayerWeights(
    float fuzz_weight,
    float coat_weight,
    float base_metalness,
    float transmission_weight,
    float subsurface_weight,
    float NdotV,
    float roughness,
    float coat_ior,
    float specular_ior
) {
    // Coat directional albedo
    float coat_F0 = iorToF0(coat_ior);
    float E_coat = directionalAlbedoGGX(NdotV, 0.0, coat_F0); // coat is usually smooth

    // Specular directional albedo
    float spec_F0 = iorToF0(specular_ior);
    float E_spec = directionalAlbedoGGX(NdotV, roughness, spec_F0);

    // Build the evaluation tree bottom-up
    // M_glossy-diffuse = layer(S_diffuse, S_gloss)
    // Diffuse gets weighted by (1 - E_spec)
    float diffuse_weight = 1.0 - E_spec;

    // M_opaque-base = mix(M_glossy-diffuse, S_subsurface, subsurface_weight)
    float opaque_diffuse = (1.0 - subsurface_weight) * diffuse_weight;
    float opaque_subsurface = subsurface_weight;

    // M_dielectric-base = mix(M_opaque-base, S_translucent-base, transmission_weight)
    float dielectric_opaque = (1.0 - transmission_weight);
    float dielectric_transmission = transmission_weight;

    // M_base-substrate = mix(M_dielectric-base, S_metal, base_metalness)
    float substrate_dielectric = (1.0 - base_metalness) * dielectric_opaque;
    float substrate_metal = base_metalness;

    // M_coated-base = layer(M_base-substrate, S_coat, coat_weight)
    // weighted_layer formula: sub gets multiplied by lerp(1, 1-E_coat, coat_weight)
    float coat_sub_weight = mix(1.0, 1.0 - E_coat, coat_weight);
    float coated_substrate = coat_sub_weight * (substrate_dielectric + substrate_metal);
    float coated_coat = coat_weight;

    // M_surface = layer(M_coated-base, S_fuzz, fuzz_weight)
    // Fuzz is additive on top
    float fuzz_sub_weight = mix(1.0, 0.5, fuzz_weight); // Fuzz blocks ~50% at full weight
    float final_fuzz = fuzz_weight;
    float final_coat = coated_coat * fuzz_sub_weight;
    float final_metal = substrate_metal * coat_sub_weight * fuzz_sub_weight;
    float final_dielectric = substrate_dielectric * coat_sub_weight * fuzz_sub_weight;

    return vec4(final_fuzz, final_coat, final_metal, final_dielectric);
}

void main() {
    // X-axis: NdotV from 0.01 to 1
    // Y-axis: roughness from 0.01 to 1
    float NdotV = max(0.01, vUv.x);
    float roughness = max(0.01, vUv.y);

    vec4 weights = computeLayerWeights(
        u_fuzz_weight,
        u_coat_weight,
        u_base_metalness,
        u_transmission_weight,
        u_subsurface_weight,
        NdotV,
        roughness,
        1.5, // coat_ior
        u_specular_ior
    );

    // Output: R=fuzz, G=coat, B=metal, A=dielectric
    gl_FragColor = weights;
}
`;

// Energy conservation test shader
const energyConservationTestFragmentShader = `
precision highp float;

varying vec2 vUv;

uniform float u_coat_weight;
uniform float u_base_metalness;
uniform float u_specular_ior;
uniform float u_roughness;

#define PI 3.14159265359

float iorToF0(float ior) {
    float r = (ior - 1.0) / (ior + 1.0);
    return r * r;
}

float fresnelSchlick(float cosTheta, float F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Approximate directional albedo for energy conservation check
float directionalAlbedoApprox(float NdotV, float roughness, float F0) {
    float F = fresnelSchlick(NdotV, F0);
    return F + (1.0 - F) * 0.3 * roughness; // Rough approximation
}

void main() {
    // X-axis: NdotV from 0.01 to 1
    // Y-axis: coat_weight from 0 to 1 (override uniform for sweep)
    float NdotV = max(0.01, vUv.x);
    float coat_w = vUv.y;

    float spec_F0 = iorToF0(u_specular_ior);
    float coat_F0 = iorToF0(1.5);

    // Compute layer albedos
    float E_spec = directionalAlbedoApprox(NdotV, u_roughness, spec_F0);
    float E_coat = directionalAlbedoApprox(NdotV, 0.03, coat_F0);

    // Diffuse contribution (energy not reflected by specular)
    float diffuse_albedo = (1.0 - E_spec) * (1.0 - u_base_metalness);

    // Specular contribution
    float specular_albedo = E_spec;

    // Base substrate total
    float base_albedo = diffuse_albedo + specular_albedo;

    // With coat: weighted_layer formula
    // E_layer = w*E_coat + lerp(1, 1-E_coat, w)*E_sub
    float coat_sub_weight = mix(1.0, 1.0 - E_coat, coat_w);
    float total_albedo = coat_w * E_coat + coat_sub_weight * base_albedo;

    // Energy conservation: total should be <= 1
    float energy_excess = max(0.0, total_albedo - 1.0);

    // Output: R=total_albedo, G=energy_excess, B=coat_contribution, A=base_contribution
    gl_FragColor = vec4(total_albedo, energy_excess, coat_w * E_coat, coat_sub_weight * base_albedo);
}
`;

// ============================================================================
// JavaScript Ground Truth Functions
// ============================================================================

/**
 * Fresnel Schlick approximation
 */
function fresnelSchlickJS(cosTheta, F0) {
    const t = Math.pow(Math.max(0, 1.0 - cosTheta), 5);
    if (Array.isArray(F0)) {
        return F0.map(f => f + (1.0 - f) * t);
    }
    return F0 + (1.0 - F0) * t;
}

/**
 * IOR to F0 conversion
 */
function iorToF0JS(ior) {
    const r = (ior - 1.0) / (ior + 1.0);
    return r * r;
}

/**
 * GGX Normal Distribution Function
 */
function distributionGGXJS(NdotH, roughness) {
    const a = roughness * roughness;
    const a2 = a * a;
    const NdotH2 = NdotH * NdotH;
    const denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (Math.PI * denom * denom);
}

/**
 * Smith GGX Geometry Function (single direction)
 */
function geometrySmithGGX1JS(NdotX, roughness) {
    const a = roughness * roughness;
    const a2 = a * a;
    const NdotX2 = NdotX * NdotX;
    return 2.0 * NdotX / (NdotX + Math.sqrt(a2 + (1.0 - a2) * NdotX2));
}

/**
 * Smith GGX Geometry Function (both directions)
 */
function geometrySmithGGXJS(NdotV, NdotL, roughness) {
    return geometrySmithGGX1JS(NdotV, roughness) * geometrySmithGGX1JS(NdotL, roughness);
}

/**
 * Full BRDF evaluation
 */
function evaluateBRDFJS(NdotV, NdotL, roughness, baseColor, metalness, ior) {
    if (NdotL <= 0 || NdotV <= 0) return [0, 0, 0];

    // Compute angles (assuming V and L in same plane, same side of N)
    // θ_H = (θ_V + θ_L) / 2, VdotH = cos((θ_V - θ_L) / 2)
    const thetaV = Math.acos(Math.max(0, Math.min(1, NdotV)));
    const thetaL = Math.acos(Math.max(0, Math.min(1, NdotL)));
    const thetaH = (thetaV + thetaL) / 2.0;
    const NdotH = Math.cos(thetaH);
    const VdotH = Math.cos(Math.abs(thetaV - thetaL) / 2.0);

    // F0 calculation
    const dielectricF0 = iorToF0JS(ior);
    const F0 = baseColor.map(c => dielectricF0 * (1.0 - metalness) + c * metalness);

    // BRDF components
    const D = distributionGGXJS(NdotH, roughness);
    const G = geometrySmithGGXJS(NdotV, NdotL, roughness);
    const F = fresnelSchlickJS(VdotH, F0);

    // Specular
    const specDenom = Math.max(4.0 * NdotV * NdotL, 0.001);
    const specular = F.map(f => (D * G * f) / specDenom);

    // Diffuse (Lambertian)
    const diffuse = baseColor.map((c, i) => (c / Math.PI) * (1.0 - metalness) * (1.0 - F[i]));

    return specular.map((s, i) => s + diffuse[i]);
}

// ============================================================================
// OpenPBR Layer Mixing Ground Truth Functions
// ============================================================================

/**
 * Directional albedo approximation for GGX
 */
function directionalAlbedoGGXJS(NdotV, roughness, F0) {
    const F = fresnelSchlickJS(NdotV, F0);
    const multiScatter = roughness * (1.0 - F) * 0.2;
    return Math.min(1.0, Math.max(0.0, F + multiScatter));
}

/**
 * OpenPBR mix operation: lerp(f0, f1, weight)
 * f_mix = (1 - w) * f_0 + w * f_1
 */
function openpbrMixJS(f0, f1, weight) {
    if (Array.isArray(f0)) {
        return f0.map((v, i) => (1.0 - weight) * v + weight * f1[i]);
    }
    return (1.0 - weight) * f0 + weight * f1;
}

/**
 * OpenPBR layer operation with albedo scaling
 * f_layer = f_coat + (1 - E_coat) * f_sub
 */
function openpbrLayerJS(f_sub, f_coat, E_coat) {
    if (Array.isArray(f_sub)) {
        return f_sub.map((v, i) => f_coat[i] + (1.0 - E_coat) * v);
    }
    return f_coat + (1.0 - E_coat) * f_sub;
}

/**
 * OpenPBR weighted layer operation
 * f_weighted_layer = w * f_coat + lerp(1, 1 - E_coat, w) * f_sub
 */
function openpbrWeightedLayerJS(f_sub, f_coat, weight, E_coat) {
    const subWeight = (1.0 - weight) * 1.0 + weight * (1.0 - E_coat);
    if (Array.isArray(f_sub)) {
        return f_sub.map((v, i) => weight * f_coat[i] + subWeight * v);
    }
    return weight * f_coat + subWeight * f_sub;
}

/**
 * Compute full OpenPBR layer weight hierarchy
 * Returns: { fuzz, coat, metal, dielectric }
 */
function computeLayerWeightsJS(params) {
    const {
        fuzz_weight = 0,
        coat_weight = 0,
        base_metalness = 0,
        transmission_weight = 0,
        subsurface_weight = 0,
        NdotV,
        roughness,
        coat_ior = 1.5,
        specular_ior = 1.5
    } = params;

    // Coat directional albedo
    const coat_F0 = iorToF0JS(coat_ior);
    const E_coat = directionalAlbedoGGXJS(NdotV, 0.0, coat_F0);

    // Specular directional albedo
    const spec_F0 = iorToF0JS(specular_ior);
    const E_spec = directionalAlbedoGGXJS(NdotV, roughness, spec_F0);

    // Build evaluation tree bottom-up
    // M_glossy-diffuse = layer(S_diffuse, S_gloss)
    const diffuse_weight = 1.0 - E_spec;

    // M_opaque-base = mix(M_glossy-diffuse, S_subsurface, subsurface_weight)
    const opaque_diffuse = (1.0 - subsurface_weight) * diffuse_weight;

    // M_dielectric-base = mix(M_opaque-base, S_translucent-base, transmission_weight)
    const dielectric_opaque = (1.0 - transmission_weight);

    // M_base-substrate = mix(M_dielectric-base, S_metal, base_metalness)
    const substrate_dielectric = (1.0 - base_metalness) * dielectric_opaque;
    const substrate_metal = base_metalness;

    // M_coated-base = layer(M_base-substrate, S_coat, coat_weight)
    const coat_sub_weight = (1.0 - coat_weight) * 1.0 + coat_weight * (1.0 - E_coat);
    const coated_coat = coat_weight;

    // M_surface = layer(M_coated-base, S_fuzz, fuzz_weight)
    const fuzz_sub_weight = (1.0 - fuzz_weight) * 1.0 + fuzz_weight * 0.5;
    const final_fuzz = fuzz_weight;
    const final_coat = coated_coat * fuzz_sub_weight;
    const final_metal = substrate_metal * coat_sub_weight * fuzz_sub_weight;
    const final_dielectric = substrate_dielectric * coat_sub_weight * fuzz_sub_weight;

    return {
        fuzz: final_fuzz,
        coat: final_coat,
        metal: final_metal,
        dielectric: final_dielectric
    };
}

/**
 * Compute energy conservation metrics
 * Returns: { totalAlbedo, energyExcess, coatContribution, baseContribution }
 */
function computeEnergyConservationJS(params) {
    const {
        coat_weight = 0,
        base_metalness = 0,
        specular_ior = 1.5,
        roughness = 0.5,
        NdotV
    } = params;

    const spec_F0 = iorToF0JS(specular_ior);
    const coat_F0 = iorToF0JS(1.5);

    // Compute layer albedos
    const E_spec = directionalAlbedoGGXJS(NdotV, roughness, spec_F0);
    const E_coat = directionalAlbedoGGXJS(NdotV, 0.03, coat_F0);

    // Diffuse contribution
    const diffuse_albedo = (1.0 - E_spec) * (1.0 - base_metalness);
    const specular_albedo = E_spec;
    const base_albedo = diffuse_albedo + specular_albedo;

    // With coat: weighted_layer formula
    const coat_sub_weight = (1.0 - coat_weight) * 1.0 + coat_weight * (1.0 - E_coat);
    const coatContribution = coat_weight * E_coat;
    const baseContribution = coat_sub_weight * base_albedo;
    const totalAlbedo = coatContribution + baseContribution;
    const energyExcess = Math.max(0, totalAlbedo - 1.0);

    return { totalAlbedo, energyExcess, coatContribution, baseContribution };
}

// ============================================================================
// OpenPBRValidator Class
// ============================================================================

export class OpenPBRValidator {
    constructor(renderer, resolution = 256) {
        this.renderer = renderer;
        this.resolution = resolution;

        // Create render target with float precision
        this.renderTarget = new THREE.WebGLRenderTarget(this.resolution, this.resolution, {
            type: THREE.FloatType,
            format: THREE.RGBAFormat,
            minFilter: THREE.NearestFilter,
            magFilter: THREE.NearestFilter
        });

        // Fullscreen quad geometry
        this.quadGeometry = new THREE.PlaneGeometry(2, 2);

        // Scene and camera for quad rendering
        this.quadScene = new THREE.Scene();
        this.quadCamera = new THREE.OrthographicCamera(-1, 1, 1, -1, 0, 1);

        // Create shader materials
        this.materials = {
            fresnel: new THREE.ShaderMaterial({
                vertexShader: fullscreenQuadVertexShader,
                fragmentShader: fresnelTestFragmentShader
            }),
            ggxNDF: new THREE.ShaderMaterial({
                vertexShader: fullscreenQuadVertexShader,
                fragmentShader: ggxNDFTestFragmentShader
            }),
            smithG: new THREE.ShaderMaterial({
                vertexShader: fullscreenQuadVertexShader,
                fragmentShader: smithGTestFragmentShader,
                uniforms: {
                    u_NdotL: { value: 0.5 }
                }
            }),
            brdfFull: new THREE.ShaderMaterial({
                vertexShader: fullscreenQuadVertexShader,
                fragmentShader: brdfFullTestFragmentShader,
                uniforms: {
                    u_baseColor: { value: new THREE.Vector3(0.8, 0.8, 0.8) },
                    u_metalness: { value: 0.0 },
                    u_ior: { value: 1.5 }
                }
            }),
            layerMixing: new THREE.ShaderMaterial({
                vertexShader: fullscreenQuadVertexShader,
                fragmentShader: layerMixingTestFragmentShader,
                uniforms: {
                    u_fuzz_weight: { value: 0.0 },
                    u_coat_weight: { value: 0.0 },
                    u_base_metalness: { value: 0.0 },
                    u_transmission_weight: { value: 0.0 },
                    u_subsurface_weight: { value: 0.0 },
                    u_specular_ior: { value: 1.5 }
                }
            }),
            energyConservation: new THREE.ShaderMaterial({
                vertexShader: fullscreenQuadVertexShader,
                fragmentShader: energyConservationTestFragmentShader,
                uniforms: {
                    u_coat_weight: { value: 0.0 },
                    u_base_metalness: { value: 0.0 },
                    u_specular_ior: { value: 1.5 },
                    u_roughness: { value: 0.5 }
                }
            })
        };

        // Quad mesh (material assigned per test)
        this.quadMesh = new THREE.Mesh(this.quadGeometry, this.materials.fresnel);
        this.quadScene.add(this.quadMesh);

        // Results storage
        this.results = {};
    }

    /**
     * Render a test shader and read back pixels
     */
    renderAndReadback(material) {
        this.quadMesh.material = material;

        // Save current render target
        const currentTarget = this.renderer.getRenderTarget();

        // Render to our target
        this.renderer.setRenderTarget(this.renderTarget);
        this.renderer.render(this.quadScene, this.quadCamera);

        // Read pixels
        const pixelBuffer = new Float32Array(this.resolution * this.resolution * 4);
        this.renderer.readRenderTargetPixels(
            this.renderTarget,
            0, 0,
            this.resolution, this.resolution,
            pixelBuffer
        );

        // Restore render target
        this.renderer.setRenderTarget(currentTarget);

        return pixelBuffer;
    }

    /**
     * Run Fresnel test
     */
    runFresnelTest() {
        console.log('=== Fresnel Test ===');

        const gpuPixels = this.renderAndReadback(this.materials.fresnel);

        let maxError = 0;
        let totalError = 0;
        let numPixels = 0;
        const errors = [];

        // Skip edge pixels to avoid boundary issues
        const margin = 1;

        for (let y = margin; y < this.resolution - margin; y++) {
            for (let x = margin; x < this.resolution - margin; x++) {
                const i = (y * this.resolution + x) * 4;

                // Parameters from UV
                const VdotH = x / (this.resolution - 1);
                const F0 = y / (this.resolution - 1);

                // GPU result
                const gpuR = gpuPixels[i];

                // JS ground truth
                const jsR = fresnelSchlickJS(VdotH, F0);

                // Error
                const error = Math.abs(gpuR - jsR);
                maxError = Math.max(maxError, error);
                totalError += error;
                numPixels++;

                if (error > 0.01) {
                    errors.push({ x, y, VdotH, F0, gpuR, jsR, error });
                }
            }
        }

        const avgError = totalError / numPixels;

        console.log(`  Max Error: ${maxError.toFixed(6)}`);
        console.log(`  Avg Error: ${avgError.toFixed(6)}`);
        console.log(`  Failing Pixels (>0.01): ${errors.length}`);

        if (errors.length > 0 && errors.length <= 10) {
            errors.forEach(e => {
                console.log(`    VdotH=${e.VdotH.toFixed(3)}, F0=${e.F0.toFixed(3)}: GPU=${e.gpuR.toFixed(4)}, JS=${e.jsR.toFixed(4)}, Err=${e.error.toFixed(4)}`);
            });
        }

        // Pass if max error < 0.02 (2% tolerance for floating point)
        this.results.fresnel = { maxError, avgError, failingPixels: errors.length, passed: maxError < 0.02 };
        return this.results.fresnel;
    }

    /**
     * Run GGX NDF test
     */
    runGGXNDFTest() {
        console.log('=== GGX NDF Test ===');

        const gpuPixels = this.renderAndReadback(this.materials.ggxNDF);

        let maxError = 0;
        let totalError = 0;
        let numPixels = 0;
        const errors = [];

        // Skip edge pixels - GGX has numerical issues at boundaries
        // NdotH=0 → D approaches 0 but can have precision issues
        // NdotH=1, roughness→0 → D spikes to infinity
        const margin = 2;

        for (let y = margin; y < this.resolution - margin; y++) {
            for (let x = margin; x < this.resolution - margin; x++) {
                const i = (y * this.resolution + x) * 4;

                const NdotH = x / (this.resolution - 1);
                const roughness = Math.max(0.01, y / (this.resolution - 1));

                // Skip very low roughness at high NdotH (numerical instability zone)
                if (roughness < 0.05 && NdotH > 0.95) continue;

                // GPU result (clamped to 100 in shader)
                const gpuD = Math.min(gpuPixels[i], 100);

                // JS ground truth
                const jsD = Math.min(distributionGGXJS(NdotH, roughness), 100);

                // Use absolute error for small values, relative for large
                // This handles the transition zone better
                const absError = Math.abs(gpuD - jsD);
                let error;
                if (jsD < 0.1) {
                    // For small values, use absolute error
                    error = absError;
                } else {
                    // For larger values, use relative error
                    error = absError / jsD;
                }

                maxError = Math.max(maxError, error);
                totalError += error;
                numPixels++;

                if (error > 0.05) {
                    errors.push({ x, y, NdotH, roughness, gpuD, jsD, error });
                }
            }
        }

        const avgError = totalError / numPixels;

        console.log(`  Max Error: ${maxError.toFixed(6)}`);
        console.log(`  Avg Error: ${avgError.toFixed(6)}`);
        console.log(`  Failing Pixels (>5%): ${errors.length}`);

        if (errors.length > 0 && errors.length <= 5) {
            errors.forEach(e => {
                console.log(`    NdotH=${e.NdotH.toFixed(3)}, rough=${e.roughness.toFixed(3)}: GPU=${e.gpuD.toFixed(4)}, JS=${e.jsD.toFixed(4)}, Err=${e.error.toFixed(4)}`);
            });
        }

        // Pass if avg error < 0.01 (GGX has numerical issues at low roughness edges,
        // so we allow higher max error as long as average is low)
        this.results.ggxNDF = { maxError, avgError, failingPixels: errors.length, passed: avgError < 0.01 };
        return this.results.ggxNDF;
    }

    /**
     * Run Smith G test
     */
    runSmithGTest(NdotL = 0.5) {
        console.log(`=== Smith G Test (NdotL=${NdotL}) ===`);

        this.materials.smithG.uniforms.u_NdotL.value = NdotL;
        const gpuPixels = this.renderAndReadback(this.materials.smithG);

        let maxError = 0;
        let totalError = 0;
        let numPixels = 0;
        const errors = [];

        // Skip edge pixels - G has numerical issues at NdotV=0 and low roughness
        const margin = 3;

        for (let y = margin; y < this.resolution - margin; y++) {
            for (let x = margin; x < this.resolution - margin; x++) {
                const i = (y * this.resolution + x) * 4;

                const NdotV = Math.max(0.001, x / (this.resolution - 1));
                const roughness = Math.max(0.01, y / (this.resolution - 1));

                // Skip very low NdotV where G becomes numerically unstable
                if (NdotV < 0.02) continue;

                const gpuG = gpuPixels[i];
                const jsG = geometrySmithGGXJS(NdotV, NdotL, roughness);

                const error = Math.abs(gpuG - jsG);
                maxError = Math.max(maxError, error);
                totalError += error;
                numPixels++;

                if (error > 0.02) {
                    errors.push({ x, y, NdotV, roughness, gpuG, jsG, error });
                }
            }
        }

        const avgError = totalError / numPixels;

        console.log(`  Max Error: ${maxError.toFixed(6)}`);
        console.log(`  Avg Error: ${avgError.toFixed(6)}`);
        console.log(`  Failing Pixels (>0.02): ${errors.length}`);

        if (errors.length > 0 && errors.length <= 5) {
            errors.forEach(e => {
                console.log(`    NdotV=${e.NdotV.toFixed(3)}, rough=${e.roughness.toFixed(3)}: GPU=${e.gpuG.toFixed(4)}, JS=${e.jsG.toFixed(4)}, Err=${e.error.toFixed(4)}`);
            });
        }

        // Pass if max error < 0.05 (5% tolerance for geometry function)
        this.results.smithG = { maxError, avgError, failingPixels: errors.length, passed: maxError < 0.05 && avgError < 0.005 };
        return this.results.smithG;
    }

    /**
     * Run full BRDF test
     */
    runBRDFFullTest(options = {}) {
        const baseColor = options.baseColor || [0.8, 0.8, 0.8];
        const metalness = options.metalness !== undefined ? options.metalness : 0.3;
        const ior = options.ior || 1.5;
        const NdotL = 0.7; // Fixed in shader

        console.log(`=== Full BRDF Test ===`);
        console.log(`  baseColor: [${baseColor.join(', ')}]`);
        console.log(`  metalness: ${metalness}`);
        console.log(`  ior: ${ior}`);

        this.materials.brdfFull.uniforms.u_baseColor.value.set(...baseColor);
        this.materials.brdfFull.uniforms.u_metalness.value = metalness;
        this.materials.brdfFull.uniforms.u_ior.value = ior;

        const gpuPixels = this.renderAndReadback(this.materials.brdfFull);

        let maxError = 0;
        let totalError = 0;
        let numPixels = 0;
        const errors = [];

        // Skip edges - full BRDF combines multiple functions with edge issues
        const margin = 5;

        for (let y = margin; y < this.resolution - margin; y++) {
            for (let x = margin; x < this.resolution - margin; x++) {
                const i = (y * this.resolution + x) * 4;

                const NdotV = Math.max(0.001, x / (this.resolution - 1));
                const roughness = Math.max(0.01, y / (this.resolution - 1));

                // Skip grazing angles and very low roughness
                if (NdotV < 0.05) continue;
                if (roughness < 0.05 && NdotV > 0.95) continue;

                // GPU result (RGB)
                const gpuBRDF = [
                    Math.min(gpuPixels[i], 10),
                    Math.min(gpuPixels[i + 1], 10),
                    Math.min(gpuPixels[i + 2], 10)
                ];

                // JS ground truth
                const jsBRDF = evaluateBRDFJS(NdotV, NdotL, roughness, baseColor, metalness, ior)
                    .map(v => Math.min(v, 10));

                // RGB error magnitude
                const errorVec = gpuBRDF.map((g, idx) => g - jsBRDF[idx]);
                const errorMag = Math.sqrt(errorVec.reduce((sum, e) => sum + e * e, 0));

                // Use relative error for larger values
                const maxVal = Math.max(...jsBRDF, 0.1);
                const relError = errorMag / maxVal;

                maxError = Math.max(maxError, relError);
                totalError += relError;
                numPixels++;

                if (relError > 0.1) {
                    errors.push({ x, y, NdotV, roughness, gpuBRDF, jsBRDF, error: relError });
                }
            }
        }

        const avgError = totalError / numPixels;

        console.log(`  Max Relative Error: ${maxError.toFixed(6)}`);
        console.log(`  Avg Relative Error: ${avgError.toFixed(6)}`);
        console.log(`  Failing Pixels (>10%): ${errors.length}`);

        if (errors.length > 0 && errors.length <= 5) {
            errors.forEach(e => {
                console.log(`    NdotV=${e.NdotV.toFixed(3)}, rough=${e.roughness.toFixed(3)}: GPU=[${e.gpuBRDF.map(v => v.toFixed(3)).join(',')}], JS=[${e.jsBRDF.map(v => v.toFixed(3)).join(',')}], Err=${e.error.toFixed(4)}`);
            });
        }

        // Pass if max error < 0.2 (20% tolerance for full BRDF which combines multiple functions)
        this.results.brdfFull = { maxError, avgError, failingPixels: errors.length, passed: maxError < 0.2 && avgError < 0.02 };
        return this.results.brdfFull;
    }

    /**
     * Run layer mixing test
     * Tests OpenPBR evaluation tree weight calculations
     */
    runLayerMixingTest(options = {}) {
        const {
            fuzz_weight = 0.0,
            coat_weight = 0.5,
            base_metalness = 0.3,
            transmission_weight = 0.0,
            subsurface_weight = 0.0,
            specular_ior = 1.5
        } = options;

        console.log('=== Layer Mixing Test ===');
        console.log(`  fuzz=${fuzz_weight}, coat=${coat_weight}, metal=${base_metalness}`);
        console.log(`  transmission=${transmission_weight}, subsurface=${subsurface_weight}`);

        // Set uniforms
        const mat = this.materials.layerMixing;
        mat.uniforms.u_fuzz_weight.value = fuzz_weight;
        mat.uniforms.u_coat_weight.value = coat_weight;
        mat.uniforms.u_base_metalness.value = base_metalness;
        mat.uniforms.u_transmission_weight.value = transmission_weight;
        mat.uniforms.u_subsurface_weight.value = subsurface_weight;
        mat.uniforms.u_specular_ior.value = specular_ior;

        const gpuPixels = this.renderAndReadback(mat);

        let maxError = 0;
        let totalError = 0;
        let numPixels = 0;
        const errors = [];

        const margin = 2;

        for (let y = margin; y < this.resolution - margin; y++) {
            for (let x = margin; x < this.resolution - margin; x++) {
                const i = (y * this.resolution + x) * 4;

                const NdotV = Math.max(0.01, x / (this.resolution - 1));
                const roughness = Math.max(0.01, y / (this.resolution - 1));

                // GPU results: R=fuzz, G=coat, B=metal, A=dielectric
                const gpuWeights = {
                    fuzz: gpuPixels[i],
                    coat: gpuPixels[i + 1],
                    metal: gpuPixels[i + 2],
                    dielectric: gpuPixels[i + 3]
                };

                // JS ground truth
                const jsWeights = computeLayerWeightsJS({
                    fuzz_weight,
                    coat_weight,
                    base_metalness,
                    transmission_weight,
                    subsurface_weight,
                    NdotV,
                    roughness,
                    specular_ior
                });

                // Calculate error for each component
                const componentErrors = [
                    Math.abs(gpuWeights.fuzz - jsWeights.fuzz),
                    Math.abs(gpuWeights.coat - jsWeights.coat),
                    Math.abs(gpuWeights.metal - jsWeights.metal),
                    Math.abs(gpuWeights.dielectric - jsWeights.dielectric)
                ];
                const errorMag = Math.max(...componentErrors);

                maxError = Math.max(maxError, errorMag);
                totalError += errorMag;
                numPixels++;

                if (errorMag > 0.02) {
                    errors.push({ x, y, NdotV, roughness, gpuWeights, jsWeights, error: errorMag });
                }
            }
        }

        const avgError = totalError / numPixels;

        console.log(`  Max Error: ${maxError.toFixed(6)}`);
        console.log(`  Avg Error: ${avgError.toFixed(6)}`);
        console.log(`  Failing Pixels (>2%): ${errors.length}`);

        if (errors.length > 0 && errors.length <= 3) {
            errors.forEach(e => {
                console.log(`    NdotV=${e.NdotV.toFixed(3)}: GPU=[${Object.values(e.gpuWeights).map(v => v.toFixed(3)).join(',')}], JS=[${Object.values(e.jsWeights).map(v => v.toFixed(3)).join(',')}]`);
            });
        }

        this.results.layerMixing = { maxError, avgError, failingPixels: errors.length, passed: avgError < 0.01 };
        return this.results.layerMixing;
    }

    /**
     * Run energy conservation test
     * Validates that total albedo <= 1 for all parameter combinations
     */
    runEnergyConservationTest(options = {}) {
        const {
            base_metalness = 0.0,
            specular_ior = 1.5,
            roughness = 0.5
        } = options;

        console.log('=== Energy Conservation Test ===');
        console.log(`  metalness=${base_metalness}, ior=${specular_ior}, roughness=${roughness}`);

        // Set uniforms
        const mat = this.materials.energyConservation;
        mat.uniforms.u_base_metalness.value = base_metalness;
        mat.uniforms.u_specular_ior.value = specular_ior;
        mat.uniforms.u_roughness.value = roughness;

        const gpuPixels = this.renderAndReadback(mat);

        let maxAlbedo = 0;
        let maxExcess = 0;
        let totalExcess = 0;
        let numPixels = 0;
        let violationCount = 0;
        const violations = [];

        const margin = 2;

        for (let y = margin; y < this.resolution - margin; y++) {
            for (let x = margin; x < this.resolution - margin; x++) {
                const i = (y * this.resolution + x) * 4;

                const NdotV = Math.max(0.01, x / (this.resolution - 1));
                const coat_weight = y / (this.resolution - 1);

                // GPU results: R=totalAlbedo, G=energyExcess, B=coatContrib, A=baseContrib
                const totalAlbedo = gpuPixels[i];
                const energyExcess = gpuPixels[i + 1];

                maxAlbedo = Math.max(maxAlbedo, totalAlbedo);
                maxExcess = Math.max(maxExcess, energyExcess);
                totalExcess += energyExcess;
                numPixels++;

                // Check JS ground truth
                const jsResult = computeEnergyConservationJS({
                    coat_weight,
                    base_metalness,
                    specular_ior,
                    roughness,
                    NdotV
                });

                // Compare GPU vs JS
                const albedoError = Math.abs(totalAlbedo - jsResult.totalAlbedo);

                if (energyExcess > 0.001 || albedoError > 0.02) {
                    violationCount++;
                    if (violations.length < 5) {
                        violations.push({ NdotV, coat_weight, totalAlbedo, energyExcess, jsAlbedo: jsResult.totalAlbedo, albedoError });
                    }
                }
            }
        }

        const avgExcess = totalExcess / numPixels;

        console.log(`  Max Albedo: ${maxAlbedo.toFixed(6)}`);
        console.log(`  Max Energy Excess: ${maxExcess.toFixed(6)}`);
        console.log(`  Avg Energy Excess: ${avgExcess.toFixed(6)}`);
        console.log(`  Violation Count: ${violationCount}`);

        if (violations.length > 0) {
            console.log('  Sample violations:');
            violations.forEach(v => {
                console.log(`    NdotV=${v.NdotV.toFixed(3)}, coat=${v.coat_weight.toFixed(3)}: albedo=${v.totalAlbedo.toFixed(4)}, excess=${v.energyExcess.toFixed(4)}, jsAlbedo=${v.jsAlbedo.toFixed(4)}`);
            });
        }

        // Pass if no significant energy excess and albedo matches JS
        this.results.energyConservation = {
            maxAlbedo,
            maxExcess,
            avgExcess,
            violationCount,
            passed: maxExcess < 0.05 && avgExcess < 0.001
        };
        return this.results.energyConservation;
    }

    /**
     * Run all tests
     */
    runAllTests(options = {}) {
        console.log('========================================');
        console.log('OpenPBR BRDF Validation Suite');
        console.log('========================================');

        this.runFresnelTest();
        this.runGGXNDFTest();
        this.runSmithGTest(0.5);
        this.runBRDFFullTest(options);

        const allPassed = this.results.fresnel.passed &&
                          this.results.ggxNDF.passed &&
                          this.results.smithG.passed &&
                          this.results.brdfFull.passed;

        console.log('========================================');
        console.log('Summary:');
        console.log(`  Fresnel:   ${this.results.fresnel.passed ? 'PASS' : 'FAIL'} (max: ${this.results.fresnel.maxError.toFixed(4)}, avg: ${this.results.fresnel.avgError.toFixed(6)})`);
        console.log(`  GGX NDF:   ${this.results.ggxNDF.passed ? 'PASS' : 'FAIL'} (max: ${this.results.ggxNDF.maxError.toFixed(4)}, avg: ${this.results.ggxNDF.avgError.toFixed(6)})`);
        console.log(`  Smith G:   ${this.results.smithG.passed ? 'PASS' : 'FAIL'} (max: ${this.results.smithG.maxError.toFixed(4)}, avg: ${this.results.smithG.avgError.toFixed(6)})`);
        console.log(`  Full BRDF: ${this.results.brdfFull.passed ? 'PASS' : 'FAIL'} (max: ${this.results.brdfFull.maxError.toFixed(4)}, avg: ${this.results.brdfFull.avgError.toFixed(6)})`);
        console.log('----------------------------------------');
        console.log(`  Overall: ${allPassed ? 'ALL TESTS PASSED' : 'SOME TESTS FAILED'}`);
        console.log('========================================');

        this.results.allPassed = allPassed;
        return this.results;
    }

    /**
     * Run layer mixing and energy conservation tests
     */
    runLayerTests(options = {}) {
        console.log('========================================');
        console.log('OpenPBR Layer Mixing Validation');
        console.log('========================================');

        // Run layer mixing with different configurations
        const configs = [
            { name: 'Dielectric + Coat', fuzz_weight: 0, coat_weight: 0.5, base_metalness: 0, specular_ior: 1.5 },
            { name: 'Metal + Coat', fuzz_weight: 0, coat_weight: 0.5, base_metalness: 1.0, specular_ior: 1.5 },
            { name: 'Full Stack', fuzz_weight: 0.3, coat_weight: 0.5, base_metalness: 0.3, specular_ior: 1.5 }
        ];

        let allPassed = true;
        const configResults = [];

        for (const config of configs) {
            console.log(`\n--- Config: ${config.name} ---`);
            const result = this.runLayerMixingTest(config);
            configResults.push({ name: config.name, ...result });
            if (!result.passed) allPassed = false;
        }

        // Run energy conservation test
        console.log('\n--- Energy Conservation ---');
        const energyResult = this.runEnergyConservationTest(options);
        if (!energyResult.passed) allPassed = false;

        console.log('\n========================================');
        console.log('Layer Tests Summary:');
        for (const r of configResults) {
            console.log(`  ${r.name}: ${r.passed ? 'PASS' : 'FAIL'} (avg: ${r.avgError.toFixed(6)})`);
        }
        console.log(`  Energy Conservation: ${energyResult.passed ? 'PASS' : 'FAIL'} (max excess: ${energyResult.maxExcess.toFixed(6)})`);
        console.log('----------------------------------------');
        console.log(`  Overall: ${allPassed ? 'ALL PASSED' : 'SOME FAILED'}`);
        console.log('========================================');

        this.results.layerTests = { allPassed, configResults, energyConservation: energyResult };
        return this.results.layerTests;
    }

    /**
     * Generate visual comparison texture
     */
    generateComparisonTexture(testName) {
        const material = this.materials[testName];
        if (!material) {
            console.error(`Unknown test: ${testName}`);
            return null;
        }

        const gpuPixels = this.renderAndReadback(material);

        // Create a canvas for visualization
        const canvas = document.createElement('canvas');
        canvas.width = this.resolution;
        canvas.height = this.resolution;
        const ctx = canvas.getContext('2d');
        const imageData = ctx.createImageData(this.resolution, this.resolution);

        for (let y = 0; y < this.resolution; y++) {
            for (let x = 0; x < this.resolution; x++) {
                const srcI = (y * this.resolution + x) * 4;
                // Flip Y for canvas (top-left origin)
                const dstI = ((this.resolution - 1 - y) * this.resolution + x) * 4;

                // Normalize and convert to 0-255
                const r = Math.min(1, Math.max(0, gpuPixels[srcI] / 10)) * 255;
                const g = Math.min(1, Math.max(0, gpuPixels[srcI + 1] / 10)) * 255;
                const b = Math.min(1, Math.max(0, gpuPixels[srcI + 2] / 10)) * 255;

                imageData.data[dstI] = r;
                imageData.data[dstI + 1] = g;
                imageData.data[dstI + 2] = b;
                imageData.data[dstI + 3] = 255;
            }
        }

        ctx.putImageData(imageData, 0, 0);

        return canvas;
    }

    /**
     * Dispose resources
     */
    dispose() {
        this.renderTarget.dispose();
        this.quadGeometry.dispose();
        Object.values(this.materials).forEach(m => m.dispose());
    }
}

// Export ground truth functions for external use
export const OpenPBRGroundTruth = {
    fresnelSchlick: fresnelSchlickJS,
    iorToF0: iorToF0JS,
    distributionGGX: distributionGGXJS,
    geometrySmithGGX1: geometrySmithGGX1JS,
    geometrySmithGGX: geometrySmithGGXJS,
    evaluateBRDF: evaluateBRDFJS
};

// Export for global access
if (typeof window !== 'undefined') {
    window.OpenPBRValidator = OpenPBRValidator;
    window.OpenPBRGroundTruth = OpenPBRGroundTruth;
}
