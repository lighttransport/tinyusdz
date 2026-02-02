/**
 * Extended Skinning Support for Three.js
 *
 * Supports configurable bone influences per vertex:
 * - 4 bones: Standard Three.js (skinIndex, skinWeight)
 * - 8 bones: Extended attributes (skinIndex, skinWeight, skinIndex2, skinWeight2)
 * - 16/32/64+ bones: Texture-based storage (unlimited scalability)
 *
 * Based on approaches from: https://github.com/mrdoob/three.js/issues/26137
 *
 * @module ExtendedSkinning
 */

import * as THREE from 'three';

/**
 * Skinning modes based on bone influence count
 */
export const SkinningMode = {
    STANDARD: 4,      // 4 bones - standard Three.js
    EXTENDED_8: 8,    // 8 bones - extra vertex attributes
    TEXTURE_16: 16,   // 16 bones - texture-based
    TEXTURE_32: 32,   // 32 bones - texture-based
    TEXTURE_48: 48,   // 48 bones - texture-based
    TEXTURE_64: 64,   // 64 bones - texture-based
    TEXTURE_80: 80,   // 80 bones - texture-based
    TEXTURE_96: 96,   // 96 bones - texture-based
    TEXTURE_128: 128  // 128 bones - texture-based (max practical)
};

/**
 * Determine the best skinning mode for given influences per vertex
 * @param {number} influencesPerVertex - Number of bone influences per vertex
 * @returns {number} Recommended skinning mode
 */
export function getSkinningMode(influencesPerVertex) {
    if (influencesPerVertex <= 4) return SkinningMode.STANDARD;
    if (influencesPerVertex <= 8) return SkinningMode.EXTENDED_8;
    if (influencesPerVertex <= 16) return SkinningMode.TEXTURE_16;
    if (influencesPerVertex <= 32) return SkinningMode.TEXTURE_32;
    if (influencesPerVertex <= 48) return SkinningMode.TEXTURE_48;
    if (influencesPerVertex <= 64) return SkinningMode.TEXTURE_64;
    if (influencesPerVertex <= 80) return SkinningMode.TEXTURE_80;
    if (influencesPerVertex <= 96) return SkinningMode.TEXTURE_96;
    return SkinningMode.TEXTURE_128;
}

/**
 * Extended skinning configuration for a mesh
 */
export class ExtendedSkinningConfig {
    constructor(options = {}) {
        this.maxInfluences = options.maxInfluences || 4;
        this.mode = getSkinningMode(this.maxInfluences);
        this.normalizeWeights = options.normalizeWeights !== false;

        // Texture-based storage (for 16+ bones)
        this.boneDataTexture = null;
        this.vertexBoneOffsets = null;
    }
}

/**
 * Add extended skinning attributes to geometry
 * Automatically selects the best method based on influence count
 *
 * @param {THREE.BufferGeometry} geometry - Target geometry
 * @param {Uint16Array|Int32Array} jointIndices - Flat array of joint indices
 * @param {Float32Array} jointWeights - Flat array of joint weights
 * @param {number} influencesPerVertex - Number of bone influences per vertex in source data
 * @param {Object} [options] - Options
 * @param {boolean} [options.normalize=true] - Normalize weights to sum to 1
 * @param {number} [options.forceMode] - Force a specific skinning mode
 * @returns {ExtendedSkinningConfig} Configuration object with skinning setup info
 */
export function addExtendedSkinningAttributes(geometry, jointIndices, jointWeights, influencesPerVertex, options = {}) {
    const normalize = options.normalize !== false;
    const vertexCount = geometry.attributes.position.count;
    const mode = options.forceMode || getSkinningMode(influencesPerVertex);

    const config = new ExtendedSkinningConfig({
        maxInfluences: influencesPerVertex,
        normalizeWeights: normalize
    });

    if (mode <= SkinningMode.EXTENDED_8) {
        // Use vertex attribute-based skinning (4 or 8 bones)
        addAttributeBasedSkinning(geometry, jointIndices, jointWeights, influencesPerVertex, mode, normalize);
    } else {
        // Use texture-based skinning (16+ bones)
        const textureData = addTextureBasedSkinning(geometry, jointIndices, jointWeights, influencesPerVertex, mode, normalize);
        config.boneDataTexture = textureData.texture;
        config.vertexBoneOffsets = textureData.offsets;
    }

    // Store config in geometry userData
    geometry.userData.extendedSkinning = config;

    return config;
}

