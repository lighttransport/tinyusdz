/**
 * Light HDRI Projection Library
 * Projects Three.js area lights and sphere lights onto an environment map (HDRI)
 *
 * Supports both standalone mode (Node.js execution) and library mode (import/require)
 *
 * @module light-hdri-projection
 */

// ============================================
// Constants
// ============================================

const PI = Math.PI;
const TWO_PI = 2 * Math.PI;
const HALF_PI = Math.PI / 2;
const INV_PI = 1 / Math.PI;
const INV_TWO_PI = 1 / (2 * Math.PI);

// Default HDRI resolution
const DEFAULT_WIDTH = 1024;
const DEFAULT_HEIGHT = 512;

// Maximum HDR value to prevent infinity/NaN issues
const MAX_HDR_VALUE = 1e6;

/**
 * Sanitize a float value - clamp and replace NaN/Infinity with 0
 * @param {number} value - Input value
 * @param {number} [maxVal] - Maximum allowed value
 * @returns {number} Sanitized value
 */
function sanitizeFloat(value, maxVal = MAX_HDR_VALUE) {
  if (!Number.isFinite(value)) return 0;
  return Math.max(0, Math.min(value, maxVal));
}

// ============================================
// Vector Math Utilities (Three.js-compatible when available)
// ============================================

class Vec3 {
  constructor(x = 0, y = 0, z = 0) {
    this.x = x;
    this.y = y;
    this.z = z;
  }

  set(x, y, z) {
    this.x = x;
    this.y = y;
    this.z = z;
    return this;
  }

  copy(v) {
    this.x = v.x;
    this.y = v.y;
    this.z = v.z;
    return this;
  }

  clone() {
    return new Vec3(this.x, this.y, this.z);
  }

  add(v) {
    this.x += v.x;
    this.y += v.y;
    this.z += v.z;
    return this;
  }

  sub(v) {
    this.x -= v.x;
    this.y -= v.y;
    this.z -= v.z;
    return this;
  }

  multiplyScalar(s) {
    this.x *= s;
    this.y *= s;
    this.z *= s;
    return this;
  }

  divideScalar(s) {
    return this.multiplyScalar(1 / s);
  }

  length() {
    return Math.sqrt(this.x * this.x + this.y * this.y + this.z * this.z);
  }

  lengthSq() {
    return this.x * this.x + this.y * this.y + this.z * this.z;
  }

  normalize() {
    const len = this.length();
    if (len > 0) {
      this.multiplyScalar(1 / len);
    }
    return this;
  }

  dot(v) {
    return this.x * v.x + this.y * v.y + this.z * v.z;
  }

  cross(v) {
    const x = this.y * v.z - this.z * v.y;
    const y = this.z * v.x - this.x * v.z;
    const z = this.x * v.y - this.y * v.x;
    return new Vec3(x, y, z);
  }

  negate() {
    this.x = -this.x;
    this.y = -this.y;
    this.z = -this.z;
    return this;
  }

  distanceTo(v) {
    return Math.sqrt(this.distanceToSquared(v));
  }

  distanceToSquared(v) {
    const dx = this.x - v.x;
    const dy = this.y - v.y;
    const dz = this.z - v.z;
    return dx * dx + dy * dy + dz * dz;
  }

  applyMatrix4(m) {
    const x = this.x, y = this.y, z = this.z;
    const e = m.elements;
    const w = 1 / (e[3] * x + e[7] * y + e[11] * z + e[15]);
    this.x = (e[0] * x + e[4] * y + e[8] * z + e[12]) * w;
    this.y = (e[1] * x + e[5] * y + e[9] * z + e[13]) * w;
    this.z = (e[2] * x + e[6] * y + e[10] * z + e[14]) * w;
    return this;
  }

  static fromSpherical(theta, phi) {
    const sinPhi = Math.sin(phi);
    return new Vec3(
      sinPhi * Math.cos(theta),
      Math.cos(phi),
      sinPhi * Math.sin(theta)
    );
  }

  toSpherical() {
    const len = this.length();
    if (len === 0) return { theta: 0, phi: 0 };
    const phi = Math.acos(Math.max(-1, Math.min(1, this.y / len)));
    const theta = Math.atan2(this.z, this.x);
    return { theta, phi };
  }
}

class Color3 {
  constructor(r = 1, g = 1, b = 1) {
    this.r = r;
    this.g = g;
    this.b = b;
  }

  set(r, g, b) {
    this.r = r;
    this.g = g;
    this.b = b;
    return this;
  }

  copy(c) {
    this.r = c.r;
    this.g = c.g;
    this.b = c.b;
    return this;
  }

  clone() {
    return new Color3(this.r, this.g, this.b);
  }

  add(c) {
    this.r += c.r;
    this.g += c.g;
    this.b += c.b;
    return this;
  }

  multiplyScalar(s) {
    this.r *= s;
    this.g *= s;
    this.b *= s;
    return this;
  }

  multiply(c) {
    this.r *= c.r;
    this.g *= c.g;
    this.b *= c.b;
    return this;
  }

  setFromHex(hex) {
    this.r = ((hex >> 16) & 255) / 255;
    this.g = ((hex >> 8) & 255) / 255;
    this.b = (hex & 255) / 255;
    return this;
  }

  toArray() {
    return [this.r, this.g, this.b];
  }
}

// ============================================
// Light Source Definitions
// ============================================

/**
 * Sphere Light (Point Light with Physical Radius)
 */
class SphereLight {
  /**
   * @param {Object} options
   * @param {Vec3|Object} options.position - Light position {x, y, z}
   * @param {number} options.radius - Physical radius of the sphere light
   * @param {Color3|Object|number} options.color - Light color (RGB object or hex)
   * @param {number} options.intensity - Light intensity (in cd or lm, depending on use)
   * @param {Object} [options.texture] - Optional texture for colored emission
   */
  constructor(options = {}) {
    this.type = 'sphere';
    this.position = options.position instanceof Vec3
      ? options.position
      : new Vec3(options.position?.x || 0, options.position?.y || 0, options.position?.z || 0);
    this.radius = options.radius || 0.1;
    this.intensity = options.intensity || 1;

    // Handle color
    if (typeof options.color === 'number') {
      this.color = new Color3().setFromHex(options.color);
    } else if (options.color instanceof Color3) {
      this.color = options.color.clone();
    } else if (options.color) {
      this.color = new Color3(options.color.r || 1, options.color.g || 1, options.color.b || 1);
    } else {
      this.color = new Color3(1, 1, 1);
    }

    this.texture = options.texture || null;
  }

