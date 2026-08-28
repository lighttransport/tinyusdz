import * as THREE from 'three';

const DEFAULT_HAIR = Object.freeze({
  tintR: [0.42, 0.12, 0.035], tintTT: [0.28, 0.055, 0.012],
  tintTRT: [0.12, 0.025, 0.006], roughnessR: [0.22, 0.35],
  roughnessTT: [0.32, 0.45], roughnessTRT: [0.42, 0.55],
  absorptionCoefficient: [0.35, 0.8, 1.4], ior: 1.55, cuticleAngle: 3.0
});

function hash01(value) {
  const x = Math.sin(value * 127.1 + 311.7) * 43758.5453123;
  return x - Math.floor(x);
}

function readWidth(widths, pointIndex, fallback) {
  if (!widths?.length) return fallback;
  return Number(widths[Math.min(pointIndex, widths.length - 1)]) || fallback;
}

export function buildHairRibbonGeometry(curves, options = {}) {
  const points = curves.tessellatedPoints || curves.points || [];
  const counts = curves.tessellatedVertexCounts || curves.curveVertexCounts || [];
  const widths = curves.tessellatedWidths || curves.widths || [];
  const colors = curves.tessellatedColors || curves.colors || [];
  const defaultWidth = options.width ?? 0.012;
  let pointCount = 0;
  for (const count of counts) pointCount += Math.max(0, Number(count) || 0);
  const centers = new Float32Array(pointCount * 2 * 3);
  const tangents = new Float32Array(pointCount * 2 * 3);
  const sides = new Float32Array(pointCount * 2);
  const strandU = new Float32Array(pointCount * 2);
  const strandRandom = new Float32Array(pointCount * 2);
  const ribbonWidths = new Float32Array(pointCount * 2);
  const ribbonColors = new Float32Array(pointCount * 2 * 3);
  const segmentCount = counts.reduce((sum, count) => sum + Math.max(0, count - 1), 0);
  const IndexArray = pointCount * 2 > 65535 ? Uint32Array : Uint16Array;
  const indices = new IndexArray(segmentCount * 6);
  let srcPoint = 0;
  let vertex = 0;
  let index = 0;
  counts.forEach((rawCount, strand) => {
    const count = Math.max(0, Number(rawCount) || 0);
    for (let i = 0; i < count; i++) {
      const prev = srcPoint + Math.max(0, i - 1);
      const next = srcPoint + Math.min(count - 1, i + 1);
      let tx = Number(points[next * 3]) - Number(points[prev * 3]);
      let ty = Number(points[next * 3 + 1]) - Number(points[prev * 3 + 1]);
      let tz = Number(points[next * 3 + 2]) - Number(points[prev * 3 + 2]);
      const length = Math.hypot(tx, ty, tz);
      if (length > 1e-8) { tx /= length; ty /= length; tz /= length; }
      else { tx = 0; ty = 1; tz = 0; }
      const u = count > 1 ? i / (count - 1) : 0;
      const width = readWidth(widths, srcPoint + i, defaultWidth);
      const random = hash01(strand + 1);
      for (let side = 0; side < 2; side++) {
        const v = vertex + side;
        centers.set([points[(srcPoint + i) * 3] || 0, points[(srcPoint + i) * 3 + 1] || 0,
          points[(srcPoint + i) * 3 + 2] || 0], v * 3);
        tangents.set([tx, ty, tz], v * 3);
        sides[v] = side ? 1 : -1;
        strandU[v] = u;
        strandRandom[v] = random;
        ribbonWidths[v] = width;
        const colorIndex = colors.length >= (srcPoint + i + 1) * 3 ? (srcPoint + i) * 3 : -1;
        ribbonColors.set(colorIndex >= 0
          ? [colors[colorIndex], colors[colorIndex + 1], colors[colorIndex + 2]] : [1, 1, 1], v * 3);
      }
      if (i + 1 < count) {
        const a = vertex;
        indices.set([a, a + 1, a + 2, a + 1, a + 3, a + 2], index);
        index += 6;
      }
      vertex += 2;
    }
    srcPoint += count;
  });
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.BufferAttribute(centers, 3));
  geometry.setAttribute('hairTangent', new THREE.BufferAttribute(tangents, 3));
  geometry.setAttribute('hairSide', new THREE.BufferAttribute(sides, 1));
  geometry.setAttribute('hairU', new THREE.BufferAttribute(strandU, 1));
  geometry.setAttribute('hairRandom', new THREE.BufferAttribute(strandRandom, 1));
  geometry.setAttribute('hairWidth', new THREE.BufferAttribute(ribbonWidths, 1));
  geometry.setAttribute('hairColor', new THREE.BufferAttribute(ribbonColors, 3));
  geometry.setIndex(new THREE.BufferAttribute(indices, 1));
  geometry.computeBoundingSphere();
  geometry.userData = { strandCount: counts.length, segmentCount };
  return geometry;
}