/**
 * Add attribute-based skinning (4 or 8 bones)
 */
function addAttributeBasedSkinning(geometry, jointIndices, jointWeights, influencesPerVertex, mode, normalize) {
    const vertexCount = geometry.attributes.position.count;
    const useExtended = mode === SkinningMode.EXTENDED_8;

    // Primary skinning attributes (bones 0-3)
    const skinIndices = new Uint16Array(vertexCount * 4);
    const skinWeights = new Float32Array(vertexCount * 4);

    // Extended skinning attributes (bones 4-7)
    let skinIndices2 = null;
    let skinWeights2 = null;
    if (useExtended) {
        skinIndices2 = new Uint16Array(vertexCount * 4);
        skinWeights2 = new Float32Array(vertexCount * 4);
    }

    const sourceVertexCount = Math.floor(jointIndices.length / influencesPerVertex);
    const effectiveVertexCount = Math.min(vertexCount, sourceVertexCount);

    for (let i = 0; i < effectiveVertexCount; i++) {
        let totalWeight = 0;

        // First 4 bones
        for (let j = 0; j < 4; j++) {
            const srcIdx = i * influencesPerVertex + j;
            const dstIdx = i * 4 + j;

            if (j < influencesPerVertex && srcIdx < jointIndices.length) {
                skinIndices[dstIdx] = jointIndices[srcIdx];
                skinWeights[dstIdx] = jointWeights[srcIdx];
                totalWeight += jointWeights[srcIdx];
            } else {
                skinIndices[dstIdx] = 0;
                skinWeights[dstIdx] = 0;
            }
        }

        // Extended bones (4-7)
        if (useExtended) {
            for (let j = 0; j < 4; j++) {
                const srcIdx = i * influencesPerVertex + (j + 4);
                const dstIdx = i * 4 + j;

                if ((j + 4) < influencesPerVertex && srcIdx < jointIndices.length) {
                    skinIndices2[dstIdx] = jointIndices[srcIdx];
                    skinWeights2[dstIdx] = jointWeights[srcIdx];
                    totalWeight += jointWeights[srcIdx];
                } else {
                    skinIndices2[dstIdx] = 0;
                    skinWeights2[dstIdx] = 0;
                }
            }
        }

        // Normalize weights
        if (normalize && totalWeight > 0 && Math.abs(totalWeight - 1.0) > 0.001) {
            const invTotal = 1.0 / totalWeight;
            for (let j = 0; j < 4; j++) {
                skinWeights[i * 4 + j] *= invTotal;
            }
            if (useExtended) {
                for (let j = 0; j < 4; j++) {
                    skinWeights2[i * 4 + j] *= invTotal;
                }
            }
        }
    }

    // Fill remaining vertices with defaults
    for (let i = effectiveVertexCount; i < vertexCount; i++) {
        for (let j = 0; j < 4; j++) {
            const dstIdx = i * 4 + j;
            skinIndices[dstIdx] = 0;
            skinWeights[dstIdx] = j === 0 ? 1 : 0;
            if (useExtended) {
                skinIndices2[dstIdx] = 0;
                skinWeights2[dstIdx] = 0;
            }
        }
    }

    // Add attributes to geometry
    geometry.setAttribute('skinIndex', new THREE.Uint16BufferAttribute(skinIndices, 4));
    geometry.setAttribute('skinWeight', new THREE.Float32BufferAttribute(skinWeights, 4));

    if (useExtended) {
        geometry.setAttribute('skinIndex2', new THREE.Uint16BufferAttribute(skinIndices2, 4));
        geometry.setAttribute('skinWeight2', new THREE.Float32BufferAttribute(skinWeights2, 4));
    }

    console.log(`Added ${useExtended ? '8-bone' : '4-bone'} attribute-based skinning`);
}

/**
 * Add texture-based skinning (16+ bones)
 * Stores bone indices and weights in a texture for unlimited scalability
 */