  /**
   * Calculate the radiance seen from a point looking at this light
   * @param {Vec3} viewPoint - The point from which we're viewing
   * @param {Vec3} direction - The viewing direction (normalized)
   * @param {number} maxDistance - Maximum distance to consider
   * @returns {Color3} Radiance contribution
   */
  getRadiance(viewPoint, direction, maxDistance) {
    // Calculate distance to light center
    const toLight = this.position.clone().sub(viewPoint);
    const distToCenter = toLight.length();

    // Beyond max distance, no contribution
    if (distToCenter > maxDistance + this.radius) {
      return new Color3(0, 0, 0);
    }

    // Normalize direction to light
    const lightDir = toLight.clone().normalize();

    // Check if viewing direction intersects the sphere
    // Ray-sphere intersection
    const a = 1; // direction is normalized
    const b = -2 * direction.dot(toLight);
    const c = toLight.lengthSq() - this.radius * this.radius;
    const discriminant = b * b - 4 * a * c;

    if (discriminant < 0) {
      // No intersection
      return new Color3(0, 0, 0);
    }

    // We have an intersection
    const t = (-b - Math.sqrt(discriminant)) / (2 * a);
    if (t < 0) {
      // Intersection behind view point (we're inside the sphere)
      // Return full intensity
      return this.color.clone().multiplyScalar(this.intensity);
    }

    // Calculate solid angle subtended by the sphere
    const sinTheta = Math.min(1, this.radius / distToCenter);
    const cosTheta = Math.sqrt(1 - sinTheta * sinTheta);
    const solidAngle = TWO_PI * (1 - cosTheta);

    // Radiance = Intensity / solid_angle (for uniform sphere)
    // But we want to output the intensity as seen per steradian
    const radiance = this.color.clone().multiplyScalar(this.intensity * solidAngle * INV_PI);

    // Sample texture if available
    if (this.texture) {
      const texColor = this._sampleTexture(direction, lightDir);
      radiance.multiply(texColor);
    }

    return radiance;
  }

  /**
   * Sample texture using direction
   * @private
   */
  _sampleTexture(viewDir, lightDir) {
    if (!this.texture || !this.texture.data) {
      return new Color3(1, 1, 1);
    }

    // Calculate UV from direction (spherical mapping)
    const relDir = viewDir.clone().sub(lightDir).normalize();
    const { theta, phi } = relDir.toSpherical();

    const u = (theta + PI) * INV_TWO_PI;
    const v = phi * INV_PI;

    return this._sampleTextureUV(u, v);
  }

  /**
   * Sample texture at UV coordinates
   * @private
   */
  _sampleTextureUV(u, v) {
    const tex = this.texture;
    const width = tex.width;
    const height = tex.height;
    const channels = tex.channels || 3;

    const x = Math.floor(u * width) % width;
    const y = Math.floor(v * height) % height;
    const idx = (y * width + x) * channels;

    const data = tex.data;
    const r = data[idx] || 0;
    const g = data[idx + 1] || 0;
    const b = data[idx + 2] || 0;

    // Normalize if uint8
    const maxVal = tex.isFloat ? 1 : 255;
    return new Color3(r / maxVal, g / maxVal, b / maxVal);
  }
}

/**
 * Area Light (Rectangle Light)
 */
class AreaLight {
  /**
   * @param {Object} options
   * @param {Vec3|Object} options.position - Light center position {x, y, z}
   * @param {Vec3|Object} options.normal - Light facing direction (normalized)
   * @param {Vec3|Object} options.tangent - Light width direction (normalized)
   * @param {number} options.width - Light width
   * @param {number} options.height - Light height
   * @param {Color3|Object|number} options.color - Light color
   * @param {number} options.intensity - Light intensity
   * @param {Object} [options.texture] - Optional texture for patterned emission
   * @param {boolean} [options.twoSided] - Whether light emits from both sides
   */
  constructor(options = {}) {
    this.type = 'area';
    this.position = options.position instanceof Vec3
      ? options.position
      : new Vec3(options.position?.x || 0, options.position?.y || 0, options.position?.z || 0);

    this.normal = options.normal instanceof Vec3
      ? options.normal.clone().normalize()
      : new Vec3(options.normal?.x || 0, options.normal?.y || -1, options.normal?.z || 0).normalize();

    this.tangent = options.tangent instanceof Vec3
      ? options.tangent.clone().normalize()
      : new Vec3(options.tangent?.x || 1, options.tangent?.y || 0, options.tangent?.z || 0).normalize();

    // Compute bitangent
    this.bitangent = this.normal.clone().cross(this.tangent).normalize();

    this.width = options.width || 1;
    this.height = options.height || 1;
    this.intensity = options.intensity || 1;
    this.twoSided = options.twoSided || false;

    // Handle color
    if (typeof options.color === 'number') {
      this.color = new Color3().setFromHex(options.color);
    } else if (options.color instanceof Color3) {
      this.color = options.color.clone();
    } else if (options.color) {
      this.color = new Color3(options.color.r || 1, options.color.g || 1, options.color.b || 1);
    } else {
      this.color = new Color3(1, 1, 1);
    }

    this.texture = options.texture || null;

    // Precompute corners
    this._computeCorners();
  }

  _computeCorners() {
    const hw = this.width / 2;
    const hh = this.height / 2;

    // Corners: top-left, top-right, bottom-right, bottom-left
    this.corners = [
      this.position.clone()
        .add(this.tangent.clone().multiplyScalar(-hw))
        .add(this.bitangent.clone().multiplyScalar(hh)),
      this.position.clone()
        .add(this.tangent.clone().multiplyScalar(hw))
        .add(this.bitangent.clone().multiplyScalar(hh)),
      this.position.clone()
        .add(this.tangent.clone().multiplyScalar(hw))
        .add(this.bitangent.clone().multiplyScalar(-hh)),
      this.position.clone()
        .add(this.tangent.clone().multiplyScalar(-hw))
        .add(this.bitangent.clone().multiplyScalar(-hh))
    ];
  }

