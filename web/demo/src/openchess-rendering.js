import * as THREE from 'three';
import { EffectComposer } from 'three/examples/jsm/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/examples/jsm/postprocessing/RenderPass.js';
import { BokehPass } from 'three/examples/jsm/postprocessing/BokehPass.js';
import { OutputPass } from 'three/examples/jsm/postprocessing/OutputPass.js';
import { OpenChessWebGPUPathTracer } from './openchess-webgpu-path.js';

const MODES = {
  webgl: 'WebGL2 raster',
  cpu: 'LightRT CPU path trace',
  webgpu: 'WebGPU raster',
  webgpuPath: 'WebGPU path trace'
};

// Three.js r183 has the legacy ACES Filmic curve but no ACES 2.0 rendering
// transform. CustomToneMapping gives this demo a compact, GPU-friendly ACES
// 2.0-style tonescale and highlight chroma compression. It is intentionally an
// approximation, not a replacement for an OCIO ACES 2 config.
const ACES2_APPROX_GLSL = `
vec3 CustomToneMapping( vec3 color ) {
  color = max( color * toneMappingExposure, vec3( 0.0 ) );
  float peak = max( max( color.r, color.g ), color.b );
  float chromaCompression = 1.0 / ( 1.0 + 0.18 * peak * peak );
  float luminance = dot( color, vec3( 0.2126, 0.7152, 0.0722 ) );
  color = mix( vec3( luminance ), color, chromaCompression );
  color = ( color * ( 2.51 * color + 0.03 ) ) /
          ( color * ( 2.43 * color + 0.59 ) + 0.14 );
  return clamp( color, 0.0, 1.0 );
}`;

THREE.ShaderChunk.tonemapping_pars_fragment = THREE.ShaderChunk.tonemapping_pars_fragment
  .replace('vec3 CustomToneMapping( vec3 color ) { return color; }', ACES2_APPROX_GLSL);

const TONE_MAPS = {
  'ACES 2.0 (approx)': THREE.CustomToneMapping,
  'ACES Filmic (legacy)': THREE.ACESFilmicToneMapping,
  AgX: THREE.AgXToneMapping,
  Neutral: THREE.NeutralToneMapping,
  Reinhard: THREE.ReinhardToneMapping,
  Linear: THREE.LinearToneMapping,
  None: THREE.NoToneMapping
};

function materialData(material) {
  const raw = material?.userData?.openPBRData || material?.userData?.rawData || {};
  return raw.openPBR || raw.openPBRShader || raw;
}

function scalar(value, fallback = 0) {
  if (Number.isFinite(value)) return value;
  if (Number.isFinite(value?.value)) return value.value;
  return fallback;
}

function color3(value, fallback = [1, 1, 1]) {
  const v = value?.value ?? value;
  if (Array.isArray(v) || ArrayBuffer.isView(v)) return [v[0] ?? fallback[0], v[1] ?? fallback[1], v[2] ?? fallback[2]];
  return fallback;
}

function applyApproximateSSS(root) {
  let count = 0;
  root.traverse((object) => {
    const materials = object.material ? (Array.isArray(object.material) ? object.material : [object.material]) : [];
    for (const material of materials) {
      if (!material?.isMeshStandardMaterial || material.userData?.openChessSSSInstalled) continue;
      const data = materialData(material);
      const weight = scalar(data.subsurface_weight ?? data.subsurface, 0);
      if (weight <= 0) continue;
      const tint = color3(data.subsurface_color, [material.color.r, material.color.g, material.color.b]);
      const radius = color3(data.subsurface_radius, [1, 1, 1]);
      const scale = scalar(data.subsurface_radius_scale ?? data.subsurface_scale, 1);
      const uniforms = {
        openChessSSSWeight: { value: THREE.MathUtils.clamp(weight, 0, 1) },
        openChessSSSColor: { value: new THREE.Color(...tint) },
        openChessSSSRadius: { value: Math.max(0, (radius[0] + radius[1] + radius[2]) * scale / 3) }
      };
      const previous = material.onBeforeCompile;
      material.onBeforeCompile = (shader, renderer) => {
        previous?.(shader, renderer);
        Object.assign(shader.uniforms, uniforms);
        shader.fragmentShader = shader.fragmentShader
          .replace('#include <common>', `#include <common>\nuniform float openChessSSSWeight;\nuniform vec3 openChessSSSColor;\nuniform float openChessSSSRadius;`)
          .replace('#include <lights_fragment_begin>', `#include <lights_fragment_begin>\nfloat openChessWrap = clamp((dot(normal, normalize(vViewPosition)) + 0.35) / 1.35, 0.0, 1.0);\nfloat openChessBack = pow(1.0 - openChessWrap, 2.0) * openChessSSSWeight * clamp(sqrt(openChessSSSRadius) * 0.25, 0.0, 1.0);\nreflectedLight.indirectDiffuse += openChessSSSColor * openChessBack * RECIPROCAL_PI;`);
      };
      material.customProgramCacheKey = () => `openchess-sss-${weight}-${scale}`;
      material.userData.openChessSSSInstalled = true;
      material.userData.openChessSSSApproximate = true;
      material.needsUpdate = true;
      count++;
    }
  });
  return count;
}

