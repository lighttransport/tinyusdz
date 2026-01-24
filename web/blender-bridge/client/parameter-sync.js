// Parameter Sync for Blender Bridge
// Maps OpenPBR/USD parameters to Three.js objects

import * as THREE from 'three';

/**
 * Target types for parameter updates
 */
export const TargetType = {
  MATERIAL: 'material',
  LIGHT: 'light',
  CAMERA: 'camera',
  TRANSFORM: 'transform'
};

/**
 * Color space conversion: sRGB to linear
 */
function sRGBToLinear(value) {
  if (value <= 0.04045) {
    return value / 12.92;
  }
  return Math.pow((value + 0.055) / 1.055, 2.4);
}

/**
 * Convert sRGB color array to linear
 */
function sRGBArrayToLinear(arr) {
  return arr.map(v => sRGBToLinear(v));
}

/**
 * Material parameter mappings
 * Maps OpenPBR/USD parameter names to Three.js MeshPhysicalMaterial setters
 */
const MATERIAL_PARAM_MAP = {
  // Base layer
  'base_color': (mat, value) => {
    const linear = sRGBArrayToLinear(value);
    mat.color.setRGB(linear[0], linear[1], linear[2]);
  },
  'base_weight': (mat, value) => {
    // OpenPBR base weight affects overall color
    // Multiply with existing color
  },
  'base_metalness': (mat, value) => {
    mat.metalness = value;
  },
  'base_diffuse_roughness': (mat, value) => {
    // OpenPBR diffuse roughness - no direct Three.js equivalent
    // Could use for custom shader
  },

  // Specular layer
  'specular_roughness': (mat, value) => {
    mat.roughness = value;
  },
  'specular_color': (mat, value) => {
    const linear = sRGBArrayToLinear(value);
    mat.specularColor.setRGB(linear[0], linear[1], linear[2]);
  },
  'specular_ior': (mat, value) => {
    mat.ior = value;
  },
  'specular_weight': (mat, value) => {
    mat.specularIntensity = value;
  },
  'specular_anisotropy': (mat, value) => {
    mat.anisotropy = value;
  },
  'specular_anisotropy_rotation': (mat, value) => {
    mat.anisotropyRotation = value * Math.PI * 2; // 0-1 to radians
  },

  // Coat layer (clearcoat)
  'coat_weight': (mat, value) => {
    mat.clearcoat = value;
  },
  'coat_roughness': (mat, value) => {
    mat.clearcoatRoughness = value;
  },
  'coat_ior': (mat, value) => {
    // Three.js doesn't have separate clearcoat IOR
    // Could affect reflectivity calculation
  },
  'coat_color': (mat, value) => {
    // Three.js doesn't have clearcoat color
    // Would need custom shader
  },

  // Transmission
  'transmission_weight': (mat, value) => {
    mat.transmission = value;
  },
  'transmission_color': (mat, value) => {
    const linear = sRGBArrayToLinear(value);
    mat.attenuationColor.setRGB(linear[0], linear[1], linear[2]);
  },
  'transmission_depth': (mat, value) => {
    mat.attenuationDistance = value;
  },

  // Subsurface
  'subsurface_weight': (mat, value) => {
    // Three.js has limited SSS support
    // Could use thickness for approximation
  },
  'subsurface_color': (mat, value) => {
    // Would need custom shader for proper SSS
  },
  'subsurface_radius': (mat, value) => {
    // SSS radius per channel
  },

  // Emission
  'emission_color': (mat, value) => {
    const linear = sRGBArrayToLinear(value);
    mat.emissive.setRGB(linear[0], linear[1], linear[2]);
  },
  'emission_luminance': (mat, value) => {
    // Convert luminance to intensity
    mat.emissiveIntensity = value / 1000;
  },

  // Geometry
  'geometry_opacity': (mat, value) => {
    mat.opacity = value;
    mat.transparent = value < 1.0;
  },
  'geometry_thin_walled': (mat, value) => {
    mat.thickness = value ? 0 : 0.5;
    mat.side = value ? THREE.DoubleSide : THREE.FrontSide;
  },

  // Sheen
  'sheen_weight': (mat, value) => {
    mat.sheen = value;
  },
  'sheen_color': (mat, value) => {
    const linear = sRGBArrayToLinear(value);
    mat.sheenColor.setRGB(linear[0], linear[1], linear[2]);
  },
  'sheen_roughness': (mat, value) => {
    mat.sheenRoughness = value;
  },

  // USD Preview Surface compatibility
  'diffuseColor': (mat, value) => {
    const linear = sRGBArrayToLinear(value);
    mat.color.setRGB(linear[0], linear[1], linear[2]);
  },
  'metallic': (mat, value) => {
    mat.metalness = value;
  },
  'roughness': (mat, value) => {
    mat.roughness = value;
  },
  'opacity': (mat, value) => {
    mat.opacity = value;
    mat.transparent = value < 1.0;
  },
  'ior': (mat, value) => {
    mat.ior = value;
  },
  'clearcoat': (mat, value) => {
    mat.clearcoat = value;
  },
  'clearcoatRoughness': (mat, value) => {
    mat.clearcoatRoughness = value;
  },
  'emissiveColor': (mat, value) => {
    const linear = sRGBArrayToLinear(value);
    mat.emissive.setRGB(linear[0], linear[1], linear[2]);
  }
};