function addTextureBasedSkinning(geometry, jointIndices, jointWeights, influencesPerVertex, mode, normalize) {
    const vertexCount = geometry.attributes.position.count;
    const maxInfluences = mode; // 16, 32, 64, etc.

    // Calculate texture dimensions
    // Each vertex needs maxInfluences entries, each entry is (boneIndex, weight) = 2 floats
    // Pack as RGBA: (boneIndex0, weight0, boneIndex1, weight1) per texel = 2 influences per texel
    const influencesPerTexel = 2;
    const texelsPerVertex = Math.ceil(maxInfluences / influencesPerTexel);
    const totalTexels = vertexCount * texelsPerVertex;

    // Find optimal texture dimensions (power of 2 preferred)
    const texWidth = Math.min(4096, Math.pow(2, Math.ceil(Math.log2(Math.sqrt(totalTexels)))));
    const texHeight = Math.ceil(totalTexels / texWidth);

    // Create texture data (RGBA float)
    const textureData = new Float32Array(texWidth * texHeight * 4);

    // Fill texture with bone data
    const sourceVertexCount = Math.floor(jointIndices.length / influencesPerVertex);

    // Also create standard 4-bone attributes for fallback/compatibility
    const skinIndices = new Uint16Array(vertexCount * 4);
    const skinWeights = new Float32Array(vertexCount * 4);

    // Vertex offsets into texture (for shader lookup)
    const vertexBoneOffsets = new Float32Array(vertexCount);

    for (let v = 0; v < vertexCount; v++) {
        const texelOffset = v * texelsPerVertex;
        vertexBoneOffsets[v] = texelOffset;

        let totalWeight = 0;
        const vertexInfluences = [];

        // Collect all influences for this vertex
        for (let j = 0; j < maxInfluences; j++) {
            const srcIdx = v * influencesPerVertex + j;

            if (v < sourceVertexCount && j < influencesPerVertex && srcIdx < jointIndices.length) {
                const boneIdx = jointIndices[srcIdx];
                const weight = jointWeights[srcIdx];

                if (weight > 0) {
                    vertexInfluences.push({ boneIdx, weight });
                    totalWeight += weight;
                }
            }
        }

        // Normalize if needed
        if (normalize && totalWeight > 0 && Math.abs(totalWeight - 1.0) > 0.001) {
            const invTotal = 1.0 / totalWeight;
            for (const inf of vertexInfluences) {
                inf.weight *= invTotal;
            }
        }

        // Sort by weight (highest first) for potential early termination in shader
        vertexInfluences.sort((a, b) => b.weight - a.weight);

        // Write to texture (2 influences per texel: R=bone0, G=weight0, B=bone1, A=weight1)
        for (let t = 0; t < texelsPerVertex; t++) {
            const texelIdx = (texelOffset + t) * 4;
            const inf0 = vertexInfluences[t * 2] || { boneIdx: -1, weight: 0 };
            const inf1 = vertexInfluences[t * 2 + 1] || { boneIdx: -1, weight: 0 };

            textureData[texelIdx + 0] = inf0.boneIdx; // R: bone index 0
            textureData[texelIdx + 1] = inf0.weight;  // G: weight 0
            textureData[texelIdx + 2] = inf1.boneIdx; // B: bone index 1
            textureData[texelIdx + 3] = inf1.weight;  // A: weight 1
        }

        // Also fill standard 4-bone attributes (for compatibility and weight visualization)
        for (let j = 0; j < 4; j++) {
            const inf = vertexInfluences[j] || { boneIdx: 0, weight: 0 };
            skinIndices[v * 4 + j] = inf.boneIdx >= 0 ? inf.boneIdx : 0;
            skinWeights[v * 4 + j] = inf.boneIdx >= 0 ? inf.weight : (j === 0 && vertexInfluences.length === 0 ? 1 : 0);
        }
    }

    // Create Three.js texture
    const texture = new THREE.DataTexture(
        textureData,
        texWidth,
        texHeight,
        THREE.RGBAFormat,
        THREE.FloatType
    );
    texture.needsUpdate = true;
    texture.minFilter = THREE.NearestFilter;
    texture.magFilter = THREE.NearestFilter;

    // Add attributes
    geometry.setAttribute('skinIndex', new THREE.Uint16BufferAttribute(skinIndices, 4));
    geometry.setAttribute('skinWeight', new THREE.Float32BufferAttribute(skinWeights, 4));
    geometry.setAttribute('boneDataOffset', new THREE.Float32BufferAttribute(vertexBoneOffsets, 1));

    console.log(`Added ${mode}-bone texture-based skinning (texture: ${texWidth}x${texHeight})`);

    return {
        texture,
        offsets: vertexBoneOffsets,
        texWidth,
        texHeight,
        texelsPerVertex,
        maxInfluences: mode
    };
}

