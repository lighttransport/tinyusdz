import * as THREE from 'three';

const D65 = [0.3127, 0.3290];

const LINEAR_SPACES = Object.freeze({
  lin_ap0_scene: [[0.73485524337371, 0.26422532524554],
    [-0.0061709124786224, 1.0113149590212864],
    [0.015967559255041, -0.064235503128551], D65],
  lin_ap1_scene: [[0.71319588766205, 0.29268891446333],
    [0.15950855654178, 0.83878851615096],
    [0.128672995285350, 0.043895571160528], D65],
  lin_rec709_scene: [[0.64, 0.33], [0.30, 0.60], [0.15, 0.06], D65],
  lin_p3d65_scene: [[0.68, 0.32], [0.265, 0.69], [0.15, 0.06], D65],
  lin_rec2020_scene: [[0.708, 0.292], [0.17, 0.797], [0.131, 0.046], D65],
  lin_adobergb_scene: [[0.64, 0.33], [0.21, 0.71], [0.15, 0.06], D65]
});

const ALIASES = Object.freeze({
  acescg: 'lin_ap1_scene', lin_acescg: 'lin_ap1_scene',
  lin_ap1: 'lin_ap1_scene',
  'aces2065-1': 'lin_ap0_scene', lin_ap0: 'lin_ap0_scene',
  lin_srgb: 'lin_rec709_scene', lin_rec709: 'lin_rec709_scene',
  linear: 'lin_rec709_scene', srgb: 'srgb_rec709_scene',
  sRGB: 'srgb_rec709_scene', srgb_texture: 'srgb_rec709_scene',
  lin_displayp3: 'lin_p3d65_scene', srgb_displayp3: 'srgb_p3d65_scene',
  lin_rec2020: 'lin_rec2020_scene', lin_adobergb: 'lin_adobergb_scene',
  adobergb: 'g22_adobergb_scene'
});

function invert3(m) {
  const d = m[0] * (m[4] * m[8] - m[5] * m[7]) -
    m[1] * (m[3] * m[8] - m[5] * m[6]) +
    m[2] * (m[3] * m[7] - m[4] * m[6]);
  return [(m[4] * m[8] - m[5] * m[7]) / d,
    (m[2] * m[7] - m[1] * m[8]) / d,
    (m[1] * m[5] - m[2] * m[4]) / d,
    (m[5] * m[6] - m[3] * m[8]) / d,
    (m[0] * m[8] - m[2] * m[6]) / d,
    (m[2] * m[3] - m[0] * m[5]) / d,
    (m[3] * m[7] - m[4] * m[6]) / d,
    (m[1] * m[6] - m[0] * m[7]) / d,
    (m[0] * m[4] - m[1] * m[3]) / d];
}

function mul3(a, b) {
  return Array.from({ length: 9 }, (_, i) => {
    const r = Math.floor(i / 3), c = i % 3;
    return a[r * 3] * b[c] + a[r * 3 + 1] * b[c + 3] +
      a[r * 3 + 2] * b[c + 6];
  });
}

function mulv(m, v) {
  return [m[0] * v[0] + m[1] * v[1] + m[2] * v[2],
    m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
    m[6] * v[0] + m[7] * v[1] + m[8] * v[2]];
}

function rgbToXyz(space) {
  const [r, g, b, w] = LINEAR_SPACES[space];
  const p = [r[0], g[0], b[0], r[1], g[1], b[1],
    1 - r[0] - r[1], 1 - g[0] - g[1], 1 - b[0] - b[1]];
  const scale = mulv(invert3(p),
    [w[0] / w[1], 1, (1 - w[0] - w[1]) / w[1]]);
  return p.map((value, i) => value * scale[i % 3]);
}

export function canonicalColorSpace(token) {
  const value = String(token || '');
  return ALIASES[value] || value;
}