function seededRandom(seed) {
  let state = seed >>> 0;
  return () => ((state = Math.imul(state ^ state >>> 15, 1 | state),
    state ^= state + Math.imul(state ^ state >>> 7, 61 | state),
    ((state ^ state >>> 14) >>> 0) / 4294967296));
}

export function generateFurballCurves(strands = 12000, segments = 5, seed = 7) {
  const random = seededRandom(seed);
  const counts = new Uint32Array(strands).fill(segments + 1);
  const points = new Float32Array(strands * (segments + 1) * 3);
  const widths = new Float32Array(strands * (segments + 1));
  let p = 0;
  for (let strand = 0; strand < strands; strand++) {
    const y = 1 - 2 * ((strand + 0.5) / strands);
    const radius = Math.sqrt(Math.max(0, 1 - y * y));
    const phi = strand * Math.PI * (3 - Math.sqrt(5)) + (random() - 0.5) * 0.08;
    const nx = Math.cos(phi) * radius, ny = y, nz = Math.sin(phi) * radius;
    const length = 0.24 + random() * 0.16;
    const curl = (random() - 0.5) * 0.11;
    for (let i = 0; i <= segments; i++, p++) {
      const u = i / segments;
      const bend = curl * u * u;
      points.set([nx * (1 + length * u) + bend * Math.sin(phi),
        ny * (1 + length * u) + 0.035 * u * u,
        nz * (1 + length * u) - bend * Math.cos(phi)], p * 3);
      widths[p] = 0.018 * (1 - 0.9 * u);
    }
  }
  return { tessellatedPoints: points, tessellatedVertexCounts: counts, tessellatedWidths: widths };
}

export function generateGrassCurves(strands = 18000, segments = 4, seed = 11) {
  const random = seededRandom(seed);
  const counts = new Uint32Array(strands).fill(segments + 1);
  const points = new Float32Array(strands * (segments + 1) * 3);
  const widths = new Float32Array(strands * (segments + 1));
  let p = 0;
  const side = Math.sqrt(strands);
  for (let strand = 0; strand < strands; strand++) {
    const gx = strand % Math.ceil(side), gz = Math.floor(strand / Math.ceil(side));
    const x = (gx / side - 0.5) * 6 + (random() - 0.5) * 0.05;
    const z = (gz / side - 0.5) * 6 + (random() - 0.5) * 0.05;
    const height = 0.28 + random() * 0.42;
    const dx = (random() - 0.5) * 0.22, dz = (random() - 0.5) * 0.22;
    for (let i = 0; i <= segments; i++, p++) {
      const u = i / segments;
      points.set([x + dx * u * u, height * u, z + dz * u * u], p * 3);
      widths[p] = 0.022 * (1 - 0.92 * u);
    }
  }
  return { tessellatedPoints: points, tessellatedVertexCounts: counts, tessellatedWidths: widths };
}

