// Material Preset Save/Load System
// Save and load material parameters as JSON presets for reuse

import * as THREE from 'three';

export class MaterialPresetManager {
    constructor() {
        this.presets = new Map(); // Stored presets
        this.loadPresetsFromLocalStorage();
    }

    // Save material as preset
    saveMaterialPreset(material, name, category = 'Custom', thumbnail = null) {
        const preset = {
            name: name,
            category: category,
            timestamp: Date.now(),
            thumbnail: thumbnail, // Base64 PNG or null
            parameters: this.extractMaterialParameters(material)
        };

        this.presets.set(name, preset);
        this.savePresetsToLocalStorage();

        return preset;
    }

    // Extract material parameters
    extractMaterialParameters(material) {
        const params = {
            type: material.type || 'MeshStandardMaterial'
        };

        // Color properties
        if (material.color) {
            params.color = [material.color.r, material.color.g, material.color.b];
        }
        if (material.emissive) {
            params.emissive = [material.emissive.r, material.emissive.g, material.emissive.b];
        }

        // Scalar properties
        const scalarProps = [
            'metalness', 'roughness', 'emissiveIntensity',
            'aoMapIntensity', 'normalScale', 'displacementScale',
            'envMapIntensity', 'clearcoat', 'clearcoatRoughness',
            'transmission', 'thickness', 'ior', 'reflectivity',
            'sheen', 'sheenRoughness', 'specularIntensity',
            'iridescence', 'iridescenceIOR', 'iridescenceThicknessRange'
        ];

        scalarProps.forEach(prop => {
            if (material[prop] !== undefined) {
                params[prop] = material[prop];
            }
        });

        // Vector2 properties
        if (material.normalScale && material.normalScale.isVector2) {
            params.normalScale = [material.normalScale.x, material.normalScale.y];
        }

        // Boolean properties
        const boolProps = [
            'transparent', 'depthWrite', 'depthTest',
            'wireframe', 'flatShading', 'fog'
        ];

        boolProps.forEach(prop => {
            if (material[prop] !== undefined) {
                params[prop] = material[prop];
            }
        });

        // Enum properties
        if (material.side !== undefined) {
            params.side = material.side; // FrontSide, BackSide, DoubleSide
        }
        if (material.blending !== undefined) {
            params.blending = material.blending;
        }
        if (material.alphaTest !== undefined) {
            params.alphaTest = material.alphaTest;
        }

        // Texture info (names only, not data)
        const textureProps = [
            'map', 'normalMap', 'roughnessMap', 'metalnessMap',
            'aoMap', 'emissiveMap', 'displacementMap', 'envMap',
            'clearcoatMap', 'clearcoatNormalMap', 'clearcoatRoughnessMap',
            'transmissionMap', 'thicknessMap', 'sheenColorMap',
            'sheenRoughnessMap', 'specularColorMap', 'specularIntensityMap',
            'iridescenceMap', 'iridescenceThicknessMap'
        ];

        params.textures = {};
        textureProps.forEach(prop => {
            if (material[prop] && material[prop].image) {
                params.textures[prop] = {
                    hasTexture: true,
                    width: material[prop].image.width,
                    height: material[prop].image.height,
                    // Note: Not saving actual texture data, just metadata
                    source: material[prop].image.src || 'embedded'
                };
            }
        });

        return params;
    }

    // Apply preset to material
    applyPreset(preset, material) {
        const params = preset.parameters;

        // Apply color properties
        if (params.color && material.color) {
            material.color.setRGB(params.color[0], params.color[1], params.color[2]);
        }
        if (params.emissive && material.emissive) {
            material.emissive.setRGB(params.emissive[0], params.emissive[1], params.emissive[2]);
        }

        // Apply scalar properties
        const scalarProps = [
            'metalness', 'roughness', 'emissiveIntensity',
            'aoMapIntensity', 'displacementScale',
            'envMapIntensity', 'clearcoat', 'clearcoatRoughness',
            'transmission', 'thickness', 'ior', 'reflectivity',
            'sheen', 'sheenRoughness', 'specularIntensity',
            'iridescence', 'iridescenceIOR'
        ];

        scalarProps.forEach(prop => {
            if (params[prop] !== undefined && material[prop] !== undefined) {
                material[prop] = params[prop];
            }
        });

        // Apply Vector2 properties
        if (params.normalScale && Array.isArray(params.normalScale) && material.normalScale) {
            material.normalScale.set(params.normalScale[0], params.normalScale[1]);
        }

        // Apply boolean properties
        const boolProps = [
            'transparent', 'depthWrite', 'depthTest',
            'wireframe', 'flatShading', 'fog'
        ];

        boolProps.forEach(prop => {
            if (params[prop] !== undefined) {
                material[prop] = params[prop];
            }
        });

        // Apply enum properties
        if (params.side !== undefined) {
            material.side = params.side;
        }
        if (params.blending !== undefined) {
            material.blending = params.blending;
        }
        if (params.alphaTest !== undefined) {
            material.alphaTest = params.alphaTest;
        }

        material.needsUpdate = true;
        return true;
    }

    // Delete preset
    deletePreset(name) {
        const deleted = this.presets.delete(name);
        if (deleted) {
            this.savePresetsToLocalStorage();
        }
        return deleted;
    }

    // Get preset by name
    getPreset(name) {
        return this.presets.get(name);
    }