/**
 * Create an extended skinning material that supports configurable bone counts
 *
 * @param {THREE.Material} baseMaterial - Base material to extend
 * @param {Object} [options] - Options
 * @param {number} [options.maxInfluences=4] - Maximum bone influences (4, 8, 16, 32, 64)
 * @param {THREE.DataTexture} [options.boneDataTexture] - Texture for 16+ bone mode
 * @param {number} [options.texelsPerVertex] - Texels per vertex in bone data texture
 * @param {number} [options.boneDataTexWidth] - Bone data texture width
 * @returns {THREE.Material} Material with extended skinning support
 */
export function createExtendedSkinningMaterial(baseMaterial, options = {}) {
    const maxInfluences = options.maxInfluences || 4;
    const mode = getSkinningMode(maxInfluences);

    // Clone the base material
    const material = baseMaterial.clone();

    material.onBeforeCompile = (shader) => {
        if (mode === SkinningMode.EXTENDED_8) {
            // 8-bone attribute-based skinning
            apply8BoneShaderMod(shader);
        } else if (mode >= SkinningMode.TEXTURE_16) {
            // 16+ bone texture-based skinning
            applyTextureBoneShaderMod(shader, options);
        }
    };

    material.needsUpdate = true;
    return material;
}

/**
 * Apply 8-bone shader modifications
 */
function apply8BoneShaderMod(shader) {
    // Add extended skinning attribute declarations
    shader.vertexShader = shader.vertexShader.replace(
        '#include <skinning_pars_vertex>',
        `#include <skinning_pars_vertex>

        #ifdef USE_SKINNING
            #define USE_EXTENDED_SKINNING_8
            attribute vec4 skinIndex2;
            attribute vec4 skinWeight2;
        #endif
        `
    );

    // Modify skinbase_vertex to get extended bone matrices
    shader.vertexShader = shader.vertexShader.replace(
        '#include <skinbase_vertex>',
        `#include <skinbase_vertex>

        #ifdef USE_EXTENDED_SKINNING_8
            mat4 boneMatX2 = getBoneMatrix( skinIndex2.x );
            mat4 boneMatY2 = getBoneMatrix( skinIndex2.y );
            mat4 boneMatZ2 = getBoneMatrix( skinIndex2.z );
            mat4 boneMatW2 = getBoneMatrix( skinIndex2.w );
        #endif
        `
    );

    // Modify skinning_vertex
    shader.vertexShader = shader.vertexShader.replace(
        '#include <skinning_vertex>',
        `#ifdef USE_SKINNING

            vec4 skinVertex = bindMatrix * vec4( transformed, 1.0 );

            vec4 skinned = vec4( 0.0 );
            skinned += boneMatX * skinVertex * skinWeight.x;
            skinned += boneMatY * skinVertex * skinWeight.y;
            skinned += boneMatZ * skinVertex * skinWeight.z;
            skinned += boneMatW * skinVertex * skinWeight.w;

            #ifdef USE_EXTENDED_SKINNING_8
            skinned += boneMatX2 * skinVertex * skinWeight2.x;
            skinned += boneMatY2 * skinVertex * skinWeight2.y;
            skinned += boneMatZ2 * skinVertex * skinWeight2.z;
            skinned += boneMatW2 * skinVertex * skinWeight2.w;
            #endif

            transformed = ( bindMatrixInverse * skinned ).xyz;

        #endif
        `
    );

    // Modify skinnormal_vertex
    shader.vertexShader = shader.vertexShader.replace(
        '#include <skinnormal_vertex>',
        `#ifdef USE_SKINNING

            mat4 skinMatrix = mat4( 0.0 );
            skinMatrix += skinWeight.x * boneMatX;
            skinMatrix += skinWeight.y * boneMatY;
            skinMatrix += skinWeight.z * boneMatZ;
            skinMatrix += skinWeight.w * boneMatW;

            #ifdef USE_EXTENDED_SKINNING_8
            skinMatrix += skinWeight2.x * boneMatX2;
            skinMatrix += skinWeight2.y * boneMatY2;
            skinMatrix += skinWeight2.z * boneMatZ2;
            skinMatrix += skinWeight2.w * boneMatW2;
            #endif

            skinMatrix = bindMatrixInverse * skinMatrix * bindMatrix;

            objectNormal = vec4( skinMatrix * vec4( objectNormal, 0.0 ) ).xyz;

            #ifdef USE_TANGENT
                objectTangent = vec4( skinMatrix * vec4( objectTangent, 0.0 ) ).xyz;
            #endif

        #endif
        `
    );
}