  /**
   * Calculate the radiance seen from a point looking at this light
   * @param {Vec3} viewPoint - The point from which we're viewing
   * @param {Vec3} direction - The viewing direction (normalized)
   * @param {number} maxDistance - Maximum distance to consider
   * @returns {Color3} Radiance contribution
   */
  getRadiance(viewPoint, direction, maxDistance) {
    // Ray-plane intersection
    const denom = direction.dot(this.normal);

    // Check if ray is parallel to the light plane or facing away
    if (Math.abs(denom) < 1e-6) {
      return new Color3(0, 0, 0);
    }

    // Check if we're looking at the back of a one-sided light
    if (!this.twoSided && denom > 0) {
      return new Color3(0, 0, 0);
    }

    const toPlane = this.position.clone().sub(viewPoint);
    const t = toPlane.dot(this.normal) / denom;

    // Check if intersection is behind us or too far
    if (t < 0 || t > maxDistance) {
      return new Color3(0, 0, 0);
    }

    // Calculate intersection point
    const hitPoint = viewPoint.clone().add(direction.clone().multiplyScalar(t));

    // Check if hit point is within the rectangle
    const localHit = hitPoint.clone().sub(this.position);
    const u = localHit.dot(this.tangent);
    const v = localHit.dot(this.bitangent);

    const hw = this.width / 2;
    const hh = this.height / 2;

    if (Math.abs(u) > hw || Math.abs(v) > hh) {
      return new Color3(0, 0, 0);
    }

    // Calculate solid angle (approximation for distant lights)
    const distSq = Math.max(t * t, 1e-6); // Clamp to avoid division by zero
    const cosAngle = Math.abs(denom);
    const area = this.width * this.height;
    const solidAngle = Math.min((area * cosAngle) / distSq, 4 * PI); // Clamp to max 4π steradians

    // Radiance = Intensity / area (for uniform area light)
    const radiance = this.color.clone().multiplyScalar(this.intensity * solidAngle * INV_PI);

    // Sample texture if available
    if (this.texture) {
      const texU = (u / this.width) + 0.5;
      const texV = (v / this.height) + 0.5;
      const texColor = this._sampleTextureUV(texU, texV);
      radiance.multiply(texColor);
    }

    return radiance;
  }

  /**
   * Sample texture at UV coordinates
   * @private
   */
  _sampleTextureUV(u, v) {
    if (!this.texture || !this.texture.data) {
      return new Color3(1, 1, 1);
    }

    const tex = this.texture;
    const width = tex.width;
    const height = tex.height;
    const channels = tex.channels || 3;

    // Clamp UV
    u = Math.max(0, Math.min(1, u));
    v = Math.max(0, Math.min(1, v));

    const x = Math.floor(u * (width - 1));
    const y = Math.floor(v * (height - 1));
    const idx = (y * width + x) * channels;

    const data = tex.data;
    const r = data[idx] || 0;
    const g = data[idx + 1] || 0;
    const b = data[idx + 2] || 0;

    // Normalize if uint8
    const maxVal = tex.isFloat ? 1 : 255;
    return new Color3(r / maxVal, g / maxVal, b / maxVal);
  }
}

/**
 * Disk Light (Circular Area Light)
 */
class DiskLight {
  /**
   * @param {Object} options
   * @param {Vec3|Object} options.position - Light center position
   * @param {Vec3|Object} options.normal - Light facing direction
   * @param {number} options.radius - Light radius
   * @param {Color3|Object|number} options.color - Light color
   * @param {number} options.intensity - Light intensity
   * @param {boolean} [options.twoSided] - Whether light emits from both sides
   */
  constructor(options = {}) {
    this.type = 'disk';
    this.position = options.position instanceof Vec3
      ? options.position
      : new Vec3(options.position?.x || 0, options.position?.y || 0, options.position?.z || 0);

    this.normal = options.normal instanceof Vec3
      ? options.normal.clone().normalize()
      : new Vec3(options.normal?.x || 0, options.normal?.y || -1, options.normal?.z || 0).normalize();

    this.radius = options.radius || 0.5;
    this.intensity = options.intensity || 1;
    this.twoSided = options.twoSided || false;

    // Handle color
    if (typeof options.color === 'number') {
      this.color = new Color3().setFromHex(options.color);
    } else if (options.color instanceof Color3) {
      this.color = options.color.clone();
    } else if (options.color) {
      this.color = new Color3(options.color.r || 1, options.color.g || 1, options.color.b || 1);
    } else {
      this.color = new Color3(1, 1, 1);
    }

    this.texture = options.texture || null;
  }

  /**
   * Calculate the radiance seen from a point looking at this light
   */
  getRadiance(viewPoint, direction, maxDistance) {
    // Ray-plane intersection
    const denom = direction.dot(this.normal);

    if (Math.abs(denom) < 1e-6) {
      return new Color3(0, 0, 0);
    }

    if (!this.twoSided && denom > 0) {
      return new Color3(0, 0, 0);
    }

    const toPlane = this.position.clone().sub(viewPoint);
    const t = toPlane.dot(this.normal) / denom;

    if (t < 0 || t > maxDistance) {
      return new Color3(0, 0, 0);
    }

    // Calculate intersection point
    const hitPoint = viewPoint.clone().add(direction.clone().multiplyScalar(t));

    // Check if hit point is within the disk
    const dist = hitPoint.distanceTo(this.position);
    if (dist > this.radius) {
      return new Color3(0, 0, 0);
    }

    // Calculate solid angle
    const distSq = Math.max(t * t, 1e-6); // Clamp to avoid division by zero
    const cosAngle = Math.abs(denom);
    const area = PI * this.radius * this.radius;
    const solidAngle = Math.min((area * cosAngle) / distSq, 4 * PI); // Clamp to max 4π steradians

    const radiance = this.color.clone().multiplyScalar(this.intensity * solidAngle * INV_PI);

    return radiance;
  }
}

// ============================================
// HDRI Projection Engine
// ============================================

/**
 * Light HDRI Projection Engine
 * Projects light sources onto an equirectangular environment map
 */
class LightHDRIProjection {
  /**
   * @param {Object} options
   * @param {number} [options.width] - Output HDRI width
   * @param {number} [options.height] - Output HDRI height
   * @param {Vec3|Object} [options.center] - View center point for projection
   * @param {number} [options.maxDistance] - Maximum distance to consider for lights
   * @param {boolean} [options.useFloat32] - Use Float32 instead of Float16
   */
  constructor(options = {}) {
    this.width = options.width || DEFAULT_WIDTH;
    this.height = options.height || DEFAULT_HEIGHT;
    this.center = options.center instanceof Vec3
      ? options.center
      : new Vec3(options.center?.x || 0, options.center?.y || 0, options.center?.z || 0);
    this.maxDistance = options.maxDistance || 1000;
    this.useFloat32 = options.useFloat32 !== false;

    this.lights = [];
    this.outputBuffer = null;
  }