const vertexShader = `
  attribute vec3 hairTangent; attribute float hairSide; attribute float hairU;
  attribute float hairRandom; attribute float hairWidth; attribute vec3 hairColor;
  uniform float widthScale; uniform float time; uniform float wind;
  varying vec3 vWorld; varying vec3 vTangent; varying float vSide; varying float vU;
  varying float vRandom; varying vec3 vColor;
  void main() {
    vec4 centerWorld = modelMatrix * vec4(position, 1.0);
    vec3 tangentWorld = normalize(mat3(modelMatrix) * hairTangent);
    vec3 viewDir = normalize(cameraPosition - centerWorld.xyz);
    vec3 sideDir = cross(tangentWorld, viewDir);
    if (dot(sideDir, sideDir) < 1e-6) sideDir = cross(tangentWorld, vec3(0.0, 0.0, 1.0));
    sideDir = normalize(sideDir);
    float sway = sin(time * 1.7 + centerWorld.x * 1.9 + centerWorld.z * 1.3 + hairRandom * 6.28);
    centerWorld.xz += vec2(sway, sway * 0.37) * wind * hairU * hairU;
    centerWorld.xyz += sideDir * hairSide * hairWidth * widthScale * 0.5;
    vWorld = centerWorld.xyz; vTangent = tangentWorld; vSide = hairSide;
    vU = hairU; vRandom = hairRandom; vColor = hairColor;
    gl_Position = projectionMatrix * viewMatrix * centerWorld;
  }`;

const hairFragment = `
  precision highp float;
  uniform vec3 tintR, tintTT, tintTRT, absorption, lightDirection, lightColor, ambientColor;
  uniform float roughnessR, roughnessTT, roughnessTRT, ior, cuticleAngle, opacity;
  uniform sampler2D environmentMap; uniform float environmentIntensity, environmentRotation;
  uniform bool hasEnvironment;
  uniform sampler2D opaqueDepth; uniform vec2 resolution; uniform int pass;
  varying vec3 vWorld; varying vec3 vTangent; varying float vSide; varying float vU;
  varying float vRandom; varying vec3 vColor;
  float longitudinal(vec3 t, vec3 h, float roughness, float shift) {
    float theta = acos(clamp(dot(t, h), -1.0, 1.0)) - 1.5707963 - shift;
    float beta = max(0.035, roughness * roughness);
    return exp(-0.5 * theta * theta / (beta * beta)) / (2.5066 * beta);
  }
  vec3 sampleEnvironment(vec3 direction) {
    float c = cos(environmentRotation), s = sin(environmentRotation);
    direction.xz = mat2(c, -s, s, c) * direction.xz;
    vec2 uv = vec2(atan(direction.z, direction.x) * 0.15915494 + 0.5,
      asin(clamp(direction.y, -1.0, 1.0)) * 0.31830989 + 0.5);
    return texture2D(environmentMap, uv).rgb;
  }
  void main() {
    float sceneDepth = texture2D(opaqueDepth, gl_FragCoord.xy / resolution).r;
    if (gl_FragCoord.z > sceneDepth + 0.00015) discard;
    float edge = 1.0 - smoothstep(0.60, 1.0, abs(vSide));
    float alpha = opacity * edge * smoothstep(1.0, 0.82, vU);
    if (alpha < 0.002) discard;
    vec3 T = normalize(vTangent), V = normalize(cameraPosition - vWorld);
    vec3 L = normalize(lightDirection), H = normalize(L + V);
    float sinTL = sqrt(max(0.0, 1.0 - pow(dot(T, L), 2.0)));
    float f0 = pow((ior - 1.0) / (ior + 1.0), 2.0);
    float fresnel = f0 + (1.0 - f0) * pow(1.0 - max(dot(V, H), 0.0), 5.0);
    float cuticle = radians(cuticleAngle);
    float r = longitudinal(T, H, roughnessR, -cuticle) * fresnel;
    float tt = longitudinal(T, H, roughnessTT, cuticle * 0.5) * (1.0 - fresnel);
    float trt = longitudinal(T, H, roughnessTRT, cuticle * 1.5) * fresnel * 0.55;
    vec3 transmit = exp(-absorption / max(0.18, sinTL));
    vec3 base = mix(tintTT, tintR, 0.35 + 0.25 * vRandom) * vColor;
    vec3 env = vec3(0.0);
    if (hasEnvironment) {
      // Hair has an azimuthally symmetric normal distribution. Two reflected
      // directions around the tangent give a stable, inexpensive environment
      // response without per-strand normals or prefiltered texture lookups.
      vec3 side = normalize(cross(T, V));
      if (dot(side, side) < 1e-5) side = vec3(1.0, 0.0, 0.0);
      vec3 n0 = normalize(cross(side, T));
      vec3 reflected = sampleEnvironment(reflect(-V, n0));
      vec3 tangentLight = sampleEnvironment(normalize(T + n0 * 0.45));
      float envFresnel = f0 + (1.0 - f0) * pow(1.0 - abs(dot(V, n0)), 5.0);
      env = (base * tangentLight * 0.45 + reflected * tintR * (0.25 + envFresnel))
        * environmentIntensity;
    }
    vec3 color = base * ambientColor + lightColor * (base * (0.18 + 0.82 * sinTL)
      + tintR * r + tintTT * transmit * tt + tintTRT * transmit * transmit * trt) + env;
    float depthWeight = clamp(pow(1.0 - gl_FragCoord.z, 3.0) * 80.0, 0.25, 12.0);
    if (pass == 0) gl_FragColor = vec4(color * alpha * depthWeight, alpha * depthWeight);
    else gl_FragColor = vec4(alpha, alpha, alpha, alpha);
  }`;