/**
 * Apply texture-based bone shader modifications (16+ bones)
 */
function applyTextureBoneShaderMod(shader, options) {
    const maxInfluences = options.maxInfluences || 16;
    const texelsPerVertex = options.texelsPerVertex || Math.ceil(maxInfluences / 2);
    const texWidth = options.boneDataTexWidth || 1024;

    // Add uniforms
    shader.uniforms.boneDataTexture = { value: options.boneDataTexture };
    shader.uniforms.boneDataTexWidth = { value: texWidth };
    shader.uniforms.texelsPerVertex = { value: texelsPerVertex };

    // Add texture-based skinning declarations
    shader.vertexShader = shader.vertexShader.replace(
        '#include <skinning_pars_vertex>',
        `#include <skinning_pars_vertex>

        #ifdef USE_SKINNING
            #define USE_TEXTURE_SKINNING
            #define MAX_TEXTURE_INFLUENCES ${maxInfluences}
            #define TEXELS_PER_VERTEX ${texelsPerVertex}

            uniform sampler2D boneDataTexture;
            uniform float boneDataTexWidth;
            uniform float texelsPerVertex;

            attribute float boneDataOffset;

            // Read bone index and weight from texture
            vec2 getBoneData(float texelIndex) {
                float x = mod(texelIndex, boneDataTexWidth);
                float y = floor(texelIndex / boneDataTexWidth);
                vec2 uv = (vec2(x, y) + 0.5) / vec2(boneDataTexWidth, textureSize(boneDataTexture, 0).y);
                return texture2D(boneDataTexture, uv).rg; // r=boneIndex, g=weight
            }

            vec2 getBoneData2(float texelIndex) {
                float x = mod(texelIndex, boneDataTexWidth);
                float y = floor(texelIndex / boneDataTexWidth);
                vec2 uv = (vec2(x, y) + 0.5) / vec2(boneDataTexWidth, textureSize(boneDataTexture, 0).y);
                return texture2D(boneDataTexture, uv).ba; // b=boneIndex, a=weight
            }
        #endif
        `
    );

    // Replace entire skinning calculation with texture-based version
    shader.vertexShader = shader.vertexShader.replace(
        '#include <skinbase_vertex>',
        `#ifdef USE_SKINNING
            #ifndef USE_TEXTURE_SKINNING
                // Standard 4-bone skinning
                mat4 boneMatX = getBoneMatrix( skinIndex.x );
                mat4 boneMatY = getBoneMatrix( skinIndex.y );
                mat4 boneMatZ = getBoneMatrix( skinIndex.z );
                mat4 boneMatW = getBoneMatrix( skinIndex.w );
            #endif
        #endif
        `
    );

    shader.vertexShader = shader.vertexShader.replace(
        '#include <skinning_vertex>',
        `#ifdef USE_SKINNING
            vec4 skinVertex = bindMatrix * vec4( transformed, 1.0 );
            vec4 skinned = vec4( 0.0 );

            #ifdef USE_TEXTURE_SKINNING
                // Texture-based skinning with ${maxInfluences} influences
                for (int t = 0; t < TEXELS_PER_VERTEX; t++) {
                    float texelIdx = boneDataOffset + float(t);

                    // First influence in texel (RG)
                    vec2 data0 = getBoneData(texelIdx);
                    if (data0.x >= 0.0 && data0.y > 0.0) {
                        mat4 boneMat = getBoneMatrix(data0.x);
                        skinned += boneMat * skinVertex * data0.y;
                    }

                    // Second influence in texel (BA)
                    vec2 data1 = getBoneData2(texelIdx);
                    if (data1.x >= 0.0 && data1.y > 0.0) {
                        mat4 boneMat = getBoneMatrix(data1.x);
                        skinned += boneMat * skinVertex * data1.y;
                    }
                }
            #else
                // Standard 4-bone skinning
                skinned += boneMatX * skinVertex * skinWeight.x;
                skinned += boneMatY * skinVertex * skinWeight.y;
                skinned += boneMatZ * skinVertex * skinWeight.z;
                skinned += boneMatW * skinVertex * skinWeight.w;
            #endif

            transformed = ( bindMatrixInverse * skinned ).xyz;
        #endif
        `
    );

    shader.vertexShader = shader.vertexShader.replace(
        '#include <skinnormal_vertex>',
        `#ifdef USE_SKINNING
            mat4 skinMatrix = mat4( 0.0 );

            #ifdef USE_TEXTURE_SKINNING
                // Texture-based normal skinning
                for (int t = 0; t < TEXELS_PER_VERTEX; t++) {
                    float texelIdx = boneDataOffset + float(t);

                    vec2 data0 = getBoneData(texelIdx);
                    if (data0.x >= 0.0 && data0.y > 0.0) {
                        skinMatrix += getBoneMatrix(data0.x) * data0.y;
                    }

                    vec2 data1 = getBoneData2(texelIdx);
                    if (data1.x >= 0.0 && data1.y > 0.0) {
                        skinMatrix += getBoneMatrix(data1.x) * data1.y;
                    }
                }
            #else
                // Standard 4-bone normal skinning
                skinMatrix += skinWeight.x * boneMatX;
                skinMatrix += skinWeight.y * boneMatY;
                skinMatrix += skinWeight.z * boneMatZ;
                skinMatrix += skinWeight.w * boneMatW;
            #endif

            skinMatrix = bindMatrixInverse * skinMatrix * bindMatrix;

            objectNormal = vec4( skinMatrix * vec4( objectNormal, 0.0 ) ).xyz;

            #ifdef USE_TANGENT
                objectTangent = vec4( skinMatrix * vec4( objectTangent, 0.0 ) ).xyz;
            #endif
        #endif
        `
    );
}