/**
 * Light parameter mappings
 */
const LIGHT_PARAM_MAP = {
  'color': (light, value) => {
    const linear = sRGBArrayToLinear(value);
    light.color.setRGB(linear[0], linear[1], linear[2]);
  },
  'intensity': (light, value) => {
    light.intensity = value;
  },
  'exposure': (light, value) => {
    // Exposure affects intensity: intensity * 2^exposure
    light.intensity = light.userData.baseIntensity * Math.pow(2, value);
  },
  'position': (light, value) => {
    light.position.set(value[0], value[1], value[2]);
  },
  'direction': (light, value) => {
    // For directional lights, set target position
    if (light.target) {
      const pos = light.position;
      light.target.position.set(
        pos.x + value[0],
        pos.y + value[1],
        pos.z + value[2]
      );
    }
  },
  'radius': (light, value) => {
    // For point/spot lights
    if (light.isPointLight || light.isSpotLight) {
      light.distance = value * 10; // Scale factor
    }
  },
  'angle': (light, value) => {
    // For spot lights (value in degrees)
    if (light.isSpotLight) {
      light.angle = THREE.MathUtils.degToRad(value);
    }
  },
  'penumbra': (light, value) => {
    if (light.isSpotLight) {
      light.penumbra = value;
    }
  },
  'width': (light, value) => {
    // For rect area lights
    if (light.isRectAreaLight) {
      light.width = value;
    }
  },
  'height': (light, value) => {
    // For rect area lights
    if (light.isRectAreaLight) {
      light.height = value;
    }
  },
  'castShadow': (light, value) => {
    light.castShadow = value;
  },
  'shadowBias': (light, value) => {
    if (light.shadow) {
      light.shadow.bias = value;
    }
  },
  'shadowRadius': (light, value) => {
    if (light.shadow) {
      light.shadow.radius = value;
    }
  }
};

/**
 * Camera parameter mappings
 */
const CAMERA_PARAM_MAP = {
  'position': (camera, value, controls) => {
    camera.position.set(value[0], value[1], value[2]);
    if (controls) controls.update();
  },
  'target': (camera, value, controls) => {
    if (controls) {
      controls.target.set(value[0], value[1], value[2]);
      controls.update();
    }
  },
  'fov': (camera, value) => {
    if (camera.isPerspectiveCamera) {
      camera.fov = value;
      camera.updateProjectionMatrix();
    }
  },
  'near': (camera, value) => {
    camera.near = value;
    camera.updateProjectionMatrix();
  },
  'far': (camera, value) => {
    camera.far = value;
    camera.updateProjectionMatrix();
  },
  'zoom': (camera, value) => {
    camera.zoom = value;
    camera.updateProjectionMatrix();
  },
  'focalLength': (camera, value) => {
    if (camera.isPerspectiveCamera) {
      camera.setFocalLength(value);
    }
  }
};

/**
 * Transform parameter mappings
 */
const TRANSFORM_PARAM_MAP = {
  'position': (object, value) => {
    object.position.set(value[0], value[1], value[2]);
  },
  'rotation': (object, value) => {
    // Euler angles in radians
    object.rotation.set(value[0], value[1], value[2]);
  },
  'scale': (object, value) => {
    object.scale.set(value[0], value[1], value[2]);
  },
  'matrix': (object, value) => {
    // 4x4 matrix as flat array
    const matrix = new THREE.Matrix4();
    matrix.fromArray(value);
    object.matrix.copy(matrix);
    object.matrix.decompose(object.position, object.quaternion, object.scale);
  },
  'quaternion': (object, value) => {
    object.quaternion.set(value[0], value[1], value[2], value[3]);
  },
  'visible': (object, value) => {
    object.visible = value;
  }
};

/**
 * ParameterSync - Applies parameter updates to Three.js objects
 */