function makeHairMaterial(params, pass, opaqueDepth) {
  const hair = params.hair || DEFAULT_HAIR;
  const material = new THREE.ShaderMaterial({
    vertexShader, fragmentShader: hairFragment, side: THREE.DoubleSide,
    depthWrite: false, depthTest: false, transparent: true,
    blending: pass === 0 ? THREE.AdditiveBlending : THREE.CustomBlending,
    blendSrc: pass === 0 ? THREE.OneFactor : THREE.ZeroFactor,
    blendDst: pass === 0 ? THREE.OneFactor : THREE.OneMinusSrcAlphaFactor,
    uniforms: {
      widthScale: { value: params.widthScale ?? 1 }, time: { value: 0 }, wind: { value: params.wind ?? 0 },
      tintR: { value: new THREE.Color(...hair.tintR) }, tintTT: { value: new THREE.Color(...hair.tintTT) },
      tintTRT: { value: new THREE.Color(...hair.tintTRT) }, absorption: { value: new THREE.Vector3(...hair.absorptionCoefficient) },
      roughnessR: { value: hair.roughnessR[0] }, roughnessTT: { value: hair.roughnessTT[0] },
      roughnessTRT: { value: hair.roughnessTRT[0] }, ior: { value: hair.ior },
      cuticleAngle: { value: hair.cuticleAngle }, opacity: { value: params.opacity ?? 0.72 },
      environmentMap: { value: null }, hasEnvironment: { value: false },
      environmentIntensity: { value: params.environmentIntensity ?? 1 },
      environmentRotation: { value: params.environmentRotation ?? 0 },
      lightDirection: { value: new THREE.Vector3(0.5, 0.8, 0.4).normalize() },
      lightColor: { value: new THREE.Color(3.2, 2.95, 2.7) }, ambientColor: { value: new THREE.Color(0.18, 0.22, 0.28) },
      opaqueDepth: { value: opaqueDepth }, resolution: { value: new THREE.Vector2(1, 1) }, pass: { value: pass }
    }
  });
  material.userData.hairPass = pass;
  return material;
}

const hairDepthFragment = `
  #include <packing>
  uniform float opacity; varying float vSide; varying float vU;
  void main() {
    float alpha = opacity * (1.0 - smoothstep(0.60, 1.0, abs(vSide))) * smoothstep(1.0, 0.82, vU);
    if (alpha < 0.28) discard;
    gl_FragColor = packDepthToRGBA(gl_FragCoord.z);
  }`;

function makeHairDepthMaterial(params) {
  const material = new THREE.ShaderMaterial({
    vertexShader, fragmentShader: hairDepthFragment, side: THREE.DoubleSide,
    uniforms: {
      widthScale: { value: params.widthScale ?? 1 }, time: { value: 0 },
      wind: { value: params.wind ?? 0 }, opacity: { value: params.opacity ?? 0.72 }
    }
  });
  material.depthPacking = THREE.RGBADepthPacking;
  return material;
}