  /**
   * Add a light source to project
   * @param {SphereLight|AreaLight|DiskLight|Object} light - Light source
   * @returns {this}
   */
  addLight(light) {
    if (light.type === 'sphere' && !(light instanceof SphereLight)) {
      light = new SphereLight(light);
    } else if (light.type === 'area' && !(light instanceof AreaLight)) {
      light = new AreaLight(light);
    } else if (light.type === 'disk' && !(light instanceof DiskLight)) {
      light = new DiskLight(light);
    }

    this.lights.push(light);
    return this;
  }

  /**
   * Add a Three.js light (converts to internal format)
   * @param {THREE.Light} threeLight - Three.js light object
   * @returns {this}
   */
  addThreeJSLight(threeLight) {
    if (!threeLight) return this;

    const position = threeLight.position ? {
      x: threeLight.position.x,
      y: threeLight.position.y,
      z: threeLight.position.z
    } : { x: 0, y: 0, z: 0 };

    const color = threeLight.color ? {
      r: threeLight.color.r,
      g: threeLight.color.g,
      b: threeLight.color.b
    } : { r: 1, g: 1, b: 1 };

    const intensity = threeLight.intensity || 1;

    if (threeLight.isPointLight) {
      // Treat PointLight as SphereLight with small radius
      this.addLight(new SphereLight({
        position,
        radius: threeLight.distance > 0 ? 0.1 : 0.05,
        color,
        intensity
      }));
    } else if (threeLight.isRectAreaLight) {
      // RectAreaLight -> AreaLight
      const worldMatrix = threeLight.matrixWorld;
      const normal = new Vec3(0, 0, -1);
      const tangent = new Vec3(1, 0, 0);

      if (worldMatrix) {
        // Transform normal and tangent by world matrix (rotation only)
        const elements = worldMatrix.elements;
        const nx = normal.x, ny = normal.y, nz = normal.z;
        normal.x = elements[0] * nx + elements[4] * ny + elements[8] * nz;
        normal.y = elements[1] * nx + elements[5] * ny + elements[9] * nz;
        normal.z = elements[2] * nx + elements[6] * ny + elements[10] * nz;
        normal.normalize();

        const tx = tangent.x, ty = tangent.y, tz = tangent.z;
        tangent.x = elements[0] * tx + elements[4] * ty + elements[8] * tz;
        tangent.y = elements[1] * tx + elements[5] * ty + elements[9] * tz;
        tangent.z = elements[2] * tx + elements[6] * ty + elements[10] * tz;
        tangent.normalize();
      }

      this.addLight(new AreaLight({
        position,
        normal,
        tangent,
        width: threeLight.width || 1,
        height: threeLight.height || 1,
        color,
        intensity
      }));
    } else if (threeLight.isSpotLight) {
      // SpotLight -> SphereLight (simplified)
      this.addLight(new SphereLight({
        position,
        radius: 0.05,
        color,
        intensity
      }));
    }

    return this;
  }

  /**
   * Clear all lights
   * @returns {this}
   */
  clearLights() {
    this.lights = [];
    return this;
  }

  /**
   * Set the view center point
   * @param {Vec3|Object} center - Center point {x, y, z}
   * @returns {this}
   */
  setCenter(center) {
    this.center = center instanceof Vec3
      ? center
      : new Vec3(center?.x || 0, center?.y || 0, center?.z || 0);
    return this;
  }

  /**
   * Set maximum distance for light consideration
   * @param {number} distance - Maximum distance
   * @returns {this}
   */
  setMaxDistance(distance) {
    this.maxDistance = distance;
    return this;
  }

  /**
   * Set output resolution
   * @param {number} width - Output width
   * @param {number} height - Output height
   * @returns {this}
   */
  setResolution(width, height) {
    this.width = width;
    this.height = height || Math.floor(width / 2);
    return this;
  }

  /**
   * Convert pixel coordinates to direction
   * @param {number} x - Pixel x coordinate
   * @param {number} y - Pixel y coordinate
   * @returns {Vec3} Direction vector
   */
  pixelToDirection(x, y) {
    // Equirectangular mapping
    const u = (x + 0.5) / this.width;
    const v = (y + 0.5) / this.height;

    const theta = (u * 2 - 1) * PI; // longitude: -PI to PI
    const phi = v * PI;              // latitude: 0 to PI

    return Vec3.fromSpherical(theta, phi);
  }

  /**
   * Convert direction to pixel coordinates
   * @param {Vec3} direction - Direction vector
   * @returns {{x: number, y: number}} Pixel coordinates
   */
  directionToPixel(direction) {
    const { theta, phi } = direction.clone().normalize().toSpherical();

    const u = (theta / PI + 1) * 0.5;
    const v = phi / PI;

    return {
      x: Math.floor(u * this.width),
      y: Math.floor(v * this.height)
    };
  }

  /**
   * Generate the HDRI from the light sources
   * @param {Object} [options] - Generation options
   * @param {boolean} [options.additive] - Add to existing buffer instead of replacing
   * @param {Float32Array} [options.baseBuffer] - Base HDRI buffer to add lights to
   * @returns {Object} Generated HDRI data {data, width, height, channels}
   */
  generate(options = {}) {
    const channels = 3; // RGB
    const size = this.width * this.height * channels;

    // Create or reuse buffer
    if (options.baseBuffer && options.baseBuffer.length === size) {
      this.outputBuffer = new Float32Array(options.baseBuffer);
    } else if (options.additive && this.outputBuffer && this.outputBuffer.length === size) {
      // Keep existing buffer
    } else {
      this.outputBuffer = new Float32Array(size);
      // Explicitly initialize to 0.0
      this.outputBuffer.fill(0.0);
    }

    const buffer = this.outputBuffer;

    // For each pixel in the output image
    for (let y = 0; y < this.height; y++) {
      for (let x = 0; x < this.width; x++) {
        const direction = this.pixelToDirection(x, y);
        const idx = (y * this.width + x) * channels;

        // Accumulate radiance from all lights
        const radiance = new Color3(0, 0, 0);

        for (const light of this.lights) {
          const contribution = light.getRadiance(this.center, direction, this.maxDistance);
          radiance.add(contribution);
        }

        // Write to buffer with value sanitization (clamp NaN/Infinity)
        if (options.additive) {
          buffer[idx] = sanitizeFloat(buffer[idx] + radiance.r);
          buffer[idx + 1] = sanitizeFloat(buffer[idx + 1] + radiance.g);
          buffer[idx + 2] = sanitizeFloat(buffer[idx + 2] + radiance.b);
        } else {
          buffer[idx] = sanitizeFloat(radiance.r);
          buffer[idx + 1] = sanitizeFloat(radiance.g);
          buffer[idx + 2] = sanitizeFloat(radiance.b);
        }
      }
    }

    return {
      data: buffer,
      width: this.width,
      height: this.height,
      channels: channels,
      isFloat: true
    };
  }