function openChessMeshIdentity(object) {
  const path = object.userData?.usdPointInstance?.meshPath ||
    object.userData?.usdMesh?.primPath || object.userData?.['primMeta.absPath'] || object.name || '';
  const side = path.includes('/Black/') ? 'black' : (path.includes('/White/') ? 'white' : '');
  const match = path.match(/\/(King|Queen|Bishop[^/]*|Knight[^/]*|Rook[^/]*|Pawn)(?:\/|:)/i);
  const piece = match?.[1]?.replace(/[LR]$/i, '').toLowerCase() || '';
  return { path, side, piece, pawnTop: piece === 'pawn' && /Geom_Top/i.test(path) };
}

async function installOpenChessPieceMaterials(root) {
  const response = await fetch('./assets/openchess/asset-index.json');
  if (!response.ok) throw new Error(`OpenChess asset index returned ${response.status}`);
  const index = await response.json();
  const paths = new Set(index.textures || []);
  const loader = new THREE.TextureLoader();
  const textureCache = new Map();
  const loadTexture = async (relative, color = false) => {
    if (!relative || !paths.has(relative)) return null;
    if (!textureCache.has(relative)) {
      textureCache.set(relative, loader.loadAsync(`./assets/openchess/${relative}`).then((texture) => {
        texture.colorSpace = color ? THREE.SRGBColorSpace : THREE.NoColorSpace;
        texture.wrapS = texture.wrapT = THREE.RepeatWrapping;
        texture.flipY = true;
        return texture;
      }));
    }
    return textureCache.get(relative);
  };
  const materialCache = new Map();
  const makeMaterial = async ({ side, piece, pawnTop }) => {
    const key = `${side}:${piece}:${pawnTop ? 'top' : 'body'}`;
    if (materialCache.has(key)) return materialCache.get(key);
    const promise = (async () => {
      const directory = `assets/${piece[0].toUpperCase()}${piece.slice(1)}/tex`;
      if (pawnTop) {
        const material = new THREE.MeshPhysicalMaterial({
          color: side === 'black' ? 0x4c8073 : 0xfffbd3,
          roughness: 0.12,
          metalness: 0,
          transmission: 0.92,
          thickness: 0.018,
          ior: 1.48,
          envMapIntensity: 1.25
        });
        material.name = `OpenChess ${side} pawn glass`;
        material.userData.openChessMaterialXFallback = true;
        return material;
      }
      const prefix = `${directory}/${piece}`;
      const choose = (...names) => names.find((name) => paths.has(name)) || '';
      const basePath = choose(`${prefix}_${side}_base_color.jpg`);
      const normalPath = choose(`${prefix}_${side}_normal.jpg`, `${prefix}_shared_normal.jpg`);
      const roughnessPath = choose(`${prefix}_${side}_roughness.jpg`, `${prefix}_shared_roughness.jpg`);
      const metallicPath = choose(`${prefix}_shared_metallic.jpg`);
      const material = new THREE.MeshPhysicalMaterial({ color: 0xffffff, roughness: 0.38, metalness: 0.15 });
      [material.map, material.normalMap, material.roughnessMap, material.metalnessMap] = await Promise.all([
        loadTexture(basePath, true), loadTexture(normalPath), loadTexture(roughnessPath), loadTexture(metallicPath)
      ]);
      if (!material.metalnessMap) material.metalness = 0;
      material.name = `OpenChess ${side} ${piece}`;
      material.userData.openChessMaterialXFallback = true;
      if (piece === 'king' || piece === 'queen') {
        material.userData.openPBRData = { subsurface_weight: 0.22, subsurface_color: side === 'black' ? [0.12, 0.07, 0.04] : [0.85, 0.72, 0.52], subsurface_radius: [1, 0.65, 0.35], subsurface_scale: 0.003 };
      }
      material.needsUpdate = true;
      return material;
    })();
    materialCache.set(key, promise);
    return promise;
  };

  const jobs = [];
  root.traverse((object) => {
    if (!object.isMesh) return;
    const identity = openChessMeshIdentity(object);
    if (!identity.side || !identity.piece) return;
    jobs.push(makeMaterial(identity).then((material) => { object.material = material; }));
  });
  await Promise.all(jobs);
  return jobs.length;
}

function texturePixels(texture, cache) {
  if (!texture?.image) return null;
  if (cache.has(texture)) return cache.get(texture);
  const image = texture.image;
  const sourceWidth = image.width || image.videoWidth || 0;
  const sourceHeight = image.height || image.videoHeight || 0;
  if (!sourceWidth || !sourceHeight) return null;
  // The source set contains several 4K maps. Keeping every decoded RGBA map
  // beside the expanded triangle buffers can exceed a browser tab's memory
  // budget. The transport buffers only need a vertex-baked lookup, so cap the
  // temporary sampling copy while leaving raster textures untouched.
  const scale = Math.min(1, 512 / Math.max(sourceWidth, sourceHeight));
  const width = Math.max(1, Math.round(sourceWidth * scale));
  const height = Math.max(1, Math.round(sourceHeight * scale));
  try {
    const canvas = new OffscreenCanvas(width, height);
    const ctx = canvas.getContext('2d', { willReadFrequently: true });
    ctx.drawImage(image, 0, 0, width, height);
    const record = { width, height, data: ctx.getImageData(0, 0, width, height).data };
    cache.set(texture, record);
    return record;
  } catch {
    cache.set(texture, null);
    return null;
  }
}