export class ParameterSync {
  constructor() {
    // Maps USD paths to Three.js objects
    this.pathToMaterial = new Map();
    this.pathToLight = new Map();
    this.pathToCamera = new Map();
    this.pathToObject = new Map();

    // Reference to orbit controls for camera updates
    this.controls = null;
  }

  /**
   * Set the orbit controls reference
   */
  setControls(controls) {
    this.controls = controls;
  }

  /**
   * Register a material with its USD path
   */
  registerMaterial(path, material) {
    this.pathToMaterial.set(path, material);
  }

  /**
   * Register a light with its USD path
   */
  registerLight(path, light) {
    // Store base intensity for exposure calculations
    light.userData.baseIntensity = light.intensity;
    this.pathToLight.set(path, light);
  }

  /**
   * Register a camera with its USD path
   */
  registerCamera(path, camera) {
    this.pathToCamera.set(path, camera);
  }

  /**
   * Register an object (for transforms) with its USD path
   */
  registerObject(path, object) {
    this.pathToObject.set(path, object);
  }

  /**
   * Clear all registrations
   */
  clear() {
    this.pathToMaterial.clear();
    this.pathToLight.clear();
    this.pathToCamera.clear();
    this.pathToObject.clear();
  }

  /**
   * Apply parameter update
   *
   * @param {Object} update - Update from BridgeClient
   * @param {Object} update.target - Target info (type, path)
   * @param {Object} update.changes - Changed parameters
   * @returns {boolean} True if update was applied
   */
  applyUpdate(update) {
    const { target, changes } = update;

    switch (target.type) {
      case TargetType.MATERIAL:
        return this.applyMaterialUpdate(target.path, changes);
      case TargetType.LIGHT:
        return this.applyLightUpdate(target.path, changes);
      case TargetType.CAMERA:
        return this.applyCameraUpdate(target.path, changes);
      case TargetType.TRANSFORM:
        return this.applyTransformUpdate(target.path, changes);
      default:
        console.warn(`Unknown target type: ${target.type}`);
        return false;
    }
  }

  /**
   * Apply material parameter changes
   */
  applyMaterialUpdate(path, changes) {
    const material = this.pathToMaterial.get(path);
    if (!material) {
      console.warn(`Material not found: ${path}`);
      return false;
    }

    for (const [param, value] of Object.entries(changes)) {
      const setter = MATERIAL_PARAM_MAP[param];
      if (setter) {
        try {
          setter(material, value);
        } catch (err) {
          console.error(`Failed to apply material param ${param}:`, err);
        }
      } else {
        console.warn(`Unknown material parameter: ${param}`);
      }
    }

    material.needsUpdate = true;
    return true;
  }

  /**
   * Apply light parameter changes
   */
  applyLightUpdate(path, changes) {
    const light = this.pathToLight.get(path);
    if (!light) {
      console.warn(`Light not found: ${path}`);
      return false;
    }

    for (const [param, value] of Object.entries(changes)) {
      const setter = LIGHT_PARAM_MAP[param];
      if (setter) {
        try {
          setter(light, value);
        } catch (err) {
          console.error(`Failed to apply light param ${param}:`, err);
        }
      } else {
        console.warn(`Unknown light parameter: ${param}`);
      }
    }

    return true;
  }

  /**
   * Apply camera parameter changes
   */
  applyCameraUpdate(path, changes) {
    const camera = this.pathToCamera.get(path);
    if (!camera) {
      console.warn(`Camera not found: ${path}`);
      return false;
    }

    for (const [param, value] of Object.entries(changes)) {
      const setter = CAMERA_PARAM_MAP[param];
      if (setter) {
        try {
          setter(camera, value, this.controls);
        } catch (err) {
          console.error(`Failed to apply camera param ${param}:`, err);
        }
      } else {
        console.warn(`Unknown camera parameter: ${param}`);
      }
    }

    return true;
  }

  /**
   * Apply transform parameter changes
   */
  applyTransformUpdate(path, changes) {
    const object = this.pathToObject.get(path);
    if (!object) {
      console.warn(`Object not found: ${path}`);
      return false;
    }

    for (const [param, value] of Object.entries(changes)) {
      const setter = TRANSFORM_PARAM_MAP[param];
      if (setter) {
        try {
          setter(object, value);
        } catch (err) {
          console.error(`Failed to apply transform param ${param}:`, err);
        }
      } else {
        console.warn(`Unknown transform parameter: ${param}`);
      }
    }

    return true;
  }

  /**
   * Get statistics about registered objects
   */
  getStats() {
    return {
      materials: this.pathToMaterial.size,
      lights: this.pathToLight.size,
      cameras: this.pathToCamera.size,
      objects: this.pathToObject.size
    };
  }
}

export default ParameterSync;