export function linearColorTransformMatrix(source = 'lin_ap0_scene',
  destination = 'lin_rec709_scene') {
  source = canonicalColorSpace(source);
  destination = canonicalColorSpace(destination);
  if (!LINEAR_SPACES[source] || !LINEAR_SPACES[destination]) {
    throw new Error(`unsupported color space: ${source} -> ${destination}`);
  }
  return mul3(invert3(rgbToXyz(destination)), rgbToXyz(source));
}

export function transformLinearColor(rgb, source = 'lin_ap0_scene',
  destination = 'lin_rec709_scene') {
  return mulv(linearColorTransformMatrix(source, destination), rgb);
}

// Describe the operations Three.js must perform after fetching a color texel.
// sRGB transfer decoding is delegated to Three/WebGL; other gamma curves and
// all gamut transforms are injected into the material shader.
export function textureColorTransform(token,
  destination = 'lin_rec709_scene', resolved = null) {
  const canonical = canonicalColorSpace(token);
  const resolvedMatrix = resolved?.sourceToDisplayLinear;
  if (resolved?.colorTransformValid && !resolved.colorTransformApplied &&
      Array.isArray(resolvedMatrix) &&
      resolvedMatrix.length === 9) {
    const isData = !!resolved.sourceColorIsData;
    const authoredGamma = Number(resolved.sourceGamma ?? 1);
    const authoredBias = Number(resolved.sourceLinearBias ?? 0);
    // WebGL/Three can perform the standard sRGB EOTF in the texture sampler.
    // Keep custom primaries as a shader matrix while avoiding a second EOTF.
    const hardwareSrgb = !isData && Math.abs(authoredGamma - 2.4) < 1e-6 &&
      Math.abs(authoredBias - 0.055) < 1e-6;
    const gamma = hardwareSrgb ? 1 : authoredGamma;
    const linearBias = hardwareSrgb ? 0 : authoredBias;
    const matrix = Array.from(resolvedMatrix, Number);
    const identity = matrix.every((value, index) =>
      Math.abs(value - (index % 4 === 0 ? 1 : 0)) < 1e-8);
    return {
      canonical: canonical || String(token || ''),
      colorRole: hardwareSrgb ? 'color' : 'data',
      gamma,
      linearBias,
      matrix,
      bypass: !!resolved.colorTransformBypass ||
        (identity && Math.abs(gamma - 1) < 1e-8)
    };
  }
  if (!canonical || canonical === 'auto' || canonical === 'unknown') return null;
  if (canonical === 'raw' || canonical === 'data' || canonical === 'identity') {
    return { canonical, colorRole: 'data', gamma: 1, linearBias: 0,
      matrix: [1, 0, 0, 0, 1, 0, 0, 0, 1], bypass: true };
  }
  let linear = canonical;
  let colorRole = 'data';
  let gamma = 1;
  const replacements = {
    srgb_rec709_scene: 'lin_rec709_scene',
    srgb_ap1_scene: 'lin_ap1_scene',
    srgb_p3d65_scene: 'lin_p3d65_scene',
    g22_rec709_scene: 'lin_rec709_scene',
    g18_rec709_scene: 'lin_rec709_scene',
    g22_ap1_scene: 'lin_ap1_scene',
    g22_adobergb_scene: 'lin_adobergb_scene'
  };
  if (canonical.startsWith('srgb_')) colorRole = 'color';
  if (canonical.startsWith('g22_')) gamma = 2.2;
  if (canonical.startsWith('g18_')) gamma = 1.8;
  linear = replacements[canonical] || canonical;
  if (!LINEAR_SPACES[linear]) return null;
  const matrix = linearColorTransformMatrix(linear, destination);
  const identity = matrix.every((value, index) =>
    Math.abs(value - (index % 4 === 0 ? 1 : 0)) < 1e-8);
  return { canonical, linear, colorRole, gamma, linearBias: 0, matrix,
    bypass: identity && gamma === 1 };
}

function glslFloat(value) {
  const finite = Number.isFinite(value) ? value : 0;
  if (Math.abs(finite) < 1e-12) return '0.0';
  const text = finite.toPrecision(10);
  return /[.eE]/.test(text) ? text : `${text}.0`;
}