function sampleTexture(texture, uv, cache, channel = 0) {
  const record = texturePixels(texture, cache);
  if (!record) return channel < 0 ? [1, 1, 1] : 1;
  const u = ((uv.x % 1) + 1) % 1;
  const v = ((uv.y % 1) + 1) % 1;
  const x = Math.min(record.width - 1, Math.floor(u * record.width));
  const y = Math.min(record.height - 1, Math.floor((1 - v) * record.height));
  const p = (y * record.width + x) * 4;
  if (channel >= 0) return record.data[p + channel] / 255;
  const rgb = [record.data[p] / 255, record.data[p + 1] / 255, record.data[p + 2] / 255];
  const srgbToLinear = (x) => x <= 0.04045 ? x / 12.92 : Math.pow((x + 0.055) / 1.055, 2.4);
  return texture.colorSpace === THREE.SRGBColorSpace ? rgb.map(srgbToLinear) : rgb;
}

function sampleTextureRGB(texture, uv, cache, out) {
  const record = texturePixels(texture, cache);
  if (!record) {
    out[0] = 1; out[1] = 1; out[2] = 1;
    return out;
  }
  const u = ((uv.x % 1) + 1) % 1;
  const v = ((uv.y % 1) + 1) % 1;
  const x = Math.min(record.width - 1, Math.floor(u * record.width));
  const y = Math.min(record.height - 1, Math.floor((1 - v) * record.height));
  const offset = (y * record.width + x) * 4;
  const linear = (value) => value <= 0.04045 ? value / 12.92 : Math.pow((value + 0.055) / 1.055, 2.4);
  out[0] = record.data[offset] / 255;
  out[1] = record.data[offset + 1] / 255;
  out[2] = record.data[offset + 2] / 255;
  if (texture.colorSpace === THREE.SRGBColorSpace) {
    out[0] = linear(out[0]); out[1] = linear(out[1]); out[2] = linear(out[2]);
  }
  return out;
}

function collectMeshInstances(root) {
  const records = [];
  root.updateMatrixWorld(true);
  root.traverse((object) => {
    if (!object.isMesh || !object.geometry?.attributes?.position) return;
    if (object.isInstancedMesh) {
      const local = new THREE.Matrix4();
      for (let i = 0; i < object.count; i++) {
        object.getMatrixAt(i, local);
        records.push({ object, matrix: object.matrixWorld.clone().multiply(local) });
      }
    } else records.push({ object, matrix: object.matrixWorld.clone() });
  });
  return records;
}

function packTraceScene(root) {
  const records = collectMeshInstances(root);
  let sourceTriangles = 0;
  for (const { object } of records) {
    const geometry = object.geometry;
    sourceTriangles += Math.floor((geometry.index?.count || geometry.attributes.position.count) / 3);
  }
  // Never sample a triangle soup by dropping individual triangles: doing so
  // creates visible holes and invalidates both closest-hit and shadow rays.
  // The tracing backends are reference modes and therefore retain all source
  // geometry; interactive mode reduces resolution, samples, and bounces only.
  const triangleStride = 1;
  const triangles = sourceTriangles;
  const positions = new Float32Array(triangles * 9);
  const normals = new Float32Array(triangles * 9);
  const colors = new Float32Array(triangles * 9);
  const vertexParams = new Float32Array(triangles * 12);
  const materialIds = new Int32Array(triangles);
  const materials = [];
  const materialMap = new Map();
  const textureCache = new Map();
  const p = new THREE.Vector3(), n = new THREE.Vector3(), uv = new THREE.Vector2();
  const base = new Float32Array(3);
  let triOut = 0;
  const materialId = (material) => {
    if (materialMap.has(material)) return materialMap.get(material);
    const data = materialData(material);
    const id = materials.length / 10;
    materialMap.set(material, id);
    materials.push(material.color?.r ?? 0.7, material.color?.g ?? 0.7, material.color?.b ?? 0.7,
      material.metalness ?? scalar(data.metalness, 0), material.roughness ?? scalar(data.specular_roughness, 0.5),
      material.emissive?.r ?? 0, material.emissive?.g ?? 0, material.emissive?.b ?? 0,
      material.transmission ?? scalar(data.transmission_weight ?? data.transmission, 0),
      scalar(data.subsurface_weight ?? data.subsurface, 0));
    return id;
  };
  for (const { object, matrix } of records) {
    const geometry = object.geometry;
    const pos = geometry.attributes.position;
    const nor = geometry.attributes.normal;
    const tex = geometry.attributes.uv;
    const index = geometry.index;
    const normalMatrix = new THREE.Matrix3().getNormalMatrix(matrix);
    const mats = Array.isArray(object.material) ? object.material : [object.material];
    const count = index?.count || pos.count;
    for (let corner = 0; corner + 2 < count; corner += 3 * triangleStride, triOut++) {
      const group = geometry.groups.find((g) => corner >= g.start && corner < g.start + g.count);
      const material = mats[group?.materialIndex || 0] || mats[0];
      const mid = materialId(material);
      materialIds[triOut] = mid;
      for (let k = 0; k < 3; k++) {
        const source = index ? index.getX(corner + k) : corner + k;
        p.fromBufferAttribute(pos, source).applyMatrix4(matrix);
        const v3 = triOut * 9 + k * 3;
        positions[v3] = p.x; positions[v3 + 1] = p.y; positions[v3 + 2] = p.z;
        if (nor) n.fromBufferAttribute(nor, source).applyNormalMatrix(normalMatrix).normalize(); else n.set(0, 1, 0);
        normals[v3] = n.x; normals[v3 + 1] = n.y; normals[v3 + 2] = n.z;
        if (tex) uv.fromBufferAttribute(tex, source); else uv.set(0, 0);
        if (material.map) sampleTextureRGB(material.map, uv, textureCache, base);
        else { base[0] = 1; base[1] = 1; base[2] = 1; }
        colors[v3] = base[0]; colors[v3 + 1] = base[1]; colors[v3 + 2] = base[2];
        const data = materialData(material);
        const metal = (material.metalness ?? scalar(data.metalness, 0)) * (material.metalnessMap ? sampleTexture(material.metalnessMap, uv, textureCache, 2) : 1);
        const rough = (material.roughness ?? scalar(data.specular_roughness, 0.5)) * (material.roughnessMap ? sampleTexture(material.roughnessMap, uv, textureCache, 1) : 1);
        const v4 = triOut * 12 + k * 4;
        vertexParams[v4] = metal;
        vertexParams[v4 + 1] = rough;
        vertexParams[v4 + 2] = material.transmission ?? scalar(data.transmission_weight ?? data.transmission, 0);
        vertexParams[v4 + 3] = scalar(data.subsurface_weight ?? data.subsurface, 0);
      }
    }
  }
  return { positions, normals, colors, vertexParams, materialIds, materials: new Float32Array(materials), sourceTriangles };
}

