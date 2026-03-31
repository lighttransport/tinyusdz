// Material Override System
// Temporarily override material properties globally for debugging

import * as THREE from 'three';

let overridesActive = false;
let originalMaterialProps = new Map();
let currentOverrides = {
    roughness: null,
    metalness: null,
    baseColor: null,
    disableNormalMaps: false,
    disableAllTextures: false,
    disableMaps: {
        base: false,
        normal: false,
        roughness: false,
        metalness: false,
        ao: false,
        emissive: false
    }
};

// Apply material overrides to scene
export function applyMaterialOverrides(scene, overrides) {
    if (!scene) {
        console.error('No scene provided for material overrides');
        return;
    }

    // Merge overrides with current
    currentOverrides = { ...currentOverrides, ...overrides };

    scene.traverse(obj => {
        if (obj.isMesh && obj.material) {
            const material = obj.material;

            // Store original properties if not already stored
            if (!originalMaterialProps.has(obj.uuid)) {
                originalMaterialProps.set(obj.uuid, {
                    roughness: material.roughness,
                    metalness: material.metalness,
                    color: material.color ? material.color.clone() : null,
                    map: material.map,
                    normalMap: material.normalMap,
                    roughnessMap: material.roughnessMap,
                    metalnessMap: material.metalnessMap,
                    aoMap: material.aoMap,
                    emissiveMap: material.emissiveMap,
                    normalScale: material.normalScale ? material.normalScale.clone() : null
                });
            }

            // Apply roughness override
            if (overrides.roughness !== null && overrides.roughness !== undefined) {
                material.roughness = overrides.roughness;
            }

            // Apply metalness override
            if (overrides.metalness !== null && overrides.metalness !== undefined) {
                material.metalness = overrides.metalness;
            }

            // Apply base color override
            if (overrides.baseColor !== null && overrides.baseColor !== undefined) {
                if (material.color) {
                    material.color.copy(overrides.baseColor);
                }
            }

            // Disable specific texture maps
            if (overrides.disableMaps) {
                if (overrides.disableMaps.base) {
                    material.map = null;
                }
                if (overrides.disableMaps.normal) {
                    material.normalMap = null;
                }
                if (overrides.disableMaps.roughness) {
                    material.roughnessMap = null;
                }
                if (overrides.disableMaps.metalness) {
                    material.metalnessMap = null;
                }
                if (overrides.disableMaps.ao) {
                    material.aoMap = null;
                }
                if (overrides.disableMaps.emissive) {
                    material.emissiveMap = null;
                }
            }

            // Disable normal maps globally
            if (overrides.disableNormalMaps) {
                material.normalMap = null;
            }

            // Disable ALL textures
            if (overrides.disableAllTextures) {
                material.map = null;
                material.normalMap = null;
                material.roughnessMap = null;
                material.metalnessMap = null;
                material.aoMap = null;
                material.emissiveMap = null;
                material.clearcoatMap = null;
                material.clearcoatNormalMap = null;
                material.clearcoatRoughnessMap = null;
            }

            material.needsUpdate = true;
        }
    });

    overridesActive = true;
    console.log('Material overrides applied:', overrides);
}

// Reset all material overrides
export function resetMaterialOverrides(scene) {
    if (!scene) {
        console.error('No scene provided for reset');
        return;
    }

    scene.traverse(obj => {
        if (obj.isMesh && originalMaterialProps.has(obj.uuid)) {
            const material = obj.material;
            const orig = originalMaterialProps.get(obj.uuid);

            // Restore original properties
            if (orig.roughness !== undefined) {
                material.roughness = orig.roughness;
            }
            if (orig.metalness !== undefined) {
                material.metalness = orig.metalness;
            }
            if (orig.color && material.color) {
                material.color.copy(orig.color);
            }

            // Restore texture maps
            material.map = orig.map;
            material.normalMap = orig.normalMap;
            material.roughnessMap = orig.roughnessMap;
            material.metalnessMap = orig.metalnessMap;
            material.aoMap = orig.aoMap;
            material.emissiveMap = orig.emissiveMap;

            if (orig.normalScale && material.normalScale) {
                material.normalScale.copy(orig.normalScale);
            }

            material.needsUpdate = true;
        }
    });

    originalMaterialProps.clear();
    overridesActive = false;

    // Reset override state
    currentOverrides = {
        roughness: null,
        metalness: null,
        baseColor: null,
        disableNormalMaps: false,
        disableAllTextures: false,
        disableMaps: {
            base: false,
            normal: false,
            roughness: false,
            metalness: false,
            ao: false,
            emissive: false
        }
    };

    console.log('Material overrides reset');
}

// Check if overrides are active
export function areOverridesActive() {
    return overridesActive;
}

// Get current override values
export function getCurrentOverrides() {
    return { ...currentOverrides };
}

// Set specific override value
export function setOverride(scene, property, value) {
    const overrides = {};
    overrides[property] = value;
    applyMaterialOverrides(scene, overrides);
}

// Toggle texture map disable
export function toggleTextureMap(scene, mapName, disabled) {
    const overrides = {
        disableMaps: { ...currentOverrides.disableMaps }
    };
    overrides.disableMaps[mapName] = disabled;
    applyMaterialOverrides(scene, overrides);
}

// Quick presets
export const OVERRIDE_PRESETS = {
    // Show only base color (no textures)
    BASE_COLOR_ONLY: {
        disableAllTextures: true,
        roughness: 0.5,
        metalness: 0.0
    },

    // Show only normals effect
    NORMALS_ONLY: {
        baseColor: new THREE.Color(0.5, 0.5, 0.5),
        roughness: 0.5,
        metalness: 0.0,
        disableMaps: {
            base: true,
            roughness: true,
            metalness: true,
            ao: true,
            emissive: true
        }
    },

    // Flat shading (no bump detail)
    FLAT_SHADING: {
        disableNormalMaps: true
    },

    // Mirror finish (test reflections)
    MIRROR: {
        roughness: 0.0,
        metalness: 1.0
    },

    // Matte finish (test diffuse)
    MATTE: {
        roughness: 1.0,
        metalness: 0.0
    },

    // White clay (material preview style)
    WHITE_CLAY: {
        baseColor: new THREE.Color(0.8, 0.8, 0.8),
        roughness: 0.6,
        metalness: 0.0,
        disableAllTextures: true
    }
};

// Apply preset
export function applyOverridePreset(scene, presetName) {
    const preset = OVERRIDE_PRESETS[presetName];
    if (preset) {
        applyMaterialOverrides(scene, preset);
        console.log(`Applied override preset: ${presetName}`);
    } else {
        console.error(`Unknown preset: ${presetName}`);
    }
}

// Make functions globally accessible
if (typeof window !== 'undefined') {
    window.applyMaterialOverrides = applyMaterialOverrides;
    window.resetMaterialOverrides = resetMaterialOverrides;
    window.applyOverridePreset = applyOverridePreset;
    window.setMaterialOverride = setOverride;
    window.toggleTextureMap = toggleTextureMap;
}