    // Get all presets
    getAllPresets() {
        return Array.from(this.presets.values());
    }

    // Get presets by category
    getPresetsByCategory(category) {
        return this.getAllPresets().filter(p => p.category === category);
    }

    // Get all categories
    getCategories() {
        const categories = new Set();
        this.presets.forEach(preset => {
            categories.add(preset.category);
        });
        return Array.from(categories).sort();
    }

    // Export preset as JSON file
    exportPresetToFile(name) {
        const preset = this.presets.get(name);
        if (!preset) return null;

        const json = JSON.stringify(preset, null, 2);
        const blob = new Blob([json], { type: 'application/json' });
        const url = URL.createObjectURL(blob);

        const a = document.createElement('a');
        a.href = url;
        a.download = `${name.replace(/\s+/g, '_')}.json`;
        a.click();

        URL.revokeObjectURL(url);
        return true;
    }

    // Import preset from JSON file
    importPresetFromFile(file) {
        return new Promise((resolve, reject) => {
            const reader = new FileReader();

            reader.onload = (e) => {
                try {
                    const preset = JSON.parse(e.target.result);

                    // Validate preset structure
                    if (!preset.name || !preset.parameters) {
                        reject(new Error('Invalid preset format'));
                        return;
                    }

                    // Add to presets
                    this.presets.set(preset.name, preset);
                    this.savePresetsToLocalStorage();

                    resolve(preset);
                } catch (err) {
                    reject(err);
                }
            };

            reader.onerror = () => reject(new Error('Failed to read file'));
            reader.readAsText(file);
        });
    }

    // Export all presets as JSON
    exportAllPresets() {
        const allPresets = this.getAllPresets();
        const json = JSON.stringify(allPresets, null, 2);
        const blob = new Blob([json], { type: 'application/json' });
        const url = URL.createObjectURL(blob);

        const a = document.createElement('a');
        a.href = url;
        a.download = 'material_presets_library.json';
        a.click();

        URL.revokeObjectURL(url);
    }

    // Import multiple presets from JSON
    importPresetsFromFile(file) {
        return new Promise((resolve, reject) => {
            const reader = new FileReader();

            reader.onload = (e) => {
                try {
                    const presets = JSON.parse(e.target.result);

                    if (!Array.isArray(presets)) {
                        reject(new Error('File must contain an array of presets'));
                        return;
                    }

                    let imported = 0;
                    presets.forEach(preset => {
                        if (preset.name && preset.parameters) {
                            this.presets.set(preset.name, preset);
                            imported++;
                        }
                    });

                    this.savePresetsToLocalStorage();
                    resolve(imported);
                } catch (err) {
                    reject(err);
                }
            };

            reader.onerror = () => reject(new Error('Failed to read file'));
            reader.readAsText(file);
        });
    }

    // Save presets to localStorage
    savePresetsToLocalStorage() {
        try {
            const presetsArray = Array.from(this.presets.values());
            // Don't save thumbnails to localStorage (too large)
            const presetsWithoutThumbnails = presetsArray.map(p => ({
                ...p,
                thumbnail: null
            }));
            localStorage.setItem('materialPresets', JSON.stringify(presetsWithoutThumbnails));
        } catch (e) {
            console.warn('Failed to save presets to localStorage:', e);
        }
    }

    // Load presets from localStorage
    loadPresetsFromLocalStorage() {
        try {
            const stored = localStorage.getItem('materialPresets');
            if (stored) {
                const presetsArray = JSON.parse(stored);
                presetsArray.forEach(preset => {
                    this.presets.set(preset.name, preset);
                });
            }
        } catch (e) {
            console.warn('Failed to load presets from localStorage:', e);
        }
    }

    // Clear all presets
    clearAllPresets() {
        this.presets.clear();
        this.savePresetsToLocalStorage();
    }

    // Generate text report
    generateReport() {
        let report = '# Material Presets Library\n\n';
        report += `**Total Presets**: ${this.presets.size}\n\n`;

        const categories = this.getCategories();

        categories.forEach(category => {
            const categoryPresets = this.getPresetsByCategory(category);
            if (categoryPresets.length > 0) {
                report += `## ${category} (${categoryPresets.length})\n\n`;

                categoryPresets.forEach(preset => {
                    report += `### ${preset.name}\n`;
                    report += `- **Type**: ${preset.parameters.type || 'Unknown'}\n`;

                    if (preset.parameters.color) {
                        const c = preset.parameters.color;
                        report += `- **Color**: RGB(${c[0].toFixed(3)}, ${c[1].toFixed(3)}, ${c[2].toFixed(3)})\n`;
                    }

                    if (preset.parameters.metalness !== undefined) {
                        report += `- **Metalness**: ${preset.parameters.metalness}\n`;
                    }

                    if (preset.parameters.roughness !== undefined) {
                        report += `- **Roughness**: ${preset.parameters.roughness}\n`;
                    }

                    const textureCount = Object.keys(preset.parameters.textures || {}).length;
                    if (textureCount > 0) {
                        report += `- **Textures**: ${textureCount} maps\n`;
                    }

                    report += '\n';
                });
            }
        });

        return report;
    }
}

// Make class globally accessible
if (typeof window !== 'undefined') {
    window.MaterialPresetManager = MaterialPresetManager;
}