  /**
   * Generate HDRI with supersampling for better quality
   * @param {number} [samples] - Number of samples per pixel (default 4)
   * @param {Object} [options] - Generation options
   * @returns {Object} Generated HDRI data
   */
  generateSupersampled(samples = 4, options = {}) {
    const channels = 3;
    const size = this.width * this.height * channels;

    if (options.baseBuffer && options.baseBuffer.length === size) {
      this.outputBuffer = new Float32Array(options.baseBuffer);
    } else {
      this.outputBuffer = new Float32Array(size);
      // Explicitly initialize to 0.0
      this.outputBuffer.fill(0.0);
    }

    const buffer = this.outputBuffer;
    const sqrtSamples = Math.ceil(Math.sqrt(samples));
    const actualSamples = sqrtSamples * sqrtSamples;
    const invSamples = 1 / actualSamples;

    for (let y = 0; y < this.height; y++) {
      for (let x = 0; x < this.width; x++) {
        const idx = (y * this.width + x) * channels;
        const radiance = new Color3(0, 0, 0);

        // Stratified sampling
        for (let sy = 0; sy < sqrtSamples; sy++) {
          for (let sx = 0; sx < sqrtSamples; sx++) {
            const jx = (sx + 0.5) / sqrtSamples - 0.5;
            const jy = (sy + 0.5) / sqrtSamples - 0.5;

            const direction = this.pixelToDirection(x + jx, y + jy);

            for (const light of this.lights) {
              const contribution = light.getRadiance(this.center, direction, this.maxDistance);
              radiance.add(contribution.multiplyScalar(invSamples));
            }
          }
        }

        // Write to buffer with value sanitization (clamp NaN/Infinity)
        if (options.additive) {
          buffer[idx] = sanitizeFloat(buffer[idx] + radiance.r);
          buffer[idx + 1] = sanitizeFloat(buffer[idx + 1] + radiance.g);
          buffer[idx + 2] = sanitizeFloat(buffer[idx + 2] + radiance.b);
        } else {
          buffer[idx] = sanitizeFloat(radiance.r);
          buffer[idx + 1] = sanitizeFloat(radiance.g);
          buffer[idx + 2] = sanitizeFloat(radiance.b);
        }
      }
    }

    return {
      data: buffer,
      width: this.width,
      height: this.height,
      channels: channels,
      isFloat: true
    };
  }

  /**
   * Convert generated HDRI to Three.js DataTexture
   * @param {Object} [hdriData] - HDRI data (uses last generated if not provided)
   * @returns {Object|null} Three.js-compatible texture config
   */
  toThreeJSTexture(hdriData) {
    const data = hdriData || this.generate();

    if (!data || !data.data) {
      return null;
    }

    // Return texture configuration for Three.js
    return {
      data: data.data,
      width: data.width,
      height: data.height,
      format: 'RGBFormat',
      type: 'FloatType',
      mapping: 'EquirectangularReflectionMapping',
      wrapS: 'RepeatWrapping',
      wrapT: 'ClampToEdgeWrapping',
      magFilter: 'LinearFilter',
      minFilter: 'LinearMipmapLinearFilter',
      generateMipmaps: true,
      colorSpace: 'LinearSRGBColorSpace'
    };
  }

  /**
   * Create actual Three.js DataTexture (requires Three.js)
   * @param {Object} THREE - Three.js module
   * @param {Object} [hdriData] - HDRI data (uses last generated if not provided)
   * @returns {THREE.DataTexture|null}
   */
  createThreeJSTexture(THREE, hdriData) {
    if (!THREE) {
      throw new Error('Three.js module required');
    }

    const data = hdriData || this.generate();
    if (!data || !data.data) {
      return null;
    }

    // Expand RGB to RGBA for Three.js compatibility
    const rgbaData = new Float32Array(data.width * data.height * 4);
    for (let i = 0; i < data.width * data.height; i++) {
      rgbaData[i * 4] = data.data[i * 3];
      rgbaData[i * 4 + 1] = data.data[i * 3 + 1];
      rgbaData[i * 4 + 2] = data.data[i * 3 + 2];
      rgbaData[i * 4 + 3] = 1.0;
    }

    const texture = new THREE.DataTexture(
      rgbaData,
      data.width,
      data.height,
      THREE.RGBAFormat,
      THREE.FloatType
    );

    texture.mapping = THREE.EquirectangularReflectionMapping;
    texture.wrapS = THREE.RepeatWrapping;
    texture.wrapT = THREE.ClampToEdgeWrapping;
    texture.magFilter = THREE.LinearFilter;
    texture.minFilter = THREE.LinearMipmapLinearFilter;
    texture.generateMipmaps = true;
    texture.colorSpace = THREE.LinearSRGBColorSpace;
    texture.needsUpdate = true;

    return texture;
  }

  /**
   * Export HDRI data as raw float array
   * @param {Object} [hdriData] - HDRI data (uses last generated if not provided)
   * @returns {Float32Array}
   */
  toFloat32Array(hdriData) {
    const data = hdriData || this.generate();
    return new Float32Array(data.data);
  }

  /**
   * Export HDRI data as Uint8 array (LDR, tone-mapped)
   * @param {Object} [hdriData] - HDRI data
   * @param {number} [exposure] - Exposure adjustment (default 1)
   * @param {number} [gamma] - Gamma correction (default 2.2)
   * @returns {Uint8Array}
   */
  toUint8Array(hdriData, exposure = 1, gamma = 2.2) {
    const data = hdriData || this.generate();
    const output = new Uint8Array(data.width * data.height * 3);
    const invGamma = 1 / gamma;

    for (let i = 0; i < data.width * data.height; i++) {
      for (let c = 0; c < 3; c++) {
        // Apply exposure and Reinhard tone mapping
        let value = data.data[i * 3 + c] * exposure;
        value = value / (1 + value); // Reinhard
        value = Math.pow(value, invGamma); // Gamma
        output[i * 3 + c] = Math.floor(Math.max(0, Math.min(255, value * 255)));
      }
    }

    return output;
  }
}