/**
 * Create an ExtendedSkinnedMesh class that handles arbitrary bone counts
 */
export class ExtendedSkinnedMesh extends THREE.SkinnedMesh {
    constructor(geometry, material, maxInfluences = 4) {
        super(geometry, material);

        this.maxInfluences = maxInfluences;
        this.skinningMode = getSkinningMode(maxInfluences);
        this.boneDataTexture = null;
        this.texelsPerVertex = 0;

        // If geometry has extended skinning config, use it
        if (geometry.userData.extendedSkinning) {
            const config = geometry.userData.extendedSkinning;
            this.maxInfluences = config.maxInfluences;
            this.skinningMode = config.mode;
            this.boneDataTexture = config.boneDataTexture;
        }
    }

    /**
     * Override bind to set up extended skinning material
     */
    bind(skeleton, bindMatrix) {
        super.bind(skeleton, bindMatrix);

        // Apply extended skinning material if needed
        if (this.skinningMode > SkinningMode.STANDARD) {
            this.material = createExtendedSkinningMaterial(this.material, {
                maxInfluences: this.maxInfluences,
                boneDataTexture: this.boneDataTexture,
                texelsPerVertex: this.texelsPerVertex,
                boneDataTexWidth: this.boneDataTexture ?
                    Math.sqrt(this.boneDataTexture.image.width * this.boneDataTexture.image.height) : 1024
            });
        }
    }

    /**
     * Set up skinning from USD data
     */
    setupFromUSD(jointIndices, jointWeights, influencesPerVertex, options = {}) {
        const config = addExtendedSkinningAttributes(
            this.geometry,
            jointIndices,
            jointWeights,
            influencesPerVertex,
            options
        );

        this.maxInfluences = config.maxInfluences;
        this.skinningMode = config.mode;
        this.boneDataTexture = config.boneDataTexture;

        return config;
    }
}