const compositeFragment = `
  uniform sampler2D opaqueColor, accumulation, revealage; uniform float exposure;
  varying vec2 vUv;
  vec3 aces(vec3 x) { return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), 0.0, 1.0); }
  vec3 linearToSRGB(vec3 c) { return mix(12.92*c, 1.055*pow(c, vec3(1.0/2.4))-0.055, step(vec3(0.0031308), c)); }
  void main() {
    vec3 base = texture2D(opaqueColor, vUv).rgb;
    vec4 accum = texture2D(accumulation, vUv);
    float alpha = 1.0 - texture2D(revealage, vUv).r;
    vec3 hair = accum.rgb / max(accum.a, 1e-5);
    gl_FragColor = vec4(linearToSRGB(aces(mix(base, hair, alpha) * exposure)), 1.0);
  }`;

class HairFurRenderer {
  constructor(app) {
    this.app = app; this.hairMeshes = []; this.elapsed = 0;
    this.opaque = new THREE.WebGLRenderTarget(1, 1, { type: THREE.HalfFloatType });
    this.opaque.depthTexture = new THREE.DepthTexture(1, 1, THREE.UnsignedIntType);
    this.accum = new THREE.WebGLRenderTarget(1, 1, { type: THREE.HalfFloatType });
    this.reveal = new THREE.WebGLRenderTarget(1, 1, { type: THREE.HalfFloatType });
    this.quadScene = new THREE.Scene(); this.quadCamera = new THREE.OrthographicCamera(-1, 1, 1, -1, 0, 1);
    this.composite = new THREE.ShaderMaterial({ vertexShader: 'varying vec2 vUv; void main(){vUv=uv;gl_Position=vec4(position.xy,0.,1.);}',
      fragmentShader: compositeFragment, depthTest: false, depthWrite: false, toneMapped: false,
      uniforms: { opaqueColor: { value: this.opaque.texture }, accumulation: { value: this.accum.texture },
        revealage: { value: this.reveal.texture }, exposure: { value: 1.15 } } });
    this.quadScene.add(new THREE.Mesh(new THREE.PlaneGeometry(2, 2), this.composite));
  }
  add(mesh, hair, options = {}) {
    const accum = makeHairMaterial({ ...options, hair }, 0, this.opaque.depthTexture);
    const reveal = makeHairMaterial({ ...options, hair }, 1, this.opaque.depthTexture);
    const depth = makeHairDepthMaterial(options);
    const shadowOnly = new THREE.MeshBasicMaterial({ colorWrite: false, depthWrite: false });
    mesh.material = accum; mesh.userData.hairAccum = accum; mesh.userData.hairReveal = reveal;
    mesh.userData.hairDepth = depth; mesh.userData.hairShadowOnly = shadowOnly;
    mesh.customDepthMaterial = depth; mesh.castShadow = true;
    mesh.frustumCulled = false; this.hairMeshes.push(mesh); return mesh;
  }
  clear() { this.hairMeshes.length = 0; }
  resize() {
    const size = this.app.renderer.getDrawingBufferSize(new THREE.Vector2());
    if (this.opaque.width === size.x && this.opaque.height === size.y) return;
    for (const target of [this.opaque, this.accum, this.reveal]) target.setSize(size.x, size.y);
    for (const mesh of this.hairMeshes) for (const material of [mesh.userData.hairAccum, mesh.userData.hairReveal]) {
      material.uniforms.resolution.value.copy(size);
    }
  }
  render() {
    this.resize(); this.elapsed += 1 / 60;
    const { renderer, scene, camera } = this.app;
    const environment = this.app.environmentSource;
    for (const mesh of this.hairMeshes) for (const material of [mesh.userData.hairAccum, mesh.userData.hairReveal]) {
      material.uniforms.environmentMap.value = environment;
      material.uniforms.hasEnvironment.value = Boolean(environment);
    }
    const oldBackground = scene.background;
    // The colorless material keeps ribbons out of the opaque target while
    // their custom depth material still contributes to the directional shadow
    // map rendered by Three.js.
    for (const mesh of this.hairMeshes) mesh.material = mesh.userData.hairShadowOnly;
    renderer.setRenderTarget(this.opaque); renderer.setClearColor(0x111317, 1); renderer.clear(); renderer.render(scene, camera);
    for (const mesh of this.hairMeshes) mesh.material = mesh.userData.hairAccum;
    const nonHair = [];
    scene.traverse((object) => { if (object.visible && !this.hairMeshes.includes(object) && (object.isMesh || object.isLine || object.isPoints)) { nonHair.push(object); object.visible = false; } });
    scene.background = null;
    renderer.setRenderTarget(this.accum); renderer.setClearColor(0x000000, 0); renderer.clear(true, false, false);
    for (const mesh of this.hairMeshes) {
      mesh.material = mesh.userData.hairAccum; mesh.material.uniforms.time.value = this.elapsed;
      mesh.userData.hairDepth.uniforms.time.value = this.elapsed;
    }
    renderer.render(scene, camera);
    renderer.setRenderTarget(this.reveal); renderer.setClearColor(0xffffff, 1); renderer.clear(true, false, false);
    for (const mesh of this.hairMeshes) mesh.material = mesh.userData.hairReveal;
    renderer.render(scene, camera);
    for (const object of nonHair) object.visible = true;
    for (const mesh of this.hairMeshes) mesh.material = mesh.userData.hairAccum;
    scene.background = oldBackground;
    renderer.setRenderTarget(null); renderer.setClearColor(0x000000, 1); renderer.clear(); renderer.render(this.quadScene, this.quadCamera);
  }
}