// ============================================
// EXR Writer (Pure JavaScript with ZIP compression)
// ============================================

/**
 * OpenEXR file writer with ZIP compression
 * Supports half-float (float16) and full float (float32) pixel types
 */
class EXRWriter {
  /**
   * @param {Object} options
   * @param {number} options.width - Image width
   * @param {number} options.height - Image height
   * @param {string} [options.compression] - Compression type: 'none', 'zip', 'zips' (default: 'zip')
   * @param {string} [options.pixelType] - Pixel type: 'half', 'float' (default: 'half')
   * @param {string[]} [options.channels] - Channel names (default: ['B', 'G', 'R'])
   */
  constructor(options = {}) {
    this.width = options.width || 1;
    this.height = options.height || 1;
    this.compression = options.compression || 'zip';
    this.pixelType = options.pixelType || 'half';
    this.channels = options.channels || ['B', 'G', 'R']; // EXR stores BGR order typically

    // Compression constants
    this.COMPRESSION_NONE = 0;
    this.COMPRESSION_RLE = 1;
    this.COMPRESSION_ZIPS = 2;  // ZIP single scanline
    this.COMPRESSION_ZIP = 3;   // ZIP 16 scanlines
    this.COMPRESSION_PIZ = 4;
    this.COMPRESSION_PXR24 = 5;
    this.COMPRESSION_B44 = 6;
    this.COMPRESSION_B44A = 7;
    this.COMPRESSION_DWAA = 8;
    this.COMPRESSION_DWAB = 9;

    // Pixel type constants
    this.PIXEL_TYPE_UINT = 0;
    this.PIXEL_TYPE_HALF = 1;
    this.PIXEL_TYPE_FLOAT = 2;

    // Lines per block for ZIP compression
    this.linesPerBlock = this.compression === 'zips' ? 1 : 16;
  }

  /**
   * Convert float32 to float16 (half precision)
   * @param {number} value - Float32 value
   * @returns {number} Float16 as uint16
   */
  floatToHalf(value) {
    const floatView = new Float32Array(1);
    const int32View = new Int32Array(floatView.buffer);

    floatView[0] = value;
    const x = int32View[0];

    // Extract components
    const sign = (x >> 31) & 0x1;
    let exp = (x >> 23) & 0xff;
    let mantissa = x & 0x7fffff;

    // Handle special cases
    if (exp === 0) {
      // Zero or denormal
      return sign << 15;
    } else if (exp === 0xff) {
      // Infinity or NaN
      if (mantissa === 0) {
        return (sign << 15) | 0x7c00; // Infinity
      } else {
        return (sign << 15) | 0x7c00 | (mantissa >> 13); // NaN
      }
    }

    // Rebias exponent
    exp = exp - 127 + 15;

    if (exp >= 31) {
      // Overflow to infinity
      return (sign << 15) | 0x7c00;
    } else if (exp <= 0) {
      // Underflow to zero or denormal
      if (exp < -10) {
        return sign << 15;
      }
      // Denormal
      mantissa = (mantissa | 0x800000) >> (1 - exp);
      return (sign << 15) | (mantissa >> 13);
    }

    return (sign << 15) | (exp << 10) | (mantissa >> 13);
  }

  /**
   * Write a null-terminated string
   * @param {DataView} view - DataView to write to
   * @param {number} offset - Byte offset
   * @param {string} str - String to write
   * @returns {number} New offset after string
   */
  writeString(view, offset, str) {
    for (let i = 0; i < str.length; i++) {
      view.setUint8(offset++, str.charCodeAt(i));
    }
    view.setUint8(offset++, 0); // null terminator
    return offset;
  }

  /**
   * Write an EXR attribute
   * @param {DataView} view - DataView to write to
   * @param {number} offset - Byte offset
   * @param {string} name - Attribute name
   * @param {string} type - Attribute type
   * @param {*} value - Attribute value
   * @returns {number} New offset
   */
  writeAttribute(view, offset, name, type, value) {
    // Write name
    offset = this.writeString(view, offset, name);
    // Write type
    offset = this.writeString(view, offset, type);

    // Write size and value based on type
    switch (type) {
      case 'int': {
        view.setInt32(offset, 4, true);
        offset += 4;
        view.setInt32(offset, value, true);
        offset += 4;
        break;
      }
      case 'float': {
        view.setInt32(offset, 4, true);
        offset += 4;
        view.setFloat32(offset, value, true);
        offset += 4;
        break;
      }
      case 'double': {
        view.setInt32(offset, 8, true);
        offset += 4;
        view.setFloat64(offset, value, true);
        offset += 8;
        break;
      }
      case 'box2i': {
        view.setInt32(offset, 16, true);
        offset += 4;
        view.setInt32(offset, value.xMin, true); offset += 4;
        view.setInt32(offset, value.yMin, true); offset += 4;
        view.setInt32(offset, value.xMax, true); offset += 4;
        view.setInt32(offset, value.yMax, true); offset += 4;
        break;
      }
      case 'box2f': {
        view.setInt32(offset, 16, true);
        offset += 4;
        view.setFloat32(offset, value.xMin, true); offset += 4;
        view.setFloat32(offset, value.yMin, true); offset += 4;
        view.setFloat32(offset, value.xMax, true); offset += 4;
        view.setFloat32(offset, value.yMax, true); offset += 4;
        break;
      }
      case 'v2i': {
        view.setInt32(offset, 8, true);
        offset += 4;
        view.setInt32(offset, value.x, true); offset += 4;
        view.setInt32(offset, value.y, true); offset += 4;
        break;
      }
      case 'v2f': {
        view.setInt32(offset, 8, true);
        offset += 4;
        view.setFloat32(offset, value.x, true); offset += 4;
        view.setFloat32(offset, value.y, true); offset += 4;
        break;
      }
      case 'chlist': {
        // Channel list
        let size = 0;
        for (const ch of value) {
          size += ch.name.length + 1 + 16; // name + null + 4 ints
        }
        size += 1; // terminating null

        view.setInt32(offset, size, true);
        offset += 4;

        for (const ch of value) {
          offset = this.writeString(view, offset, ch.name);
          view.setInt32(offset, ch.pixelType, true); offset += 4;
          view.setUint8(offset++, ch.pLinear ? 1 : 0);
          offset += 3; // reserved
          view.setInt32(offset, ch.xSampling, true); offset += 4;
          view.setInt32(offset, ch.ySampling, true); offset += 4;
        }
        view.setUint8(offset++, 0); // terminating null
        break;
      }
      case 'compression': {
        view.setInt32(offset, 1, true);
        offset += 4;
        view.setUint8(offset++, value);
        break;
      }
      case 'lineOrder': {
        view.setInt32(offset, 1, true);
        offset += 4;
        view.setUint8(offset++, value);
        break;
      }
      case 'string': {
        const strLen = value.length;
        view.setInt32(offset, strLen, true);
        offset += 4;
        for (let i = 0; i < strLen; i++) {
          view.setUint8(offset++, value.charCodeAt(i));
        }
        break;
      }
      default:
        throw new Error(`Unknown attribute type: ${type}`);
    }

    return offset;
  }