/**
 * Check the skinning mode of a geometry
 */
export function getGeometrySkinningMode(geometry) {
    if (geometry.userData.extendedSkinning) {
        return geometry.userData.extendedSkinning.mode;
    }
    if (geometry.attributes.skinIndex2) {
        return SkinningMode.EXTENDED_8;
    }
    if (geometry.attributes.skinIndex) {
        return SkinningMode.STANDARD;
    }
    return 0; // No skinning
}

/**
 * Check if geometry has extended skinning attributes
 */
export function hasExtendedSkinning(geometry) {
    return getGeometrySkinningMode(geometry) > SkinningMode.STANDARD;
}

/**
 * Apply extended skinning material to a SkinnedMesh if needed
 */
export function applyExtendedSkinningIfNeeded(skinnedMesh) {
    const mode = getGeometrySkinningMode(skinnedMesh.geometry);

    if (mode <= SkinningMode.STANDARD) {
        return false;
    }

    const config = skinnedMesh.geometry.userData.extendedSkinning || {};

    skinnedMesh.material = createExtendedSkinningMaterial(skinnedMesh.material, {
        maxInfluences: config.maxInfluences || mode,
        boneDataTexture: config.boneDataTexture,
        texelsPerVertex: config.texelsPerVertex,
        boneDataTexWidth: config.boneDataTexture?.image?.width || 1024
    });

    console.log(`Applied ${mode}-bone extended skinning material`);
    return true;
}

/**
 * Create extended weight visualization material (supports all modes)
 */
