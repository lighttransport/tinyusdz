// Reference Material Library
// Physically accurate PBR values for common materials based on real-world measurements

import * as THREE from 'three';

// Reference material database with measured PBR values
export const REFERENCE_MATERIALS = {
    // === Metals ===
    gold: {
        name: 'Gold (Pure)',
        category: 'Metal',
        baseColor: [1.0, 0.766, 0.336],
        metalness: 1.0,
        roughness: 0.2,
        ior: 0.47, // Complex IOR (real part)
        f0: [1.0, 0.71, 0.29],
        description: 'Pure 24k gold, polished finish'
    },

    silver: {
        name: 'Silver (Pure)',
        category: 'Metal',
        baseColor: [0.972, 0.960, 0.915],
        metalness: 1.0,
        roughness: 0.15,
        ior: 0.155,
        f0: [0.95, 0.93, 0.88],
        description: 'Pure silver, polished finish'
    },

    copper: {
        name: 'Copper',
        category: 'Metal',
        baseColor: [0.955, 0.637, 0.538],
        metalness: 1.0,
        roughness: 0.25,
        ior: 1.1,
        f0: [0.93, 0.57, 0.48],
        description: 'Pure copper, slightly tarnished'
    },

    aluminum: {
        name: 'Aluminum',
        category: 'Metal',
        baseColor: [0.912, 0.914, 0.920],
        metalness: 1.0,
        roughness: 0.3,
        ior: 1.44,
        f0: [0.91, 0.92, 0.92],
        description: 'Brushed aluminum finish'
    },

    iron: {
        name: 'Iron',
        category: 'Metal',
        baseColor: [0.560, 0.570, 0.580],
        metalness: 1.0,
        roughness: 0.45,
        ior: 2.95,
        f0: [0.56, 0.57, 0.58],
        description: 'Raw iron, slightly oxidized'
    },

    chrome: {
        name: 'Chrome',
        category: 'Metal',
        baseColor: [0.550, 0.556, 0.554],
        metalness: 1.0,
        roughness: 0.05,
        ior: 3.18,
        f0: [0.54, 0.55, 0.55],
        description: 'Polished chrome plating'
    },

    // === Dielectrics - Plastics ===
    plastic_glossy: {
        name: 'Plastic (Glossy)',
        category: 'Plastic',
        baseColor: [0.5, 0.5, 0.5],
        metalness: 0.0,
        roughness: 0.1,
        ior: 1.5,
        f0: [0.04, 0.04, 0.04],
        description: 'Smooth plastic, polished finish'
    },

    plastic_matte: {
        name: 'Plastic (Matte)',
        category: 'Plastic',
        baseColor: [0.5, 0.5, 0.5],
        metalness: 0.0,
        roughness: 0.6,
        ior: 1.5,
        f0: [0.04, 0.04, 0.04],
        description: 'Matte plastic surface'
    },

    rubber: {
        name: 'Rubber',
        category: 'Plastic',
        baseColor: [0.1, 0.1, 0.1],
        metalness: 0.0,
        roughness: 0.9,
        ior: 1.519,
        f0: [0.04, 0.04, 0.04],
        description: 'Black rubber, diffuse finish'
    },

    // === Dielectrics - Glass ===
    glass: {
        name: 'Glass (Clear)',
        category: 'Glass',
        baseColor: [1.0, 1.0, 1.0],
        metalness: 0.0,
        roughness: 0.0,
        ior: 1.5,
        transmission: 1.0,
        f0: [0.04, 0.04, 0.04],
        description: 'Clear glass, smooth'
    },

    glass_frosted: {
        name: 'Glass (Frosted)',
        category: 'Glass',
        baseColor: [1.0, 1.0, 1.0],
        metalness: 0.0,
        roughness: 0.3,
        ior: 1.5,
        transmission: 0.8,
        f0: [0.04, 0.04, 0.04],
        description: 'Frosted glass'
    },

    // === Dielectrics - Natural Materials ===
    water: {
        name: 'Water',
        category: 'Liquid',
        baseColor: [0.5, 0.7, 0.8],
        metalness: 0.0,
        roughness: 0.0,
        ior: 1.333,
        transmission: 0.95,
        f0: [0.02, 0.02, 0.02],
        description: 'Clean water surface'
    },

    wood_oak: {
        name: 'Wood (Oak)',
        category: 'Wood',
        baseColor: [0.545, 0.353, 0.169],
        metalness: 0.0,
        roughness: 0.7,
        ior: 1.5,
        f0: [0.04, 0.04, 0.04],
        description: 'Oak wood, natural finish'
    },

    wood_polished: {
        name: 'Wood (Polished)',
        category: 'Wood',
        baseColor: [0.4, 0.25, 0.15],
        metalness: 0.0,
        roughness: 0.2,
        ior: 1.5,
        f0: [0.04, 0.04, 0.04],
        description: 'Polished wood with varnish'
    },

    concrete: {
        name: 'Concrete',
        category: 'Stone',
        baseColor: [0.5, 0.5, 0.5],
        metalness: 0.0,
        roughness: 0.8,
        ior: 1.55,
        f0: [0.04, 0.04, 0.04],
        description: 'Rough concrete surface'
    },

    marble: {
        name: 'Marble',
        category: 'Stone',
        baseColor: [0.9, 0.9, 0.88],
        metalness: 0.0,
        roughness: 0.3,
        ior: 1.5,
        f0: [0.04, 0.04, 0.04],
        description: 'Polished white marble'
    },

    // === Organics ===
    skin_caucasian: {
        name: 'Skin (Caucasian)',
        category: 'Skin',
        baseColor: [0.944, 0.776, 0.648],
        metalness: 0.0,
        roughness: 0.5,
        ior: 1.4,
        subsurface: 0.15,
        f0: [0.028, 0.028, 0.028],
        description: 'Human skin (Caucasian, Type II)'
    },

    skin_african: {
        name: 'Skin (African)',
        category: 'Skin',
        baseColor: [0.459, 0.345, 0.259],
        metalness: 0.0,
        roughness: 0.5,
        ior: 1.4,
        subsurface: 0.15,
        f0: [0.028, 0.028, 0.028],
        description: 'Human skin (African, Type V)'
    },

    leather: {
        name: 'Leather',
        category: 'Leather',
        baseColor: [0.4, 0.3, 0.2],
        metalness: 0.0,
        roughness: 0.6,
        ior: 1.5,
        f0: [0.04, 0.04, 0.04],
        description: 'Brown leather, natural finish'
    },

    fabric_cotton: {
        name: 'Fabric (Cotton)',
        category: 'Fabric',
        baseColor: [0.8, 0.8, 0.8],
        metalness: 0.0,
        roughness: 0.85,
        ior: 1.54,
        sheen: 0.3,
        f0: [0.04, 0.04, 0.04],
        description: 'White cotton fabric'
    },

    fabric_silk: {
        name: 'Fabric (Silk)',
        category: 'Fabric',
        baseColor: [0.9, 0.9, 0.9],
        metalness: 0.0,
        roughness: 0.4,
        ior: 1.54,
        sheen: 0.8,
        f0: [0.04, 0.04, 0.04],
        description: 'Silk fabric, lustrous'
    }
};