class OpenChessRendering {
  constructor(app) {
    this.app = app;
    this.mode = 'webgl';
    this.webglRenderer = app.renderer;
    this.webgpuRenderer = null;
    this.traceCanvas = document.createElement('canvas');
    this.traceCanvas.className = 'openchess-trace-canvas';
    Object.assign(this.traceCanvas.style, { position: 'absolute', inset: '0', width: '100%', height: '100%', display: 'none' });
    app.viewport.appendChild(this.traceCanvas);
    this.params = {
      renderer: 'webgl', quality: 'Interactive', resolutionScale: 0.5, samples: 16, bounces: 3,
      dof: true, autoFocus: true, clickToFocus: true, focus: 0.58, focalLength: 50, fStop: 4,
      maxBlur: 0.015, dofResolution: 0.75,
      environment: true, environmentBackground: true, environmentIntensity: 1.4,
      backgroundIntensity: 0.7, backgroundBlur: 0.18, environmentRotation: 0,
      exposureEV: 0, toneMap: 'ACES 2.0 (approx)', displayTransform: 'sRGB',
      focusTarget: () => this.focusOnControlsTarget()
    };
    this.worker = new Worker(new URL('./openchess-lightrt.worker.js', import.meta.url), { type: 'module' });
    this.worker.onmessage = (event) => this.onWorkerMessage(event.data);
  }