  /**
   * Compress data using ZIP (deflate)
   * @param {Uint8Array} data - Data to compress
   * @returns {Promise<Uint8Array>} Compressed data
   */
  async compressZip(data) {
    // Try to use available compression libraries

    // Node.js zlib
    if (typeof process !== 'undefined' && process.versions && process.versions.node) {
      const zlib = await import('zlib');
      return new Promise((resolve, reject) => {
        zlib.deflate(data, { level: 6 }, (err, result) => {
          if (err) reject(err);
          else resolve(new Uint8Array(result));
        });
      });
    }

    // Browser: try fflate if available
    if (typeof window !== 'undefined' && window.fflate) {
      return window.fflate.deflateSync(data, { level: 6 });
    }

    // Browser: try pako if available
    if (typeof window !== 'undefined' && window.pako) {
      return window.pako.deflate(data, { level: 6 });
    }

    // Browser: try CompressionStream API (modern browsers)
    if (typeof CompressionStream !== 'undefined') {
      const cs = new CompressionStream('deflate');
      const writer = cs.writable.getWriter();
      writer.write(data);
      writer.close();

      const chunks = [];
      const reader = cs.readable.getReader();
      while (true) {
        const { done, value } = await reader.read();
        if (done) break;
        chunks.push(value);
      }

      const totalLength = chunks.reduce((sum, chunk) => sum + chunk.length, 0);
      const result = new Uint8Array(totalLength);
      let offset = 0;
      for (const chunk of chunks) {
        result.set(chunk, offset);
        offset += chunk.length;
      }
      return result;
    }

    // Fallback: return uncompressed (caller should handle)
    console.warn('No compression library available, using uncompressed');
    return null;
  }

  /**
   * Write EXR file from HDRI data
   * @param {Object} hdriData - HDRI data with {data, width, height, channels}
   * @returns {Promise<Uint8Array>} EXR file as Uint8Array
   */
  async write(hdriData) {
    const width = hdriData.width;
    const height = hdriData.height;
    const numChannels = this.channels.length;
    const pixelSize = this.pixelType === 'half' ? 2 : 4;
    const pixelTypeConst = this.pixelType === 'half' ? this.PIXEL_TYPE_HALF : this.PIXEL_TYPE_FLOAT;

    // Determine compression type
    let compressionType;
    switch (this.compression) {
      case 'none': compressionType = this.COMPRESSION_NONE; break;
      case 'zips': compressionType = this.COMPRESSION_ZIPS; break;
      case 'zip':
      default: compressionType = this.COMPRESSION_ZIP; break;
    }

    // Build header
    const headerSize = 1024; // Generous estimate
    const headerBuffer = new ArrayBuffer(headerSize);
    const headerView = new DataView(headerBuffer);
    let offset = 0;

    // Magic number and version
    headerView.setUint32(offset, 20000630, true); // EXR magic number
    offset += 4;
    headerView.setUint32(offset, 2, true); // Version 2, single-part scanline
    offset += 4;

    // Attributes
    // channels
    const channelList = this.channels.map(name => ({
      name: name,
      pixelType: pixelTypeConst,
      pLinear: false,
      xSampling: 1,
      ySampling: 1
    }));
    offset = this.writeAttribute(headerView, offset, 'channels', 'chlist', channelList);

    // compression
    offset = this.writeAttribute(headerView, offset, 'compression', 'compression', compressionType);

    // dataWindow
    offset = this.writeAttribute(headerView, offset, 'dataWindow', 'box2i', {
      xMin: 0, yMin: 0, xMax: width - 1, yMax: height - 1
    });

    // displayWindow
    offset = this.writeAttribute(headerView, offset, 'displayWindow', 'box2i', {
      xMin: 0, yMin: 0, xMax: width - 1, yMax: height - 1
    });

    // lineOrder
    offset = this.writeAttribute(headerView, offset, 'lineOrder', 'lineOrder', 0); // INCREASING_Y

    // pixelAspectRatio
    offset = this.writeAttribute(headerView, offset, 'pixelAspectRatio', 'float', 1.0);

    // screenWindowCenter
    offset = this.writeAttribute(headerView, offset, 'screenWindowCenter', 'v2f', { x: 0, y: 0 });

    // screenWindowWidth
    offset = this.writeAttribute(headerView, offset, 'screenWindowWidth', 'float', 1.0);

    // End of header
    headerView.setUint8(offset++, 0);

    const actualHeaderSize = offset;

    // Calculate number of scanline blocks
    const numBlocks = Math.ceil(height / this.linesPerBlock);

    // Offset table size
    const offsetTableSize = numBlocks * 8; // 64-bit offsets

    // Prepare scanline data
    const scanlineBlocks = [];
    const srcData = hdriData.data;

    for (let blockIdx = 0; blockIdx < numBlocks; blockIdx++) {
      const startY = blockIdx * this.linesPerBlock;
      const endY = Math.min(startY + this.linesPerBlock, height);
      const blockHeight = endY - startY;

      // Prepare interleaved channel data for this block
      // EXR stores channels separately, then interleaved within scanlines
      const blockDataSize = width * blockHeight * numChannels * pixelSize;
      const blockData = new Uint8Array(blockDataSize);
      const blockView = new DataView(blockData.buffer);

      let blockOffset = 0;

      // For each scanline in the block
      for (let y = startY; y < endY; y++) {
        // For each channel (in reverse order: B, G, R for typical RGB input)
        for (let c = 0; c < numChannels; c++) {
          // Map channel index (EXR channels are alphabetically sorted: B, G, R)
          const srcChannel = numChannels - 1 - c; // Reverse: R->B, G->G, B->R

          // For each pixel in the scanline
          for (let x = 0; x < width; x++) {
            const srcIdx = (y * width + x) * 3 + srcChannel;
            const value = srcData[srcIdx] || 0;

            if (this.pixelType === 'half') {
              const halfValue = this.floatToHalf(value);
              blockView.setUint16(blockOffset, halfValue, true);
              blockOffset += 2;
            } else {
              blockView.setFloat32(blockOffset, value, true);
              blockOffset += 4;
            }
          }
        }
      }

      // Compress block if needed
      let compressedData;
      if (compressionType === this.COMPRESSION_NONE) {
        compressedData = blockData;
      } else {
        // Apply predictor for better compression
        const predictedData = this.applyPredictor(blockData, pixelSize);
        const compressed = await this.compressZip(predictedData);

        if (compressed && compressed.length < blockData.length) {
          compressedData = compressed;
        } else {
          // Compression didn't help, store uncompressed
          compressedData = blockData;
        }
      }

      scanlineBlocks.push({
        y: startY,
        data: compressedData,
        uncompressedSize: blockDataSize
      });
    }

    // Calculate total file size
    let totalDataSize = 0;
    for (const block of scanlineBlocks) {
      totalDataSize += 4 + 4 + block.data.length; // y-coord (4) + size (4) + data
    }

    const totalSize = actualHeaderSize + offsetTableSize + totalDataSize;

    // Build final file
    const fileBuffer = new ArrayBuffer(totalSize);
    const fileView = new DataView(fileBuffer);
    const fileArray = new Uint8Array(fileBuffer);

    // Copy header
    fileArray.set(new Uint8Array(headerBuffer, 0, actualHeaderSize), 0);
    offset = actualHeaderSize;

    // Calculate and write offset table
    let dataOffset = actualHeaderSize + offsetTableSize;
    for (let i = 0; i < numBlocks; i++) {
      // Write 64-bit offset (as two 32-bit values, little endian)
      fileView.setUint32(offset, dataOffset & 0xFFFFFFFF, true);
      fileView.setUint32(offset + 4, Math.floor(dataOffset / 0x100000000), true);
      offset += 8;

      dataOffset += 4 + 4 + scanlineBlocks[i].data.length;
    }

    // Write scanline blocks
    for (const block of scanlineBlocks) {
      fileView.setInt32(offset, block.y, true);
      offset += 4;
      fileView.setInt32(offset, block.data.length, true);
      offset += 4;
      fileArray.set(block.data, offset);
      offset += block.data.length;
    }

    return new Uint8Array(fileBuffer);
  }

