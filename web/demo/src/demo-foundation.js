import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { HDRLoader } from 'three/examples/jsm/loaders/HDRLoader.js';
import { RectAreaLightUniformsLib } from 'three/examples/jsm/lights/RectAreaLightUniformsLib.js';
import { GUI } from 'lil-gui';

import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';
import { TinyUSDZLoaderUtils } from 'tinyusdz/TinyUSDZLoaderUtils.js';
import { TinyUSDZComposer } from 'tinyusdz/TinyUSDZComposer.js';
import {
  NodeGraphOptimizationLevel,
  analyzeNodeGraph,
  optimizeNodeGraph,
  setTinyUSDZ as setMaterialXTinyUSDZ
} from 'tinyusdz/TinyUSDZMaterialX.js';
import { MtlxMaterialConverter } from 'tinyusdz/TinyUSDZOpenPBR_WebGL.js';
import { extractSkinnedMeshData } from 'tinyusdz/USDSceneSkinningData.js';
import { buildSkeletonDataFromUSD } from 'tinyusdz/USDSkeletonData.js';
import { applyUSDSceneSkinningPipeline } from 'tinyusdz/USDSceneSkinningPipeline.js';
import { getUSDSceneMetadata } from 'tinyusdz/USDSceneMetadata.js';
import { buildNodeIndexMap } from 'tinyusdz/USDAnimationConverter.js';
import { extractUSDSceneAnimations } from 'tinyusdz/USDSceneAnimationPipeline.js';

RectAreaLightUniformsLib.init();