function materialHair(usd, materialId, curves) {
  const material = materialId >= 0 ? (usd.getMaterial?.(materialId) || usd.materials?.[materialId]) : null;
  if (material?.hair) return { ...DEFAULT_HAIR, ...material.hair };
  const curveName = curves?.name || curves?.primPath || '';
  // Preserve the authored look when an older adapter exposes the curves but
  // not the nested MaterialX hair terminal. Current adapters take the branch
  // above and use the exact ND_chiang_hair_bsdf parameters.
  if (/wstraight/i.test(curveName)) return {
    ...DEFAULT_HAIR, tintR: [0.07, 0.025, 0.012], tintTT: [0.16, 0.055, 0.02],
    tintTRT: [0.24, 0.09, 0.035], absorptionCoefficient: [1.1, 2.2, 3.4]
  };
  if (/wwavy/i.test(curveName)) return {
    ...DEFAULT_HAIR, tintR: [0.22, 0.055, 0.018], tintTT: [0.48, 0.13, 0.035],
    tintTRT: [0.68, 0.24, 0.06], absorptionCoefficient: [0.6, 1.45, 2.6]
  };
  if (/grass/i.test(curveName)) return {
    ...DEFAULT_HAIR, tintR: [0.12, 0.34, 0.035], tintTT: [0.08, 0.22, 0.025],
    tintTRT: [0.18, 0.3, 0.04], absorptionCoefficient: [0.7, 0.15, 1.1]
  };
  return DEFAULT_HAIR;
}

function lightweightCurveData(curves) {
  const controlCount = curves.points?.length || 0;
  const tessellatedCount = curves.tessellatedPoints?.length || 0;
  if (!controlCount || tessellatedCount <= controlCount * 2) return curves;
  // The next adapter's cubic preview tessellation is intentionally smooth,
  // but high-density authored hair already has ample control points. Keeping
  // those points avoids multiplying a 10k-strand asset into millions of
  // ribbon segments on integrated GPUs.
  return {
    ...curves,
    tessellatedPoints: curves.points,
    tessellatedVertexCounts: curves.curveVertexCounts,
    tessellatedWidths: curves.widths,
    tessellatedColors: curves.colors
  };
}