  async install() {
    this.webgpuAvailable = !!navigator.gpu;
    this.cpuAvailable = typeof this.app.loader?.native_?.LightRTPathTracer === 'function';
    const folder = this.app.gui.addFolder('OpenChess renderer');
    const choices = { 'WebGL2 raster': 'webgl' };
    choices['LightRT CPU path trace'] = 'cpu';
    if (this.webgpuAvailable) {
      choices['WebGPU raster'] = 'webgpu';
      choices['WebGPU path trace'] = 'webgpuPath';
    }
    folder.add(this.params, 'renderer', choices).name('Renderer').onChange((mode) => this.select(mode));
    folder.add(this.params, 'quality', ['Interactive', 'Reference']).onChange((quality) => {
      const reference = quality === 'Reference';
      this.params.resolutionScale = reference ? 1 : 0.5;
      this.params.samples = reference ? 128 : 16;
      this.params.bounces = reference ? 6 : 3;
      folder.controllersRecursive().forEach((controller) => controller.updateDisplay());
      this.resetTracing();
    });
    folder.add(this.params, 'resolutionScale', 0.25, 1, 0.25).name('Resolution').onChange(() => this.resetTracing());
    folder.add(this.params, 'samples', 1, 512, 1).name('Target samples').onChange(() => this.resetTracing());
    folder.add(this.params, 'bounces', 1, 8, 1).name('Bounces').onChange(() => this.resetTracing());
    const dof = folder.addFolder('Raster depth of field');
    dof.add(this.params, 'dof').name('Enabled').onChange(() => this.updateDOF());
    dof.add(this.params, 'autoFocus').name('Track orbit target').onChange(() => this.updateDOF());
    dof.add(this.params, 'clickToFocus').name('Click primitive to focus');
    dof.add(this.params, 'focus', 0.05, 3, 0.01).name('Focus distance').onChange(() => this.updateDOF());
    dof.add(this.params, 'focalLength', 18, 135, 1).name('Focal length (mm)').onChange(() => this.updateDOF());
    dof.add(this.params, 'fStop', 1.2, 22, 0.1).name('F-stop').onChange(() => this.updateDOF());
    dof.add(this.params, 'maxBlur', 0, 0.025, 0.001).name('Max blur').onChange(() => this.updateDOF());
    dof.add(this.params, 'dofResolution', 0.5, 1, 0.25).name('Resolution').onChange(() => this.resizeDOF());
    dof.add(this.params, 'focusTarget').name('Focus orbit target');

    const display = folder.addFolder('Environment & display');
    display.add(this.params, 'environment').name('Environment light').onChange(() => this.updateDisplay());
    display.add(this.params, 'environmentBackground').name('Show Goegap HDRI').onChange(() => this.updateDisplay());
    display.add(this.params, 'environmentIntensity', 0, 5, 0.01).name('Light intensity').onChange(() => this.updateDisplay());
    display.add(this.params, 'backgroundIntensity', 0, 2, 0.01).name('Background intensity').onChange(() => this.updateDisplay());
    display.add(this.params, 'backgroundBlur', 0, 1, 0.01).name('Background blur').onChange(() => this.updateDisplay());
    display.add(this.params, 'environmentRotation', -180, 180, 1).name('Rotation').onChange(() => this.updateDisplay());
    display.add(this.params, 'exposureEV', -5, 5, 0.1).name('Exposure (EV)').onChange(() => this.updateDisplay());
    display.add(this.params, 'toneMap', Object.keys(TONE_MAPS)).name('Tone mapping').onChange(() => this.updateDisplay());
    display.add(this.params, 'displayTransform', ['sRGB', 'Linear sRGB']).name('Display transform').onChange(() => this.updateDisplay());
    this.initDOF();
    this.updateDisplay();
    dof.controllersRecursive().forEach((controller) => controller.updateDisplay());
    window.addEventListener('resize', () => this.resizeDOF());
    this.installFocusPicking();
    folder.open();
    await this.applySceneMaterials();
    this.app.onSceneChanged(async () => {
      await this.applySceneMaterials();
      this.updateDisplay();
      this.resetTracing();
    });
    this.app.controls.addEventListener('change', () => this.resetTracing());
    this.updateNotes();
  }

  async applySceneMaterials() {
    try {
      this.pieceMaterialCount = await installOpenChessPieceMaterials(this.app.world);
    } catch (error) {
      console.warn('[openchess] piece MaterialX fallback failed', error);
    }
    this.sssMaterialCount = applyApproximateSSS(this.app.world);
  }

  updateNotes(extra = '') {
    const gpu = this.webgpuAvailable ? 'WebGPU is available.' : 'WebGPU is unavailable in this browser.';
    const cpu = this.cpuAvailable ? 'LightRT WASM is available.' : 'LightRT WASM is not present in this build.';
    this.app.setNotes([
      `<strong>${MODES[this.mode]}</strong><br><code>${gpu} ${cpu}</code>`,
      `<strong>MaterialX</strong><br><code>${this.sssMaterialCount || 0} material(s) use the lightweight raster SSS approximation.</code>`,
      `<strong>Raster DOF</strong><br><code>${this.params.dof ? 'Enabled' : 'Disabled'}; lightweight screen-space depth blur on WebGL2 raster.</code>`,
      '<strong>Asset</strong><br>The Open Chess Set is CC BY 4.0, Academy Software Foundation; original artwork by Moeen and Mujtaba Sayed.',
      extra
    ].filter(Boolean));
  }

  async select(mode) {
    if (mode === 'webgl') return this.useWebGL();
    if (mode === 'webgpu') return this.useWebGPU();
    if (mode === 'cpu') return this.useCPUTrace();
    if (mode === 'webgpuPath') return this.useWebGPUTrace();
  }

  stopRenderers() {
    this.webglRenderer?.setAnimationLoop(null);
    this.webgpuRenderer?.setAnimationLoop(null);
    this.traceCanvas.style.display = 'none';
    this.app.renderOverride = null;
  }

  initDOF() {
    const rect = this.app.viewport.getBoundingClientRect();
    this.params.focus = this.app.camera.position.distanceTo(this.app.controls.target);
    this.composer = new EffectComposer(this.webglRenderer);
    this.composer.addPass(new RenderPass(this.app.scene, this.app.camera));
    this.bokehPass = new BokehPass(this.app.scene, this.app.camera, {
      focus: this.params.focus,
      aperture: this.params.aperture,
      maxblur: this.params.maxBlur,
      width: Math.max(1, rect.width),
      height: Math.max(1, rect.height)
    });
    this.composer.addPass(this.bokehPass);
    // Post-processing render targets contain linear scene-referred values.
    // OutputPass must remain last so the renderer's selected tone mapper and
    // output color-space transform are applied after DOF.
    this.outputPass = new OutputPass();
    this.composer.addPass(this.outputPass);
    this.resizeDOF();
    this.updateDOF();
    this.app.renderOverride = () => this.renderDOF();
  }

  resizeDOF() {
    if (!this.composer) return;
    const rect = this.app.viewport.getBoundingClientRect();
    this.composer.setPixelRatio(Math.min(window.devicePixelRatio, 1.5) * this.params.dofResolution);
    this.composer.setSize(Math.max(1, rect.width), Math.max(1, rect.height));
  }