  /**
   * Apply predictor for better ZIP compression
   * @param {Uint8Array} data - Input data
   * @param {number} pixelSize - Bytes per pixel component
   * @returns {Uint8Array} Predicted data
   */
  applyPredictor(data, pixelSize) {
    const result = new Uint8Array(data.length);

    // Horizontal differencing predictor
    // Process each row of bytes
    for (let i = 0; i < data.length; i++) {
      if (i < pixelSize) {
        result[i] = data[i];
      } else {
        result[i] = (data[i] - data[i - pixelSize]) & 0xFF;
      }
    }

    return result;
  }
}

/**
 * Write HDRI data to EXR format
 * @param {Object} hdriData - HDRI data {data, width, height}
 * @param {Object} [options] - Options {compression, pixelType}
 * @returns {Promise<Uint8Array>} EXR file data
 */
async function writeEXR(hdriData, options = {}) {
  const writer = new EXRWriter({
    width: hdriData.width,
    height: hdriData.height,
    compression: options.compression || 'zip',
    pixelType: options.pixelType || 'half',
    channels: options.channels || ['B', 'G', 'R']
  });

  return writer.write(hdriData);
}

// ============================================
// Convenience Functions
// ============================================

/**
 * Quick projection of a single sphere light
 * @param {Object} lightConfig - Sphere light configuration
 * @param {Object} options - Projection options
 * @returns {Object} HDRI data
 */
function projectSphereLight(lightConfig, options = {}) {
  const engine = new LightHDRIProjection(options);
  engine.addLight(new SphereLight(lightConfig));
  return engine.generate();
}

/**
 * Quick projection of a single area light
 * @param {Object} lightConfig - Area light configuration
 * @param {Object} options - Projection options
 * @returns {Object} HDRI data
 */
function projectAreaLight(lightConfig, options = {}) {
  const engine = new LightHDRIProjection(options);
  engine.addLight(new AreaLight(lightConfig));
  return engine.generate();
}

/**
 * Project multiple lights to HDRI
 * @param {Array} lights - Array of light configurations
 * @param {Object} options - Projection options
 * @returns {Object} HDRI data
 */
function projectLights(lights, options = {}) {
  const engine = new LightHDRIProjection(options);
  for (const light of lights) {
    engine.addLight(light);
  }
  return engine.generate();
}

// ============================================
// Export / Module Definition
// ============================================

const exports = {
  // Classes
  LightHDRIProjection,
  SphereLight,
  AreaLight,
  DiskLight,
  Vec3,
  Color3,
  EXRWriter,

  // Convenience functions
  projectSphereLight,
  projectAreaLight,
  projectLights,
  writeEXR,

  // Constants
  DEFAULT_WIDTH,
  DEFAULT_HEIGHT
};

// Support both ES modules and CommonJS
if (typeof module !== 'undefined' && module.exports) {
  module.exports = exports;
}

if (typeof window !== 'undefined') {
  window.LightHDRIProjection = LightHDRIProjection;
  window.SphereLight = SphereLight;
  window.AreaLight = AreaLight;
  window.DiskLight = DiskLight;
  window.EXRWriter = EXRWriter;
  window.projectSphereLight = projectSphereLight;
  window.projectAreaLight = projectAreaLight;
  window.projectLights = projectLights;
  window.writeEXR = writeEXR;
}

export {
  LightHDRIProjection,
  SphereLight,
  AreaLight,
  DiskLight,
  Vec3,
  Color3,
  EXRWriter,
  projectSphereLight,
  projectAreaLight,
  projectLights,
  writeEXR,
  DEFAULT_WIDTH,
  DEFAULT_HEIGHT
};

export default LightHDRIProjection;

// CLI is available via light-hdri-projection-cli.js