export async function installHairFurRendering(app) {
  if (!app.renderer.capabilities.isWebGL2) throw new Error('The hair demo requires WebGL 2.');
  const renderer = new HairFurRenderer(app);
  app.keyLight.castShadow = true;
  app.renderer.shadowMap.type = THREE.PCFShadowMap;
  app.keyLight.shadow.mapSize.set(2048, 2048);
  Object.assign(app.keyLight.shadow.camera, { left: -6, right: 6, top: 6, bottom: -6, near: 0.1, far: 30 });
  app.keyLight.shadow.bias = -0.00025;
  app.keyLight.shadow.normalBias = 0.025;
  const procedural = new THREE.Group(); procedural.name = 'Procedural hair and fur'; app.world.add(procedural);
  function keepProceduralYUp() {
    // Runtime generators author Three.js/Y-up coordinates. app.world may be
    // rotated for a Z-up USD layer, so cancel only that parent rotation.
    procedural.quaternion.copy(app.world.quaternion).invert();
    procedural.updateMatrixWorld(true);
  }
  keepProceduralYUp();
  const params = { source: 'USD curves', density: 12000, width: 1, opacity: 0.72, wind: 0.12,
    environmentIntensity: 1.2, environmentRotation: 0, exposure: 1.15,
    seed: 7, regenerate: () => rebuildProcedural() };
  const folder = app.gui.addFolder('Hair / Fur');
  folder.add(params, 'source', ['USD curves', 'Furball', 'Grass field']).onChange(() => {
    if (params.source === 'USD curves') updateSource(); else rebuildProcedural();
  });
  folder.add(params, 'density', 1000, 30000, 1000).onFinishChange(() => rebuildProcedural());
  folder.add(params, 'width', 0.2, 3, 0.01).onChange(updateUniforms);
  folder.add(params, 'opacity', 0.1, 1, 0.01).onChange(updateUniforms);
  folder.add(params, 'wind', 0, 0.5, 0.005).onChange(updateUniforms);
  folder.add(params, 'environmentIntensity', 0, 4, 0.01).name('Environment light').onChange(updateUniforms);
  folder.add(params, 'environmentRotation', -180, 180, 1).name('Environment rotation').onChange(updateUniforms);
  folder.add(params, 'exposure', 0.2, 3, 0.01).onChange(v => { renderer.composite.uniforms.exposure.value = v; });
  folder.add(params, 'seed', 1, 999, 1).onFinishChange(() => rebuildProcedural());
  folder.add(params, 'regenerate').name('Regenerate'); folder.open();
  let usdMeshes = [], usdRoot = null, proceduralMesh = null;
  function updateUniforms() {
    for (const mesh of renderer.hairMeshes) for (const material of [mesh.userData.hairAccum, mesh.userData.hairReveal]) {
      material.uniforms.widthScale.value = params.width; material.uniforms.opacity.value = params.opacity;
      material.uniforms.wind.value = params.source === 'Grass field' ? params.wind : params.wind * 0.12;
      material.uniforms.environmentIntensity.value = params.environmentIntensity;
      material.uniforms.environmentRotation.value = THREE.MathUtils.degToRad(params.environmentRotation);
      const depth = mesh.userData.hairDepth;
      depth.uniforms.widthScale.value = params.width; depth.uniforms.opacity.value = params.opacity;
      depth.uniforms.wind.value = params.source === 'Grass field' ? params.wind : params.wind * 0.12;
    }
  }
  function addSupport(type) {
    const material = new THREE.MeshStandardMaterial({ color: type === 'Furball' ? 0x32160d : 0x172d12, roughness: 0.82 });
    const mesh = type === 'Furball' ? new THREE.Mesh(new THREE.SphereGeometry(1, 64, 32), material)
      : new THREE.Mesh(new THREE.PlaneGeometry(6.2, 6.2), material);
    if (type !== 'Furball') mesh.rotation.x = -Math.PI / 2;
    mesh.castShadow = true; mesh.receiveShadow = true; procedural.add(mesh);
    if (type === 'Furball') {
      const floor = new THREE.Mesh(
        new THREE.CircleGeometry(3.2, 64),
        new THREE.MeshStandardMaterial({ color: 0x25272b, roughness: 0.9, metalness: 0 })
      );
      floor.name = 'Furball shadow receiver';
      floor.rotation.x = -Math.PI / 2; floor.position.y = -1.03;
      floor.receiveShadow = true; procedural.add(floor);
    }
  }
  function updateProceduralShadow() {
    const radius = params.source === 'Grass field' ? 4.6 : 3.4;
    Object.assign(app.keyLight.shadow.camera, {
      left: -radius, right: radius, top: radius, bottom: -radius,
      near: 0.1, far: 30
    });
    app.keyLight.shadow.camera.updateProjectionMatrix();
    app.keyLight.shadow.needsUpdate = true;
  }
  function rebuildProcedural() {
    while (procedural.children.length) {
      const child = procedural.children.pop(); child.geometry?.dispose(); child.material?.dispose();
    }
    proceduralMesh = null;
    if (params.source === 'USD curves') return updateSource();
    addSupport(params.source);
    const curves = params.source === 'Furball' ? generateFurballCurves(params.density, 5, params.seed)
      : generateGrassCurves(params.density, 4, params.seed);
    proceduralMesh = new THREE.Mesh(buildHairRibbonGeometry(curves));
    procedural.add(proceduralMesh); renderer.add(proceduralMesh, params.source === 'Furball' ? DEFAULT_HAIR : {
      ...DEFAULT_HAIR, tintR: [0.12, 0.34, 0.035], tintTT: [0.08, 0.22, 0.025], tintTRT: [0.18, 0.3, 0.04],
      absorptionCoefficient: [0.7, 0.15, 1.1]
    }, params); updateSource(); updateProceduralShadow(); app.fitScene();
  }
  function updateSource() {
    procedural.visible = params.source !== 'USD curves';
    if (usdRoot) usdRoot.visible = params.source === 'USD curves';
    for (const mesh of usdMeshes) mesh.visible = params.source === 'USD curves';
    // Keep the renderer's active list independent. renderer.add() appends to
    // that list while rebuilding procedural geometry; aliasing usdMeshes here
    // would pollute the USD selection after every source change.
    renderer.hairMeshes = params.source === 'USD curves' ? [...usdMeshes] : (proceduralMesh ? [proceduralMesh] : []);
    updateUniforms();
  }
  function installUsd({ usd, root }) {
    // The initial scene is already loaded before this extension is installed,
    // so that path supplies app.world rather than the sceneRoot callback value.
    // Resolve the actual USD child; hiding app.world would also hide the
    // procedural alternatives and make the source selector appear broken.
    if (root === app.world) {
      root = app.world.children.find((candidate) => {
        let containsCurves = false;
        candidate.traverse((object) => { containsCurves ||= Boolean(object.userData?.usdCurves); });
        return containsCurves;
      }) || root;
    }
    renderer.clear(); usdMeshes = []; usdRoot = root;
    // displayUSD() clears app.world before installing a newly loaded scene.
    // Keep the runtime-generated alternatives attached across USD reloads.
    if (procedural.parent !== app.world) app.world.add(procedural);
    keepProceduralYUp();
    // Non-UsdLux demos restore the lighting environment during load, but the
    // scene clear intentionally leaves a solid background behind. Hair uses
    // the default HDRI for both lighting and presentation.
    app.scene.environment = app.envMap;
    app.scene.background = app.environmentSource || new THREE.Color(0x0e0e10);
    root.traverse((object) => {
      if (object.isMesh) object.receiveShadow = true;
      const meta = object.userData?.usdCurves;
      if (!meta) return;
      const curves = usd.getCurves?.(meta.index) || usd.curves?.[meta.index];
      if (!curves) return;
      for (const child of object.children) child.visible = false;
      const mesh = new THREE.Mesh(buildHairRibbonGeometry(lightweightCurveData(curves)));
      mesh.name = `${curves.name || 'BasisCurves'} hair ribbons`;
      object.add(mesh); renderer.add(mesh, materialHair(usd, curves.materialId, curves), params); usdMeshes.push(mesh);
    });
    params.source = 'USD curves'; folder.controllers[0]?.updateDisplay(); updateSource();
    usd.releaseBuildData?.();
  }
  app.onSceneChanged(installUsd);
  if (app.currentUsd) installUsd({ usd: app.currentUsd, root: app.world });
  app.renderOverride = () => renderer.render();
  app.setNotes([
    'WebGL2 camera-facing ribbons with a lightweight Chiang/Principled Hair approximation.',
    'Weighted blended transparency avoids per-strand sorting and is suitable for integrated GPUs.',
    'Alpha-tested ribbon depth shadows use the default directional key light.',
    'The sample is an offline-generated USDC BasisCurves scene; Furball and Grass field are generated at runtime.'
  ]);
  window.__hairFurRendering = { renderer, params, generateFurballCurves, generateGrassCurves };
}