function textureTransformExpression(value, transform) {
  let expression = value;
  if (Math.abs((transform?.gamma ?? 1) - 1) > 1e-8) {
    const gamma = Number(transform.gamma);
    const bias = Number(transform.linearBias ?? 0);
    if (bias > 0) {
      const k0 = bias / (gamma - 1);
      const phi = (bias /
        Math.exp(Math.log(gamma * bias /
          (gamma + gamma * bias - 1 - bias)) * gamma)) /
        (gamma - 1);
      const absValue = `abs(${expression})`;
      const linear = `(${absValue} / vec3(${glslFloat(phi)}))`;
      const power = `pow((${absValue} + vec3(${glslFloat(bias)})) / ` +
        `vec3(${glslFloat(1 + bias)}), vec3(${glslFloat(gamma)}))`;
      expression = `(sign(${expression}) * mix(${linear}, ${power}, ` +
        `step(vec3(${glslFloat(k0)}), ${absValue})))`;
    } else {
      expression = `(sign(${expression}) * pow(abs(${expression}), vec3(${glslFloat(gamma)})))`;
    }
  }
  const m = transform?.matrix;
  if (Array.isArray(m) && m.length === 9) {
    // GLSL mat3 constructors are column-major; the shared transform is row-major.
    const columns = [m[0], m[3], m[6], m[1], m[4], m[7], m[2], m[5], m[8]];
    expression = `(mat3(${columns.map(glslFloat).join(', ')}) * ${expression})`;
  }
  return expression;
}

// Install per-color-map source-to-display conversion without resampling the
// texture through an 8-bit canvas (which would clamp AP0/AP1 out-of-gamut
// values). Three.js handles sRGB EOTF from texture.colorSpace; this hook adds
// other gamma curves and the linear-primary matrix in the fragment shader.
export function installTextureColorTransform(material, mapProperty, transform) {
  if (!material || !transform || transform.bypass ||
      (mapProperty !== 'map' && mapProperty !== 'emissiveMap')) return;
  const transforms = material.userData.lightusdTextureColorTransforms || {};
  transforms[mapProperty] = transform;
  material.userData.lightusdTextureColorTransforms = transforms;
  if (material.userData.lightusdTextureColorHookInstalled) return;
  material.userData.lightusdTextureColorHookInstalled = true;
  const previousCompile = material.onBeforeCompile?.bind(material);
  const previousCacheKey = material.customProgramCacheKey?.bind(material);
  material.onBeforeCompile = (shader, renderer) => {
    if (previousCompile) previousCompile(shader, renderer);
    const configured = material.userData.lightusdTextureColorTransforms || {};
    if (configured.map) {
      const chunk = THREE.ShaderChunk.map_fragment.replace(
        'diffuseColor *= sampledDiffuseColor;',
        `sampledDiffuseColor.rgb = ${textureTransformExpression(
          'sampledDiffuseColor.rgb', configured.map)};\n\tdiffuseColor *= sampledDiffuseColor;`);
      shader.fragmentShader = shader.fragmentShader.replace(
        '#include <map_fragment>', chunk);
    }
    if (configured.emissiveMap) {
      const chunk = THREE.ShaderChunk.emissivemap_fragment.replace(
        'totalEmissiveRadiance *= emissiveColor.rgb;',
        `emissiveColor.rgb = ${textureTransformExpression(
          'emissiveColor.rgb', configured.emissiveMap)};\n\ttotalEmissiveRadiance *= emissiveColor.rgb;`);
      shader.fragmentShader = shader.fragmentShader.replace(
        '#include <emissivemap_fragment>', chunk);
    }
  };
  material.customProgramCacheKey = () => {
    const prior = previousCacheKey ? previousCacheKey() : '';
    return `${prior}|lightusd-cs:${JSON.stringify(
      material.userData.lightusdTextureColorTransforms)}`;
  };
}