  updateDOF() {
    if (!this.bokehPass) return;
    this.bokehPass.enabled = this.params.dof;
    const uniforms = this.bokehPass.uniforms;
    uniforms.focus.value = this.params.focus;
    // BokehPass multiplies aperture by world-space distance from the focus
    // plane. Normalize by focus distance so the control behaves consistently
    // for a one-unit chess set as well as larger user-loaded scenes.
    uniforms.aperture.value = this.dofAperture();
    uniforms.maxblur.value = this.params.maxBlur;
    this.updateNotes();
  }

  renderDOF() {
    if (this.params.autoFocus) {
      const focus = this.app.camera.position.distanceTo(this.app.controls.target);
      if (Math.abs(focus - this.params.focus) > 1e-5) {
        this.params.focus = focus;
        this.bokehPass.uniforms.focus.value = focus;
        this.bokehPass.uniforms.aperture.value = this.dofAperture();
      }
    }
    this.composer.render();
  }

  dofAperture() {
    const focalScale = Math.pow(this.params.focalLength / 50, 2);
    return 0.04 * focalScale / (this.params.fStop * Math.max(this.params.focus, 0.01));
  }

  focusOnControlsTarget() {
    this.params.focus = this.app.camera.position.distanceTo(this.app.controls.target);
    this.updateDOF();
    this.app.gui.controllersRecursive().forEach((controller) => controller.updateDisplay());
  }

  installFocusPicking() {
    let pointerDown = null;
    this.app.viewport.addEventListener('pointerdown', (event) => {
      if (event.button === 0) pointerDown = { x: event.clientX, y: event.clientY };
    });
    this.app.viewport.addEventListener('pointerup', (event) => {
      if (!pointerDown || event.button !== 0) return;
      const movement = Math.hypot(event.clientX - pointerDown.x, event.clientY - pointerDown.y);
      pointerDown = null;
      if (movement <= 4 && this.params.clickToFocus && this.mode === 'webgl') {
        this.focusPickedPrimitive(event.clientX, event.clientY);
      }
    });
    this.app.viewport.addEventListener('pointercancel', () => { pointerDown = null; });
  }

  focusPickedPrimitive(clientX, clientY) {
    const rect = this.webglRenderer.domElement.getBoundingClientRect();
    if (!rect.width || !rect.height) return false;
    const pointer = new THREE.Vector2(
      ((clientX - rect.left) / rect.width) * 2 - 1,
      -((clientY - rect.top) / rect.height) * 2 + 1
    );
    const raycaster = new THREE.Raycaster();
    raycaster.setFromCamera(pointer, this.app.camera);
    const hit = raycaster.intersectObject(this.app.world, true)[0];
    if (!hit) return false;

    // BokehPass focus is distance along the camera look direction, rather than
    // Euclidean distance to the hit point.
    const cameraPoint = hit.point.clone().applyMatrix4(this.app.camera.matrixWorldInverse);
    this.params.focus = Math.max(this.app.camera.near, -cameraPoint.z);
    this.params.autoFocus = false;
    this.updateDOF();
    this.app.gui.controllersRecursive().forEach((controller) => controller.updateDisplay());
    const primitive = hit.object.userData?.usdPointInstance?.prototypePath ||
      hit.object.userData?.usdMesh?.primPath || hit.object.userData?.['primMeta.absPath'] ||
      hit.object.name || 'primitive';
    this.app.setStatus(`Focused ${primitive} at ${this.params.focus.toFixed(3)}`);
    return true;
  }

  updateDisplay() {
    const scene = this.app.scene;
    scene.environment = this.params.environment ? this.app.envMap : null;
    scene.background = this.params.environmentBackground
      ? (this.app.environmentSource || this.app.envMap)
      : new THREE.Color(0x0e0e10);
    scene.environmentIntensity = this.params.environmentIntensity;
    scene.backgroundIntensity = this.params.backgroundIntensity;
    scene.backgroundBlurriness = this.params.backgroundBlur;
    const angle = THREE.MathUtils.degToRad(this.params.environmentRotation);
    scene.environmentRotation.set(0, angle, 0);
    scene.backgroundRotation.set(0, angle, 0);
    const outputColorSpace = this.params.displayTransform === 'Linear sRGB'
      ? THREE.LinearSRGBColorSpace : THREE.SRGBColorSpace;
    for (const renderer of [this.webglRenderer, this.webgpuRenderer]) {
      if (!renderer) continue;
      renderer.outputColorSpace = outputColorSpace;
      renderer.toneMappingExposure = Math.pow(2, this.params.exposureEV);
      // WebGPU does not expose Three's custom GLSL hook; AgX is its closest
      // modern built-in fallback for the ACES 2 approximation.
      renderer.toneMapping = renderer === this.webgpuRenderer && this.params.toneMap === 'ACES 2.0 (approx)'
        ? THREE.AgXToneMapping : TONE_MAPS[this.params.toneMap];
    }
    this.app.params.envIntensity = this.params.environmentIntensity;
    this.app.applyEnvironmentToMaterials();
    this.resetTracing();
  }

