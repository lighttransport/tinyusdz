// Helper function to convert OpenPBR material data to MaterialX XML string
// This generates MaterialX XML that can be loaded by Three.js MaterialXLoader
export function convertOpenPBRToMaterialXML(materialData, materialName = 'Material') {
    let xml = `<?xml version="1.0"?>
<materialx version="1.39">
  <open_pbr_surface name="${materialName}_shader" type="surfaceshader">
`;

    // Helper to add parameter
    const addParam = (name, value, type = 'float') => {
        if (value === undefined || value === null) return '';

        if (type === 'color3' && Array.isArray(value)) {
            return `    <input name="${name}" type="color3" value="${value[0]}, ${value[1]}, ${value[2]}" />\n`;
        } else if (type === 'float') {
            return `    <input name="${name}" type="float" value="${value}" />\n`;
        } else if (type === 'vector3' && Array.isArray(value)) {
            return `    <input name="${name}" type="vector3" value="${value[0]}, ${value[1]}, ${value[2]}" />\n`;
        }
        return '';
    };

    // Extract values from either flat or grouped format
    const extractValue = (flatPath, groupedPath) => {
        // Try flat format first
        if (materialData[flatPath] !== undefined) {
            const val = materialData[flatPath];
            return typeof val === 'object' && val.value !== undefined ? val.value : val;
        }

        // Try grouped format
        if (groupedPath) {
            const parts = groupedPath.split('.');
            let current = materialData;
            for (const part of parts) {
                if (!current || !current[part]) return undefined;
                current = current[part];
            }
            const val = current;
            return typeof val === 'object' && val.value !== undefined ? val.value : val;
        }

        return undefined;
    };

    // Base layer
    xml += addParam('base_weight', extractValue('base_weight', 'base.base_weight'));
    xml += addParam('base_color', extractValue('base_color', 'base.base_color'), 'color3');
    xml += addParam('base_roughness', extractValue('base_roughness', 'base.base_roughness'));
    xml += addParam('base_metalness', extractValue('base_metalness', 'base.base_metalness'));

    // Specular layer
    xml += addParam('specular_weight', extractValue('specular_weight', 'specular.specular_weight'));
    xml += addParam('specular_color', extractValue('specular_color', 'specular.specular_color'), 'color3');
    xml += addParam('specular_roughness', extractValue('specular_roughness', 'specular.specular_roughness'));
    xml += addParam('specular_ior', extractValue('specular_ior', 'specular.specular_ior'));
    xml += addParam('specular_ior_level', extractValue('specular_ior_level', 'specular.specular_ior_level'));
    xml += addParam('specular_anisotropy', extractValue('specular_anisotropy', 'specular.specular_anisotropy'));
    xml += addParam('specular_rotation', extractValue('specular_rotation', 'specular.specular_rotation'));

    // Transmission
    xml += addParam('transmission_weight', extractValue('transmission_weight', 'transmission.transmission_weight'));
    xml += addParam('transmission_color', extractValue('transmission_color', 'transmission.transmission_color'), 'color3');
    xml += addParam('transmission_depth', extractValue('transmission_depth', 'transmission.transmission_depth'));
    xml += addParam('transmission_scatter', extractValue('transmission_scatter', 'transmission.transmission_scatter'), 'color3');
    xml += addParam('transmission_scatter_anisotropy', extractValue('transmission_scatter_anisotropy', 'transmission.transmission_scatter_anisotropy'));
    xml += addParam('transmission_dispersion', extractValue('transmission_dispersion', 'transmission.transmission_dispersion'));

    // Subsurface
    xml += addParam('subsurface_weight', extractValue('subsurface_weight', 'subsurface.subsurface_weight'));
    xml += addParam('subsurface_color', extractValue('subsurface_color', 'subsurface.subsurface_color'), 'color3');
    xml += addParam('subsurface_radius', extractValue('subsurface_radius', 'subsurface.subsurface_radius'), 'color3');
    xml += addParam('subsurface_scale', extractValue('subsurface_scale', 'subsurface.subsurface_scale'));
    xml += addParam('subsurface_anisotropy', extractValue('subsurface_anisotropy', 'subsurface.subsurface_anisotropy'));

    // Sheen
    xml += addParam('sheen_weight', extractValue('sheen_weight', 'sheen.sheen_weight'));
    xml += addParam('sheen_color', extractValue('sheen_color', 'sheen.sheen_color'), 'color3');
    xml += addParam('sheen_roughness', extractValue('sheen_roughness', 'sheen.sheen_roughness'));

    // Coat
    xml += addParam('coat_weight', extractValue('coat_weight', 'coat.coat_weight'));
    xml += addParam('coat_color', extractValue('coat_color', 'coat.coat_color'), 'color3');
    xml += addParam('coat_roughness', extractValue('coat_roughness', 'coat.coat_roughness'));
    xml += addParam('coat_anisotropy', extractValue('coat_anisotropy', 'coat.coat_anisotropy'));
    xml += addParam('coat_rotation', extractValue('coat_rotation', 'coat.coat_rotation'));
    xml += addParam('coat_ior', extractValue('coat_ior', 'coat.coat_ior'));
    xml += addParam('coat_affect_color', extractValue('coat_affect_color', 'coat.coat_affect_color'), 'color3');
    xml += addParam('coat_affect_roughness', extractValue('coat_affect_roughness', 'coat.coat_affect_roughness'));

    // Emission
    xml += addParam('emission_luminance', extractValue('emission_luminance', 'emission.emission_luminance'));
    xml += addParam('emission_color', extractValue('emission_color', 'emission.emission_color'), 'color3');

    // Geometry
    xml += addParam('opacity', extractValue('opacity', 'geometry.opacity'));
    xml += addParam('geometry_normal', extractValue('geometry_normal', 'geometry.normal'), 'vector3');
    xml += addParam('geometry_tangent', extractValue('geometry_tangent', 'geometry.tangent'), 'vector3');

    xml += `  </open_pbr_surface>

  <surfacematerial name="${materialName}" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="${materialName}_shader" />
  </surfacematerial>
</materialx>
`;

    return xml;
}
