// Scene State Manager for Blender Bridge
// Tracks server-side scene state for delta detection and state sync

/**
 * SceneState maintains the current state of a scene
 * for computing deltas and syncing new browser clients
 */
export class SceneState {
  constructor() {
    // Last uploaded scene binary info
    this.scene = null;

    // Current parameter values by path
    this.materials = new Map();  // path -> material params
    this.lights = new Map();     // path -> light params
    this.cameras = new Map();    // path -> camera params
    this.transforms = new Map(); // path -> transform params

    // Scene metadata
    this.metadata = {
      name: null,
      format: null,
      uploadedAt: null,
      blenderVersion: null
    };

    // For tracking changes
    this.lastUpdateTime = null;
  }

  /**
   * Update scene data (full scene upload)
   *
   * @param {Object} sceneInfo - Scene info from message header
   * @param {Uint8Array} binaryData - USDZ binary data
   */
  setScene(sceneInfo, binaryData) {
    this.scene = {
      data: binaryData,
      byteLength: binaryData.length
    };

    this.metadata.name = sceneInfo.name;
    this.metadata.format = sceneInfo.format;
    this.metadata.uploadedAt = Date.now();

    // Clear parameter caches on new scene
    this.materials.clear();
    this.lights.clear();
    this.cameras.clear();
    this.transforms.clear();

    this.lastUpdateTime = Date.now();
  }

  /**
   * Update material parameters
   *
   * @param {string} path - Material path
   * @param {Object} changes - Changed parameters
   * @returns {Object} Delta (only actually changed values)
   */
  updateMaterial(path, changes) {
    const current = this.materials.get(path) || {};
    const delta = {};

    for (const [key, value] of Object.entries(changes)) {
      if (!deepEqual(current[key], value)) {
        delta[key] = value;
        current[key] = value;
      }
    }

    this.materials.set(path, current);
    this.lastUpdateTime = Date.now();

    return delta;
  }

  /**
   * Update light parameters
   *
   * @param {string} path - Light path
   * @param {Object} changes - Changed parameters
   * @returns {Object} Delta
   */
  updateLight(path, changes) {
    const current = this.lights.get(path) || {};
    const delta = {};

    for (const [key, value] of Object.entries(changes)) {
      if (!deepEqual(current[key], value)) {
        delta[key] = value;
        current[key] = value;
      }
    }

    this.lights.set(path, current);
    this.lastUpdateTime = Date.now();

    return delta;
  }

  /**
   * Update camera parameters
   *
   * @param {string} path - Camera path
   * @param {Object} changes - Changed parameters
   * @returns {Object} Delta
   */
  updateCamera(path, changes) {
    const current = this.cameras.get(path) || {};
    const delta = {};

    for (const [key, value] of Object.entries(changes)) {
      if (!deepEqual(current[key], value)) {
        delta[key] = value;
        current[key] = value;
      }
    }

    this.cameras.set(path, current);
    this.lastUpdateTime = Date.now();

    return delta;
  }

  /**
   * Update transform parameters
   *
   * @param {string} path - Object path
   * @param {Object} changes - Changed parameters
   * @returns {Object} Delta
   */
  updateTransform(path, changes) {
    const current = this.transforms.get(path) || {};
    const delta = {};

    for (const [key, value] of Object.entries(changes)) {
      if (!deepEqual(current[key], value)) {
        delta[key] = value;
        current[key] = value;
      }
    }

    this.transforms.set(path, current);
    this.lastUpdateTime = Date.now();

    return delta;
  }

  /**
   * Get current state for a material
   *
   * @param {string} path
   * @returns {Object|null}
   */
  getMaterial(path) {
    return this.materials.get(path) || null;
  }

  /**
   * Get current state for a light
   *
   * @param {string} path
   * @returns {Object|null}
   */
  getLight(path) {
    return this.lights.get(path) || null;
  }

  /**
   * Get current state for a camera
   *
   * @param {string} path
   * @returns {Object|null}
   */
  getCamera(path) {
    return this.cameras.get(path) || null;
  }

  /**
   * Get current state for a transform
   *
   * @param {string} path
   * @returns {Object|null}
   */
  getTransform(path) {
    return this.transforms.get(path) || null;
  }

  /**
   * Check if scene data is available
   *
   * @returns {boolean}
   */
  hasScene() {
    return this.scene !== null;
  }

  /**
   * Get scene data for sending to new browser clients
   *
   * @returns {{ sceneInfo: Object, binaryData: Uint8Array }|null}
   */
  getSceneForSync() {
    if (!this.scene) return null;

    return {
      sceneInfo: {
        name: this.metadata.name,
        format: this.metadata.format,
        byteLength: this.scene.byteLength
      },
      binaryData: this.scene.data
    };
  }

  /**
   * Get all current parameter states for syncing a new client
   *
   * @returns {Object}
   */
  getAllParametersForSync() {
    return {
      materials: Object.fromEntries(this.materials),
      lights: Object.fromEntries(this.lights),
      cameras: Object.fromEntries(this.cameras),
      transforms: Object.fromEntries(this.transforms)
    };
  }

  /**
   * Clear all state
   */
  clear() {
    this.scene = null;
    this.materials.clear();
    this.lights.clear();
    this.cameras.clear();
    this.transforms.clear();
    this.metadata = {
      name: null,
      format: null,
      uploadedAt: null,
      blenderVersion: null
    };
    this.lastUpdateTime = null;
  }
}

/**
 * Deep equality check for arrays and objects
 */
function deepEqual(a, b) {
  if (a === b) return true;
  if (a == null || b == null) return false;
  if (typeof a !== typeof b) return false;

  if (Array.isArray(a)) {
    if (!Array.isArray(b)) return false;
    if (a.length !== b.length) return false;
    return a.every((val, i) => deepEqual(val, b[i]));
  }

  if (typeof a === 'object') {
    const keysA = Object.keys(a);
    const keysB = Object.keys(b);
    if (keysA.length !== keysB.length) return false;
    return keysA.every(key => deepEqual(a[key], b[key]));
  }

  return false;
}

export default SceneState;