  installCanvas(renderer) {
    for (const canvas of this.app.viewport.querySelectorAll('canvas')) canvas.style.display = 'none';
    renderer.domElement.style.display = 'block';
    this.app.renderer = renderer;
    this.app.controls.connect?.(renderer.domElement);
    this.app.resize();
    if (renderer === this.webglRenderer && this.composer) {
      this.resizeDOF();
      this.app.renderOverride = () => this.renderDOF();
    }
    renderer.setAnimationLoop(() => this.app.render());
  }

  useWebGL() {
    this.stopRenderers();
    this.mode = 'webgl';
    this.installCanvas(this.webglRenderer);
    this.updateNotes();
  }

  async ensureWebGPU() {
    if (this.webgpuRenderer) return this.webgpuRenderer;
    if (!navigator.gpu) throw new Error('WebGPU is not available.');
    const { WebGPURenderer } = await import('three/build/three.webgpu.js');
    const renderer = new WebGPURenderer({ antialias: true });
    renderer.setPixelRatio(Math.min(devicePixelRatio, 1.5));
    renderer.outputColorSpace = THREE.SRGBColorSpace;
    renderer.toneMapping = THREE.AgXToneMapping;
    renderer.toneMappingExposure = Math.pow(2, this.params.exposureEV);
    await renderer.init();
    this.app.viewport.appendChild(renderer.domElement);
    this.webgpuRenderer = renderer;
    this.updateDisplay();
    return renderer;
  }

  async useWebGPU() {
    try {
      this.app.setStatus('Initializing WebGPU raster...');
      const renderer = await this.ensureWebGPU();
      this.stopRenderers();
      this.mode = 'webgpu';
      this.installCanvas(renderer);
      this.app.setStatus('WebGPU raster active');
      this.updateNotes('WebGPU raster uses the same canonical material records; raster SSS remains approximate.');
    } catch (error) {
      this.params.renderer = 'webgl';
      this.useWebGL();
      this.app.setStatus(`WebGPU unavailable: ${error.message}`);
    }
  }

  resetTracing() {
    this.traceGeneration = (this.traceGeneration || 0) + 1;
    this.worker.postMessage({ type: 'cancel', generation: this.traceGeneration });
    this.accumulation = null;
    this.completedSamples = 0;
    clearTimeout(this.traceRestartTimer);
    if (this.mode === 'cpu' || this.mode === 'webgpuPath') {
      this.traceRestartTimer = setTimeout(() => this.startTraceBuild(), 180);
    }
  }

  useCPUTrace() {
    if (!this.cpuAvailable) {
      this.params.renderer = 'webgl';
      this.useWebGL();
      this.app.setStatus('This WASM build does not include LightRTPathTracer.');
      return;
    }
    this.startTrace('cpu');
  }

  useWebGPUTrace() {
    if (!this.webgpuAvailable) {
      this.params.renderer = 'webgl';
      this.useWebGL();
      return;
    }
    this.startTrace('webgpuPath');
  }

  startTrace(mode) {
    this.stopRenderers();
    this.mode = mode;
    this.traceCanvas.style.display = 'block';
    this.app.setStatus(`${MODES[mode]}: preparing scene...`);
    this.updateNotes('Progressive accumulation resets whenever the camera or render settings change.');
    // The native/compute drivers replace this placeholder as soon as their
    // acceleration structure is ready. Keeping raster as an explicit fallback
    // makes capability failures visible instead of silently changing shading.
    const ctx = this.traceCanvas.getContext('2d');
    const rect = this.app.viewport.getBoundingClientRect();
    this.traceCanvas.width = Math.max(1, Math.floor(rect.width * this.params.resolutionScale));
    this.traceCanvas.height = Math.max(1, Math.floor(rect.height * this.params.resolutionScale));
    ctx.fillStyle = '#101116'; ctx.fillRect(0, 0, this.traceCanvas.width, this.traceCanvas.height);
    ctx.fillStyle = '#eee'; ctx.font = '16px sans-serif';
    ctx.fillText(`${MODES[mode]} is preparing…`, 20, 32);
    if (mode === 'cpu' || mode === 'webgpuPath') this.startTraceBuild();
  }

  startTraceBuild() {
    const generation = ++this.traceGeneration;
    this.app.setStatus(`Packing OpenChessSet for ${MODES[this.mode]}...`);
    requestAnimationFrame(() => {
      try {
        const scene = packTraceScene(this.app.world);
        const transfer = Object.values(scene)
          .filter((value) => ArrayBuffer.isView(value))
          .map((array) => array.buffer);
        this.worker.postMessage({ type: 'build', generation, scene }, transfer);
      } catch (error) { this.app.setStatus(`Trace scene build failed: ${error.message}`); }
    });
  }

  startCPUTrace() {
    this.startTraceBuild();
  }

  requestCPUSamples() {
    const rect = this.app.viewport.getBoundingClientRect();
    const width = Math.max(1, Math.floor(rect.width * this.params.resolutionScale));
    const height = Math.max(1, Math.floor(rect.height * this.params.resolutionScale));
    this.traceCanvas.width = width; this.traceCanvas.height = height;
    this.app.camera.updateMatrixWorld(true);
    const inv = new THREE.Matrix4().multiplyMatrices(this.app.camera.projectionMatrix, this.app.camera.matrixWorldInverse).invert();
    this.worker.postMessage({ type: 'trace', generation: this.traceGeneration,
      invViewProjection: new Float32Array(inv.elements), cameraPosition: new Float32Array(this.app.camera.position.toArray()),
      width, height, sampleStart: this.completedSamples || 0,
      sampleCount: 1,
      bounces: this.params.bounces, exposure: 1 });
  }