export function createExtendedWeightVisualizationMaterial(options = {}) {
    const maxInfluences = options.maxInfluences || 8;
    const mode = getSkinningMode(maxInfluences);

    const vertexShader = `
        #include <common>
        #include <skinning_pars_vertex>

        ${mode >= SkinningMode.EXTENDED_8 ? `
        attribute vec4 skinIndex2;
        attribute vec4 skinWeight2;
        ` : ''}

        ${mode >= SkinningMode.TEXTURE_16 ? `
        uniform sampler2D boneDataTexture;
        uniform float boneDataTexWidth;
        attribute float boneDataOffset;
        #define TEXELS_PER_VERTEX ${Math.ceil(maxInfluences / 2)}
        ` : ''}

        varying vec3 vColor;
        varying float vTotalWeight;
        varying float vInfluenceCount;

        vec3 getWeightColor(float weight, float index) {
            float hue = mod(index * 0.618033988749895, 1.0);
            float sat = 0.8;
            float val = weight;

            float h = hue * 6.0;
            float c = val * sat;
            float x = c * (1.0 - abs(mod(h, 2.0) - 1.0));
            float m = val - c;

            vec3 rgb;
            if (h < 1.0) rgb = vec3(c, x, 0.0);
            else if (h < 2.0) rgb = vec3(x, c, 0.0);
            else if (h < 3.0) rgb = vec3(0.0, c, x);
            else if (h < 4.0) rgb = vec3(0.0, x, c);
            else if (h < 5.0) rgb = vec3(x, 0.0, c);
            else rgb = vec3(c, 0.0, x);

            return rgb + m;
        }

        void main() {
            #include <skinbase_vertex>

            ${mode >= SkinningMode.EXTENDED_8 ? `
            mat4 boneMatX2 = getBoneMatrix( skinIndex2.x );
            mat4 boneMatY2 = getBoneMatrix( skinIndex2.y );
            mat4 boneMatZ2 = getBoneMatrix( skinIndex2.z );
            mat4 boneMatW2 = getBoneMatrix( skinIndex2.w );
            ` : ''}

            vColor = vec3(0.0);
            vTotalWeight = 0.0;
            vInfluenceCount = 0.0;

            // Standard 4 bones
            if (skinWeight.x > 0.0) { vColor += getWeightColor(skinWeight.x, skinIndex.x) * skinWeight.x; vTotalWeight += skinWeight.x; vInfluenceCount += 1.0; }
            if (skinWeight.y > 0.0) { vColor += getWeightColor(skinWeight.y, skinIndex.y) * skinWeight.y; vTotalWeight += skinWeight.y; vInfluenceCount += 1.0; }
            if (skinWeight.z > 0.0) { vColor += getWeightColor(skinWeight.z, skinIndex.z) * skinWeight.z; vTotalWeight += skinWeight.z; vInfluenceCount += 1.0; }
            if (skinWeight.w > 0.0) { vColor += getWeightColor(skinWeight.w, skinIndex.w) * skinWeight.w; vTotalWeight += skinWeight.w; vInfluenceCount += 1.0; }

            ${mode >= SkinningMode.EXTENDED_8 ? `
            // Extended 4 bones (5-8)
            if (skinWeight2.x > 0.0) { vColor += getWeightColor(skinWeight2.x, skinIndex2.x) * skinWeight2.x; vTotalWeight += skinWeight2.x; vInfluenceCount += 1.0; }
            if (skinWeight2.y > 0.0) { vColor += getWeightColor(skinWeight2.y, skinIndex2.y) * skinWeight2.y; vTotalWeight += skinWeight2.y; vInfluenceCount += 1.0; }
            if (skinWeight2.z > 0.0) { vColor += getWeightColor(skinWeight2.z, skinIndex2.z) * skinWeight2.z; vTotalWeight += skinWeight2.z; vInfluenceCount += 1.0; }
            if (skinWeight2.w > 0.0) { vColor += getWeightColor(skinWeight2.w, skinIndex2.w) * skinWeight2.w; vTotalWeight += skinWeight2.w; vInfluenceCount += 1.0; }
            ` : ''}

            vec3 transformed = vec3(position);

            #ifdef USE_SKINNING
                vec4 skinVertex = bindMatrix * vec4( transformed, 1.0 );
                vec4 skinned = vec4( 0.0 );

                skinned += boneMatX * skinVertex * skinWeight.x;
                skinned += boneMatY * skinVertex * skinWeight.y;
                skinned += boneMatZ * skinVertex * skinWeight.z;
                skinned += boneMatW * skinVertex * skinWeight.w;

                ${mode >= SkinningMode.EXTENDED_8 ? `
                skinned += boneMatX2 * skinVertex * skinWeight2.x;
                skinned += boneMatY2 * skinVertex * skinWeight2.y;
                skinned += boneMatZ2 * skinVertex * skinWeight2.z;
                skinned += boneMatW2 * skinVertex * skinWeight2.w;
                ` : ''}

                transformed = ( bindMatrixInverse * skinned ).xyz;
            #endif

            vec4 mvPosition = modelViewMatrix * vec4(transformed, 1.0);
            gl_Position = projectionMatrix * mvPosition;
        }
    `;

    const fragmentShader = `
        varying vec3 vColor;
        varying float vTotalWeight;
        varying float vInfluenceCount;

        uniform int visualizationMode;

        void main() {
            if (visualizationMode == 0) {
                // Blended weight colors
                gl_FragColor = vec4(vColor, 1.0);
            } else if (visualizationMode == 1) {
                // Weight intensity
                gl_FragColor = vec4(vec3(vTotalWeight), 1.0);
            } else if (visualizationMode == 2) {
                // Influence count visualization (brighter = more bones)
                float intensity = vInfluenceCount / ${Math.min(maxInfluences, 8)}.0;
                gl_FragColor = vec4(intensity, intensity * 0.5, 1.0 - intensity, 1.0);
            } else {
                gl_FragColor = vec4(vColor * vTotalWeight, 1.0);
            }
        }
    `;

    const uniforms = THREE.UniformsUtils.merge([
        THREE.UniformsLib.skinning,
        { visualizationMode: { value: 0 } }
    ]);

    return new THREE.ShaderMaterial({
        vertexShader,
        fragmentShader,
        uniforms,
        skinning: true,
        side: THREE.DoubleSide
    });
}

export default {
    SkinningMode,
    getSkinningMode,
    ExtendedSkinningConfig,
    addExtendedSkinningAttributes,
    createExtendedSkinningMaterial,
    ExtendedSkinnedMesh,
    getGeometrySkinningMode,
    hasExtendedSkinning,
    applyExtendedSkinningIfNeeded,
    createExtendedWeightVisualizationMaterial
};