// Helper function to apply reference material to Three.js material
export function applyReferenceMaterial(material, referenceKey) {
    const ref = REFERENCE_MATERIALS[referenceKey];
    if (!ref) {
        console.error(`Reference material "${referenceKey}" not found`);
        return false;
    }

    // Apply base color
    if (ref.baseColor && material.color) {
        material.color.setRGB(ref.baseColor[0], ref.baseColor[1], ref.baseColor[2]);
    }

    // Apply metalness
    if (ref.metalness !== undefined && material.metalness !== undefined) {
        material.metalness = ref.metalness;
    }

    // Apply roughness
    if (ref.roughness !== undefined && material.roughness !== undefined) {
        material.roughness = ref.roughness;
    }

    // Apply IOR (if supported by material)
    if (ref.ior !== undefined && material.ior !== undefined) {
        material.ior = ref.ior;
    }

    // Apply transmission (MeshPhysicalMaterial only)
    if (ref.transmission !== undefined && material.transmission !== undefined) {
        material.transmission = ref.transmission;
    }

    // Apply sheen (MeshPhysicalMaterial only)
    if (ref.sheen !== undefined && material.sheen !== undefined) {
        material.sheen = ref.sheen;
    }

    material.needsUpdate = true;

    console.log(`Applied reference material: ${ref.name}`);
    return true;
}

// Get all reference materials by category
export function getReferencesByCategory(category) {
    return Object.keys(REFERENCE_MATERIALS)
        .filter(key => REFERENCE_MATERIALS[key].category === category)
        .map(key => ({ key, ...REFERENCE_MATERIALS[key] }));
}

// Get all categories
export function getCategories() {
    const categories = new Set();
    Object.values(REFERENCE_MATERIALS).forEach(mat => {
        categories.add(mat.category);
    });
    return Array.from(categories).sort();
}

// Compare current material with reference
export function compareWithReference(material, referenceKey) {
    const ref = REFERENCE_MATERIALS[referenceKey];
    if (!ref) {
        return null;
    }

    const comparison = {
        reference: ref.name,
        differences: []
    };

    // Compare base color
    if (ref.baseColor && material.color) {
        const currentColor = [material.color.r, material.color.g, material.color.b];
        const refColor = ref.baseColor;
        const diff = Math.sqrt(
            Math.pow(currentColor[0] - refColor[0], 2) +
            Math.pow(currentColor[1] - refColor[1], 2) +
            Math.pow(currentColor[2] - refColor[2], 2)
        );

        if (diff > 0.1) {
            comparison.differences.push({
                property: 'Base Color',
                current: `RGB(${currentColor.map(v => v.toFixed(3)).join(', ')})`,
                reference: `RGB(${refColor.map(v => v.toFixed(3)).join(', ')})`,
                delta: diff.toFixed(3)
            });
        }
    }

    // Compare metalness
    if (ref.metalness !== undefined && material.metalness !== undefined) {
        const diff = Math.abs(material.metalness - ref.metalness);
        if (diff > 0.05) {
            comparison.differences.push({
                property: 'Metalness',
                current: material.metalness.toFixed(2),
                reference: ref.metalness.toFixed(2),
                delta: diff.toFixed(3)
            });
        }
    }

    // Compare roughness
    if (ref.roughness !== undefined && material.roughness !== undefined) {
        const diff = Math.abs(material.roughness - ref.roughness);
        if (diff > 0.05) {
            comparison.differences.push({
                property: 'Roughness',
                current: material.roughness.toFixed(2),
                reference: ref.roughness.toFixed(2),
                delta: diff.toFixed(3)
            });
        }
    }

    return comparison;
}

// Make functions globally accessible
if (typeof window !== 'undefined') {
    window.REFERENCE_MATERIALS = REFERENCE_MATERIALS;
    window.applyReferenceMaterial = applyReferenceMaterial;
    window.getReferencesByCategory = getReferencesByCategory;
    window.getCategories = getCategories;
    window.compareWithReference = compareWithReference;
}