  onWorkerMessage(message) {
    if (message.generation !== this.traceGeneration || (this.mode !== 'cpu' && this.mode !== 'webgpuPath')) return;
    if (message.type === 'built') {
      this.completedSamples = 0; this.accumulation = null;
      this.app.setStatus(`LightRT BVH ready: ${Math.round(message.triangles).toLocaleString()} triangles`);
      if (this.mode === 'cpu') this.requestCPUSamples();
      else this.worker.postMessage({ type: 'export-webgpu', generation: this.traceGeneration });
    } else if (message.type === 'samples') {
      const batch = message.pixels;
      if (!this.accumulation || this.accumulation.length !== batch.length) this.accumulation = new Float32Array(batch.length);
      const before = this.completedSamples; const after = before + message.sampleCount;
      for (let i = 0; i < batch.length; i += 4) {
        this.accumulation[i] = (this.accumulation[i] * before + batch[i] * message.sampleCount) / after;
        this.accumulation[i + 1] = (this.accumulation[i + 1] * before + batch[i + 1] * message.sampleCount) / after;
        this.accumulation[i + 2] = (this.accumulation[i + 2] * before + batch[i + 2] * message.sampleCount) / after;
        this.accumulation[i + 3] = 1;
      }
      this.completedSamples = after;
      this.drawAccumulation(message.width, message.height);
      this.app.setStatus(`LightRT CPU: ${after}/${this.params.samples} spp`);
      if (after < this.params.samples) this.requestCPUSamples();
    } else if (message.type === 'webgpu-scene') this.startWebGPUPath(message.scene);
    else if (message.type === 'error') this.app.setStatus(`LightRT error: ${message.message}`);
  }

  async startWebGPUPath(scene) {
    try {
      this.gpuPath?.destroy(); this.gpuPath = new OpenChessWebGPUPathTracer();
      await this.gpuPath.init(scene); this.completedSamples = 0; this.accumulation = null;
      await this.requestWebGPUSamples();
    } catch (error) { this.app.setStatus(`WebGPU path trace failed: ${error.message}`); }
  }

  async requestWebGPUSamples() {
    const generation = this.traceGeneration; if (this.mode !== 'webgpuPath') return;
    const rect = this.app.viewport.getBoundingClientRect(), width = Math.max(1, Math.floor(rect.width * this.params.resolutionScale)), height = Math.max(1, Math.floor(rect.height * this.params.resolutionScale));
    this.traceCanvas.width = width; this.traceCanvas.height = height;
    this.app.camera.updateMatrixWorld(true);
    const inv = new THREE.Matrix4().multiplyMatrices(this.app.camera.projectionMatrix, this.app.camera.matrixWorldInverse).invert();
    const count = 1;
    const batch = await this.gpuPath.trace({ inv: inv.elements, camera: this.app.camera.position.toArray(), width, height, sampleStart: this.completedSamples, sampleCount: count, bounces: this.params.bounces });
    if (generation !== this.traceGeneration || this.mode !== 'webgpuPath') return;
    const before = this.completedSamples, after = before + count;
    if (!this.accumulation || this.accumulation.length !== batch.length) this.accumulation = new Float32Array(batch.length);
    for (let i = 0; i < batch.length; i++) this.accumulation[i] = (this.accumulation[i] * before + batch[i] * count) / after;
    this.completedSamples = after; this.drawAccumulation(width, height); this.app.setStatus(`WebGPU path trace: ${after}/${this.params.samples} spp`);
    if (after < this.params.samples) requestAnimationFrame(() => this.requestWebGPUSamples());
  }

  drawAccumulation(width, height) {
    const bytes = new Uint8ClampedArray(width * height * 4);
    const exposure = this.webglRenderer.toneMappingExposure || 1;
    for (let i = 0; i < bytes.length; i += 4) {
      for (let c = 0; c < 3; c++) {
        const x = Math.max(0, this.accumulation[i + c] * exposure);
        const mapped = this.params.toneMap === 'None' ? Math.min(1, x) : this.toneMapScalar(x);
        bytes[i + c] = Math.round(255 * Math.pow(mapped, 1 / 2.2));
      }
      bytes[i + 3] = 255;
    }
    this.traceCanvas.getContext('2d').putImageData(new ImageData(bytes, width, height), 0, 0);
  }

  toneMapScalar(x) {
    if (this.params.toneMap === 'Linear') return Math.min(1, x);
    if (this.params.toneMap === 'Reinhard') return x / (1 + x);
    // CPU/WebGPU reference canvases use the same compact shoulder as the
    // default ACES 2 approximation; chroma compression remains raster-only.
    return Math.min(1, Math.max(0, (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14)));
  }
}

export async function installOpenChessRendering(app) {
  const rendering = new OpenChessRendering(app);
  window.__openChessRendering = rendering;
  await rendering.install();
  return rendering;
}