function bytesLabel(bytes) {
  if (!Number.isFinite(bytes)) return '';
  if (bytes > 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
  if (bytes > 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${bytes} B`;
}

function downloadBlob(blob, filename) {
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  a.remove();
  URL.revokeObjectURL(url);
}

function escapeHTML(value) {
  return String(value)
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;');
}

export async function initDemo(config) {
  const root = document.getElementById('demo-root') || document.body;
  const app = new DemoApp(config, root);
  window.__tinyusdzDemoApp = app;
  await app.start();
  return app;
}

class DemoApp {
  constructor(config, root) {
    this.config = {
      useDefaultLights: true,
      preferredMaterialType: 'auto',
      ...config
    };
    this.root = root;
    this.lastRenderTime = performance.now();
    this.loader = null;
    this.currentUsd = null;
    this.currentLabel = '';
    this.envMap = null;
    this.mixer = null;
    this.actions = [];
    this.skeletonHelpers = [];
    this.usdLights = [];
    this.nodeGraphStats = null;
    this.params = {
      grid: true,
      defaultLights: this.config.useDefaultLights !== false,
      envIntensity: 1.4,
      ambient: 0.35,
      keyLight: 2.0,
      play: true,
      speed: 1.0,
      showSkeleton: !!this.config.showSkeleton
    };
  }

  async start() {
    document.title = `${this.config.title} - TinyUSDZ Demo`;
    this.createShell();
    this.setupThree();
    this.setupEvents();
    this.setupGUI();
    await this.loadDefaultEnvironment();
    await this.loadDefaultAsset();
  }

  createShell() {
    this.root.innerHTML = `
      <div class="demo-shell">
        <header class="demo-toolbar">
          <div>
            <a class="demo-back" href="./">Demos</a>
            <h1>${escapeHTML(this.config.title)}</h1>
            <p>${escapeHTML(this.config.subtitle || '')}</p>
          </div>
          <div class="demo-actions">
            <button id="open-file" type="button">Open USD</button>
            <button id="load-default" type="button">Load Sample</button>
            <button id="fit-scene" type="button">Fit</button>
          </div>
        </header>
        <main class="demo-main">
          <section class="viewport-wrap">
            <div id="viewport" class="viewport"></div>
            <div id="drop-hint" class="drop-hint">Drop USDA, USDC, USD, or USDZ</div>
            <div id="status" class="status">Initializing...</div>
          </section>
          <aside class="info-panel">
            <h2>Controls</h2>
            <div id="gui-container" class="gui-container"></div>
            <h2>Scene</h2>
            <dl id="scene-stats"></dl>
            <h2>Notes</h2>
            <div id="notes"></div>
          </aside>
        </main>
        <input id="file-input" type="file" accept=".usd,.usda,.usdc,.usdz" hidden>
      </div>
    `;

    this.viewport = this.root.querySelector('#viewport');
    this.statusEl = this.root.querySelector('#status');
    this.statsEl = this.root.querySelector('#scene-stats');
    this.notesEl = this.root.querySelector('#notes');
    this.guiContainer = this.root.querySelector('#gui-container');
    this.dropHint = this.root.querySelector('#drop-hint');
    this.fileInput = this.root.querySelector('#file-input');
    this.root.querySelector('#open-file').addEventListener('click', () => this.fileInput.click());
    this.root.querySelector('#load-default').addEventListener('click', () => this.loadDefaultAsset());
    this.root.querySelector('#fit-scene').addEventListener('click', () => this.fitScene());
    this.setNotes([
      'Drag and drop a USD file anywhere on the viewport.',
      'Use orbit controls to navigate. Press F to fit the scene.',
      this.config.materialModeLabel ? `Material mode: ${this.config.materialModeLabel}.` : null
    ].filter(Boolean));
  }

  setupThree() {
    this.scene = new THREE.Scene();
    this.scene.background = new THREE.Color(0x0e0e10);

    this.world = new THREE.Group();
    this.world.name = 'TinyUSDZDemoWorld';
    this.scene.add(this.world);

    this.lightGroup = new THREE.Group();
    this.lightGroup.name = 'Lights';
    this.scene.add(this.lightGroup);

    this.grid = new THREE.GridHelper(10, 20, 0x44444a, 0x26262b);
    this.grid.position.y = -0.01;
    this.scene.add(this.grid);

    this.camera = new THREE.PerspectiveCamera(50, 1, 0.01, 1000);
    this.camera.position.set(3, 2.2, 4);

    this.renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 1.0;
    this.renderer.shadowMap.enabled = true;
    this.viewport.appendChild(this.renderer.domElement);

    this.pmremGenerator = new THREE.PMREMGenerator(this.renderer);

    this.controls = new OrbitControls(this.camera, this.renderer.domElement);
    this.controls.enableDamping = true;
    this.controls.dampingFactor = 0.08;

    this.ambientLight = new THREE.AmbientLight(0xffffff, this.params.ambient);
    this.keyLight = new THREE.DirectionalLight(0xffffff, this.params.keyLight);
    this.keyLight.position.set(4, 6, 5);
    this.fillLight = new THREE.DirectionalLight(0x9fb7ff, 0.5);
    this.fillLight.position.set(-4, 3, -3);
    this.lightGroup.add(this.ambientLight, this.keyLight, this.fillLight);

    window.addEventListener('resize', () => this.resize());
    this.resize();
    this.renderer.setAnimationLoop(() => this.render());
  }

  setupEvents() {
    this.fileInput.addEventListener('change', () => {
      const file = this.fileInput.files?.[0];
      if (file) this.loadFile(file);
      this.fileInput.value = '';
    });

    this.viewport.addEventListener('dragenter', (event) => {
      event.preventDefault();
      this.dropHint.classList.add('active');
    });
    this.viewport.addEventListener('dragover', (event) => {
      event.preventDefault();
      this.dropHint.classList.add('active');
    });
    this.viewport.addEventListener('dragleave', () => {
      this.dropHint.classList.remove('active');
    });
    this.viewport.addEventListener('drop', (event) => {
      event.preventDefault();
      this.dropHint.classList.remove('active');
      const file = event.dataTransfer?.files?.[0];
      if (file) this.loadFile(file);
    });

    window.addEventListener('keydown', (event) => {
      if (event.key.toLowerCase() === 'f') {
        this.fitScene();
      }
    });
  }

  setupGUI() {
    this.gui = new GUI({ title: this.config.title, container: this.guiContainer });
    this.gui.add({ open: () => this.fileInput.click() }, 'open').name('Open USD');
    this.gui.add({ sample: () => this.loadDefaultAsset() }, 'sample').name('Load sample');
    this.gui.add({ fit: () => this.fitScene() }, 'fit').name('Fit scene (F)');
    this.gui.add(this.params, 'grid').name('Grid').onChange((value) => {
      this.grid.visible = value;
    });
    this.gui.add(this.params, 'defaultLights').name('Default lights').onChange((value) => {
      this.lightGroup.visible = value;
    });
    this.gui.add(this.params, 'ambient', 0, 3, 0.01).name('Ambient').onChange((value) => {
      this.ambientLight.intensity = value;
    });
    this.gui.add(this.params, 'keyLight', 0, 8, 0.01).name('Key light').onChange((value) => {
      this.keyLight.intensity = value;
    });
    this.gui.add(this.params, 'envIntensity', 0, 8, 0.01).name('Env intensity').onChange(() => {
      this.applyEnvironmentToMaterials();
    });

    if (this.config.enableSkinning) {
      this.gui.add(this.params, 'showSkeleton').name('Skeleton').onChange((value) => {
        for (const helper of this.skeletonHelpers) helper.visible = value;
      });
    }

    if (this.config.enableAnimation) {
      const anim = this.gui.addFolder('Animation');
      anim.add(this.params, 'play').name('Play');
      anim.add(this.params, 'speed', 0, 4, 0.01).name('Speed');
    }

    if (this.config.enableExport) {
      const exp = this.gui.addFolder('Export');
      exp.add({ USDA: () => this.exportCurrent('usda') }, 'USDA').name('Download USDA');
      exp.add({ USDC: () => this.exportCurrent('usdc') }, 'USDC').name('Download USDC');
      exp.add({ USDZ: () => this.exportCurrent('usdz') }, 'USDZ').name('Download USDZ');
    }
  }

  async ensureLoader() {
    if (this.loader) return this.loader;

    this.setStatus('Initializing TinyUSDZ WASM...');
    this.loader = new TinyUSDZLoader(null, { maxMemoryLimitMB: 512 });
    await this.loader.init({ useZstdCompressedWasm: false, useMemory64: false });
    TinyUSDZLoaderUtils.setTinyUSDZ(this.loader.native_);
    setMaterialXTinyUSDZ(this.loader.native_);
    this.loader.setMaxMemoryLimitMB(512);
    if (this.config.enableSkinning) {
      this.loader.setRoundBoneCount(true);
    }
    return this.loader;
  }

  async loadDefaultEnvironment() {
    try {
      const texture = await new HDRLoader().loadAsync('./assets/textures/goegap_1k.hdr');
      texture.mapping = THREE.EquirectangularReflectionMapping;
      this.envMap = this.pmremGenerator.fromEquirectangular(texture).texture;
      texture.dispose();
      this.scene.environment = this.envMap;
    } catch (error) {
      console.warn('Default HDR environment unavailable:', error);
    }
  }

  async loadDefaultAsset() {
    if (!this.config.defaultAsset) return;
    await this.loadURL(this.config.defaultAsset, this.config.title);
  }

  async loadURL(url, label = url) {
    await this.ensureLoader();
    this.setStatus(`Loading ${label}...`);
    try {
      let usd;
      if (this.config.useComposition) {
        usd = await this.loadComposedLayer(url);
      } else if (this.config.useLayerExport) {
        usd = await this.loadLayerForExport(url);
      } else {
        usd = await this.loader.loadAsync(url, {
          onFetchProgress: (loaded, total) => {
            const suffix = total ? `${bytesLabel(loaded)} / ${bytesLabel(total)}` : bytesLabel(loaded);
            this.setStatus(`Downloading ${label}: ${suffix}`);
          }
        });
      }
      await this.displayUSD(usd, label);
    } catch (error) {
      console.error(error);
      this.setStatus(`Failed: ${error.message}`);
    }
  }

  async loadFile(file) {
    await this.ensureLoader();
    this.setStatus(`Reading ${file.name} (${bytesLabel(file.size)})...`);
    try {
      const data = new Uint8Array(await file.arrayBuffer());
      const usd = this.config.useLayerExport
        ? this.parseLayerForExport(data, file.name)
        : await new Promise((resolve, reject) => {
          this.loader.parse(data, file.name, resolve, reject, { maxMemoryLimitMB: 512 });
        });
      await this.displayUSD(usd, file.name);
    } catch (error) {
      console.error(error);
      this.setStatus(`Failed: ${error.message}`);
    }
  }

  async loadComposedLayer(url) {
    const layer = await this.loader.loadAsLayerAsync(url);
    const composer = new TinyUSDZComposer();
    composer.setLayer(layer);
    composer.setUSDLoader(this.loader);
    composer.setBaseWorkingPath('./assets');
    composer.setAssetSearchPaths(['./assets']);
    await composer.progressiveComposition();
    const composed = composer.getLayer();
    composed.layerToRenderScene();
    return composed;
  }

  async loadLayerForExport(url) {
    const layer = await this.loader.loadAsLayerAsync(url);
    layer.layerToRenderScene();
    return layer;
  }

  parseLayerForExport(data, filename) {
    const layer = new this.loader.native_.TinyUSDZLoaderNative();
    layer.setMaxMemoryLimitMB(512);
    this.loader._applySkinningLoadOptions?.(layer);
    const ok = layer.loadAsLayerFromBinary(data, filename);
    if (!ok) {
      throw new Error(layer.error?.() || `Failed to load ${filename} as USD layer.`);
    }
    layer.layerToRenderScene();
    return layer;
  }

  async displayUSD(usd, label) {
    this.currentUsd = usd;
    this.currentLabel = label;
    this.clearSceneObjects();
    this.applySceneUpAxis(usd);

    if (this.config.useUsdLux) {
      await this.applyDomeLight(usd);
    } else {
      this.scene.environment = this.envMap;
    }

    const sceneRoot = await this.buildThreeRoot(usd);

    if (this.config.materialBackend === 'nodegraph') {
      this.applyNodeGraphMaterials(sceneRoot);
    }

    if (this.config.enableSkinning || this.config.enableAnimation) {
      await this.applySkinningAndAnimation(usd, sceneRoot);
    } else {
      this.world.add(sceneRoot);
    }

    if (this.config.useUsdLux) {
      this.addUSDLights(usd);
    }

    this.applyEnvironmentToMaterials();
    this.updateStats(usd, label);
    if (this.config.enableMaterialGraph) {
      this.showMaterialGraph(usd);
    } else if (this.config.enableExport) {
      this.showExportNotes(usd);
    }
    this.fitScene();
    this.setStatus(`Loaded ${label}`);
  }

  async buildThreeRoot(usd) {
    const root = new THREE.Group();
    root.name = 'USD Scene';
    const defaultMaterial = TinyUSDZLoaderUtils.createDefaultMaterial();
    defaultMaterial.envMap = this.scene.environment || this.envMap;
    const options = {
      overrideMaterial: false,
      preferredMaterialType: this.config.preferredMaterialType,
      envMap: this.scene.environment || this.envMap,
      envMapIntensity: this.params.envIntensity,
      textureCache: new Map(),
      onProgress: (info) => this.setStatus(info.message || 'Building scene...')
    };

    const rootCount = usd.numRootNodes ? Math.max(usd.numRootNodes(), 1) : 1;
    for (let i = 0; i < rootCount; i++) {
      const usdNode = rootCount > 1 && usd.getRootNode ? usd.getRootNode(i) : usd.getDefaultRootNode();
      const threeNode = await TinyUSDZLoaderUtils.buildThreeNode(usdNode, defaultMaterial, usd, options);
      root.add(threeNode);
    }
    return root;
  }

  getSceneUpAxis(usd) {
    try {
      const metadata = getUSDSceneMetadata(usd);
      const axis = metadata.fileUpAxis || metadata.upAxis || (usd?.getUpAxis ? usd.getUpAxis() : 'Y');
      return String(axis || 'Y').toUpperCase();
    } catch {
      return usd?.getUpAxis ? String(usd.getUpAxis() || 'Y').toUpperCase() : 'Y';
    }
  }

  applySceneUpAxis(usd) {
    this.currentUpAxis = this.getSceneUpAxis(usd);
    this.world.rotation.set(0, 0, 0);
    if (this.currentUpAxis === 'Z') {
      this.world.rotation.x = -Math.PI / 2;
    }
    this.world.updateMatrixWorld(true);
  }

  applyNodeGraphMaterials(sceneRoot) {
    const converter = new MtlxMaterialConverter();
    let converted = 0;
    let inspected = 0;

    sceneRoot.traverse((object) => {
      if (!object.isMesh || !object.material) return;
      const originalMaterials = Array.isArray(object.material) ? object.material : [object.material];
      const nextMaterials = originalMaterials.map((material) => {
        inspected++;
        const materialData = material.userData?.openPBRData;
        const openPBR = materialData?.openPBR || materialData?.openPBRShader || materialData;
        const nodeGraph = openPBR?.nodeGraph || materialData?.nodeGraph;
        if (!nodeGraph?.nodegraph) return material;

        const optimized = optimizeNodeGraph(nodeGraph, NodeGraphOptimizationLevel.STANDARD);
        const convertedMaterial = converter.createMaterial({
          ...materialData,
          openPBR: {
            ...openPBR,
            nodeGraph: optimized
          }
        }, {
          loadTextures: false
        });
        convertedMaterial.name = material.name || convertedMaterial.name;
        convertedMaterial.side = material.side;
        this.copyTextureMaps(material, convertedMaterial);
        if ('envMap' in convertedMaterial) convertedMaterial.envMap = this.scene.environment || this.envMap;
        if ('envMapIntensity' in convertedMaterial) convertedMaterial.envMapIntensity = this.params.envIntensity;
        convertedMaterial.userData = {
          ...material.userData,
          materialBackend: 'MaterialX WebGL2 NodeGraph',
          optimizedNodeGraph: optimized
        };
        converted++;
        return convertedMaterial;
      });
      object.material = Array.isArray(object.material) ? nextMaterials : nextMaterials[0];
    });

    this.nodeGraphStats = { converted, inspected };
  }

  copyTextureMaps(sourceMaterial, targetMaterial) {
    const textureMapNames = [
      'map',
      'normalMap',
      'roughnessMap',
      'metalnessMap',
      'emissiveMap',
      'alphaMap',
      'aoMap',
      'bumpMap',
      'displacementMap',
      'clearcoatMap',
      'clearcoatNormalMap',
      'clearcoatRoughnessMap',
      'sheenColorMap',
      'sheenRoughnessMap',
      'specularColorMap',
      'iridescenceMap',
      'iridescenceThicknessMap',
      'transmissionMap',
      'thicknessMap'
    ];

    for (const mapName of textureMapNames) {
      if (!targetMaterial[mapName] && sourceMaterial[mapName]) {
        targetMaterial[mapName] = sourceMaterial[mapName];
      }
    }

    if (sourceMaterial.normalScale && !targetMaterial.normalScale) {
      targetMaterial.normalScale = sourceMaterial.normalScale.clone?.() || sourceMaterial.normalScale;
    }
    targetMaterial.needsUpdate = true;
  }

  async applySkinningAndAnimation(usd, threeNode) {
    const metadata = getUSDSceneMetadata(usd);
    const skinningData = extractSkinnedMeshData(usd, { logger: console, verbose: false });
    const skeletonData = buildSkeletonDataFromUSD(usd, {
      logger: console,
      hasSkinnedMeshData: skinningData.hasSkinnedMeshData
    });

    const nodeIndexMap = buildNodeIndexMap(threeNode);
    const skinningResult = applyUSDSceneSkinningPipeline({
      threeNode,
      characterGroup: this.world,
      helperScene: this.world,
      skeletonDataArray: skeletonData.skeletonDataArray,
      allSkinnedMeshUSDData: skinningData.allSkinnedMeshUSDData,
      skinnedMeshDataByName: skinningData.skinnedMeshDataByName,
      usdScene: usd,
      showMesh: true,
      showSkeleton: this.params.showSkeleton,
      useWASMBoneTexture: false,
      logger: console
    });
    this.skeletonHelpers = skinningResult.skeletonHelpers || [];

    if (this.config.enableAnimation) {
      const animData = extractUSDSceneAnimations(usd, {
        boneMaps: skeletonData.boneMaps,
        nodeIndexMap,
        timeCodesPerSecond: metadata.timeCodesPerSecond,
        logger: console
      });
      const clips = [...animData.usdAnimations, ...animData.usdNodeAnimations];
      if (clips.length > 0) {
        this.mixer = new THREE.AnimationMixer(this.world);
        this.mixer.timeScale = metadata.timeCodesPerSecond;
        this.actions = clips.map((clip) => {
          const action = this.mixer.clipAction(clip);
          action.play();
          return action;
        });
        this.setNotes([
          `${clips.length} animation clip(s) loaded.`,
          'Use the GUI Animation folder to pause or change playback speed.'
        ]);
      }
    }
  }

  async applyDomeLight(usd) {
    try {
      const dome = await TinyUSDZLoaderUtils.loadDomeLightFromUSD(usd, this.pmremGenerator);
      if (dome?.texture) {
        this.scene.environment = dome.texture;
        this.scene.background = dome.texture;
        this.params.envIntensity = dome.intensity || this.params.envIntensity;
        return;
      }
    } catch (error) {
      console.warn('DomeLight load failed:', error);
    }
    this.scene.environment = this.envMap;
    this.scene.background = new THREE.Color(0x0e0e10);
  }

  addUSDLights(usd) {
    if (!usd?.numLights) return;
    const numLights = usd.numLights();
    for (let i = 0; i < numLights; i++) {
      const lightData = usd.getLight(i);
      if (!lightData || lightData.error) continue;
      const type = String(lightData.type || '').toLowerCase();
      if (type === 'dome' || type === 'domelight') continue;

      let intensity = lightData.intensity ?? 1;
      if (lightData.exposure) intensity *= Math.pow(2, lightData.exposure);
      const color = new THREE.Color(
        lightData.color?.[0] ?? 1,
        lightData.color?.[1] ?? 1,
        lightData.color?.[2] ?? 1
      );
      const position = new THREE.Vector3(
        lightData.position?.[0] ?? 0,
        lightData.position?.[1] ?? 0,
        lightData.position?.[2] ?? 0
      );
      const quaternion = new THREE.Quaternion();
      if (lightData.transform?.length === 16) {
        const matrix = new THREE.Matrix4();
        matrix.fromArray(lightData.transform);
        matrix.decompose(position, quaternion, new THREE.Vector3());
      }

      let light = null;
      if (type === 'point' || type === 'sphere') {
        if (lightData.shapingConeAngle && lightData.shapingConeAngle < 90) {
          light = new THREE.SpotLight(color, intensity, 0, THREE.MathUtils.degToRad(lightData.shapingConeAngle), lightData.shapingConeSoftness || 0);
          light.target.position.set(0, 0, -1);
          light.add(light.target);
        } else {
          light = new THREE.PointLight(color, intensity);
        }
      } else if (type === 'distant') {
        light = new THREE.DirectionalLight(color, intensity);
        light.position.copy((lightData.direction ? new THREE.Vector3(...lightData.direction) : new THREE.Vector3(0, -1, 0)).multiplyScalar(-5));
      } else if (type === 'rect' || type === 'disk') {
        light = new THREE.RectAreaLight(color, intensity, lightData.width || lightData.radius || 1, lightData.height || lightData.radius || 1);
      }

      if (!light) continue;
      light.name = lightData.name || `USDLight_${i}`;
      if (type !== 'distant') {
        light.position.copy(position);
        light.quaternion.copy(quaternion);
      }
      this.world.add(light);
      this.usdLights.push(light);
    }
  }

  showMaterialGraph(usd) {
    const lines = [];
    const numMaterials = usd.numMaterials ? usd.numMaterials() : 0;
    for (let i = 0; i < numMaterials; i++) {
      try {
        const result = usd.getMaterialWithFormat(i, 'json');
        const data = result?.data ? JSON.parse(result.data) : usd.getMaterial(i);
        const label = data.name || data.displayName || `Material ${i}`;
        const open = data.openPBR || data.openPBRShader || data.openPBRSurface || data;
        const nodeGraph = open?.nodeGraph || data.nodeGraph;
        if (nodeGraph?.nodegraph) {
          const analysis = analyzeNodeGraph(nodeGraph);
          const optimized = optimizeNodeGraph(nodeGraph, NodeGraphOptimizationLevel.STANDARD);
          const nodes = nodeGraph.nodegraph.nodes?.length || 0;
          const outputs = nodeGraph.nodegraph.outputs?.length || 0;
          const optimizedNodes = optimized.nodegraph?.nodes?.length || nodes;
          const reduction = analysis.potentialReduction;
          const analysisText = analysis.error || (
            reduction
              ? `${reduction.patternsFound} pattern(s), ${reduction.identitiesFound} identity op(s), ${reduction.nodesRemovable} removable node(s).`
              : 'No optimizer analysis available.'
          );
          lines.push(
            `<strong>${escapeHTML(label)}</strong><br>` +
            `<code>${nodes} node(s), ${outputs} output(s), ${optimizedNodes} after optimize. ` +
            `${escapeHTML(analysisText)}</code>`
          );
        } else {
          const keys = Object.keys(open || {}).slice(0, 12);
          lines.push(`<strong>${escapeHTML(label)}</strong><br><code>${escapeHTML(keys.join(' -> ') || 'material data')}</code>`);
        }
      } catch (error) {
        lines.push(`Material ${i}: ${escapeHTML(error.message)}`);
      }
    }
    if (this.nodeGraphStats) {
      lines.unshift(
        `<strong>Renderer</strong><br><code>${this.nodeGraphStats.converted} of ${this.nodeGraphStats.inspected} mesh material slot(s) using MaterialX WebGL2 node graph evaluation.</code>`
      );
    }
    if (lines.length > 0) {
      this.setNotes(lines);
    }
  }

  exportCurrent(kind) {
    if (!this.currentUsd) {
      this.setStatus('No USD scene loaded.');
      return;
    }
    try {
      if (kind === 'usda') {
        const data = typeof this.currentUsd.exportAsUSDA === 'function'
          ? this.currentUsd.exportAsUSDA()
          : this.currentUsd.layerToString?.();
        if (!data) throw new Error(this.currentUsd.error?.() || 'USDA export returned empty data.');
        downloadBlob(new Blob([data], { type: 'text/plain' }), 'tinyusdz-export.usda');
      } else if (kind === 'usdc') {
        if (typeof this.currentUsd.exportAsUSDC !== 'function') {
          this.setStatus('USDC export is not available in this WASM build.');
          return;
        }
        const bytes = new Uint8Array(this.currentUsd.exportAsUSDC());
        downloadBlob(new Blob([bytes], { type: 'application/octet-stream' }), 'tinyusdz-export.usdc');
      } else if (kind === 'usdz') {
        if (typeof this.currentUsd.exportAsUSDZ !== 'function') {
          this.setStatus('USDZ export is not available in this WASM build.');
          return;
        }
        const bytes = new Uint8Array(this.currentUsd.exportAsUSDZ());
        downloadBlob(new Blob([bytes], { type: 'model/vnd.usdz+zip' }), 'tinyusdz-export.usdz');
      }
      this.setStatus(`Exported ${kind.toUpperCase()}.`);
    } catch (error) {
      console.error(error);
      this.setStatus(`Export failed: ${error.message}`);
    }
  }

  showExportNotes(usd) {
    const hasUSDA = typeof usd.exportAsUSDA === 'function' || typeof usd.layerToString === 'function';
    const hasUSDC = typeof usd.exportAsUSDC === 'function';
    const hasUSDZ = typeof usd.exportAsUSDZ === 'function';
    this.setNotes([
      hasUSDA ? 'USDA export is available for the loaded layer.' : 'USDA export is not available for the loaded scene.',
      hasUSDC && hasUSDZ
        ? 'USDC and USDZ export bindings are available in this WASM build.'
        : 'USDC and USDZ buttons are kept for newer WASM builds; this local build exposes USDA layer export only.'
    ]);
  }

  applyEnvironmentToMaterials() {
    const env = this.scene.environment || this.envMap;
    this.world.traverse((object) => {
      const materials = object.material ? (Array.isArray(object.material) ? object.material : [object.material]) : [];
      for (const material of materials) {
        if ('envMap' in material) material.envMap = env;
        if ('envMapIntensity' in material) material.envMapIntensity = this.params.envIntensity;
        material.needsUpdate = true;
      }
    });
  }

  clearSceneObjects() {
    this.world.clear();
    this.world.rotation.set(0, 0, 0);
    for (const light of this.usdLights) {
      this.scene.remove(light);
    }
    this.usdLights = [];
    for (const helper of this.skeletonHelpers) {
      this.scene.remove(helper);
    }
    this.skeletonHelpers = [];
    this.nodeGraphStats = null;
    if (this.mixer) this.mixer.stopAllAction();
    this.mixer = null;
    this.actions = [];
    this.scene.background = new THREE.Color(0x0e0e10);
    this.scene.environment = this.envMap;
  }

  updateStats(usd, label) {
    const stats = {
      Source: label,
      'Up axis': this.currentUpAxis || this.getSceneUpAxis(usd),
      Meshes: usd.numMeshes ? usd.numMeshes() : 0,
      Materials: usd.numMaterials ? usd.numMaterials() : 0,
      Textures: usd.numTextures ? usd.numTextures() : 0,
      Images: usd.numImages ? usd.numImages() : 0,
      Lights: usd.numLights ? usd.numLights() : 0,
      Skeletons: usd.numSkeletons ? usd.numSkeletons() : 0,
      Animations: usd.numAnimations ? usd.numAnimations() : 0
    };
    this.statsEl.innerHTML = Object.entries(stats)
      .map(([key, value]) => `<dt>${escapeHTML(key)}</dt><dd>${escapeHTML(value)}</dd>`)
      .join('');
  }

  setNotes(lines) {
    this.notesEl.innerHTML = lines.map((line) => `<p>${line}</p>`).join('');
  }

  setStatus(message) {
    this.statusEl.textContent = message;
  }

  fitScene() {
    const box = new THREE.Box3().setFromObject(this.world);
    if (box.isEmpty()) return;
    const size = box.getSize(new THREE.Vector3());
    const center = box.getCenter(new THREE.Vector3());
    const maxDim = Math.max(size.x, size.y, size.z, 0.1);
    const fitHeightDistance = maxDim / (2 * Math.atan((Math.PI * this.camera.fov) / 360));
    const fitWidthDistance = fitHeightDistance / this.camera.aspect;
    const distance = Math.max(fitHeightDistance, fitWidthDistance) * 1.45;
    const direction = new THREE.Vector3(0.8, 0.55, 1).normalize();
    this.camera.position.copy(center).add(direction.multiplyScalar(distance));
    this.camera.near = Math.max(distance / 100, 0.001);
    this.camera.far = distance * 100;
    this.camera.updateProjectionMatrix();
    this.controls.target.copy(center);
    this.controls.update();
  }

  resize() {
    const rect = this.viewport.getBoundingClientRect();
    const width = Math.max(1, rect.width);
    const height = Math.max(1, rect.height);
    this.camera.aspect = width / height;
    this.camera.updateProjectionMatrix();
    this.renderer.setSize(width, height, false);
  }

  render() {
    const now = performance.now();
    const delta = Math.min((now - this.lastRenderTime) / 1000, 0.1);
    this.lastRenderTime = now;
    if (this.mixer && this.params.play) {
      this.mixer.update(delta * this.params.speed);
    }
    this.controls.update();
    this.renderer.render(this.scene, this.camera);
  }
}
