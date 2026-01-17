// Blender Bridge Viewer
// Three.js viewer with TinyUSDZ WASM integration and WebSocket bridge

import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { RGBELoader } from 'three/examples/jsm/loaders/RGBELoader.js';
import { TinyUSDZLoader } from './tinyusdz/TinyUSDZLoader.js';
import { TinyUSDZLoaderUtils } from './tinyusdz/TinyUSDZLoaderUtils.js';
import { BridgeClient } from './client/bridge-client.js';
import { ParameterSync } from './client/parameter-sync.js';

// ============================================================================
// Constants
// ============================================================================

const DEFAULT_BACKGROUND_COLOR = 0x1a1a1a;
const CAMERA_PADDING = 1.2;

// ============================================================================
// Main Application
// ============================================================================

class BlenderBridgeViewer {
  constructor() {
    // Three.js state
    this.scene = null;
    this.camera = null;
    this.renderer = null;
    this.controls = null;
    this.pmremGenerator = null;

    // TinyUSDZ loader
    this.loader = null;
    this.loaderReady = false;

    // Scene content
    this.sceneRoot = null;
    this.envMap = null;

    // Bridge components
    this.bridgeClient = null;
    this.parameterSync = null;

    // Stats
    this.frameCount = 0;
    this.lastFpsUpdate = 0;
    this.fps = 0;

    // DOM elements
    this.elements = {
      container: document.getElementById('canvas-container'),
      connectBtn: document.getElementById('connect-btn'),
      disconnectBtn: document.getElementById('disconnect-btn'),
      serverUrl: document.getElementById('server-url'),
      sessionId: document.getElementById('session-id'),
      connectionStatus: document.getElementById('connection-status'),
      statsPanel: document.getElementById('stats-panel'),
      loadingOverlay: document.getElementById('loading-overlay'),
      loadingMessage: document.getElementById('loading-message'),
      messageLog: document.getElementById('message-log'),
      logContent: document.getElementById('log-content'),
      sceneStatus: document.getElementById('scene-status'),
      meshCount: document.getElementById('mesh-count'),
      materialCount: document.getElementById('material-count'),
      lightCount: document.getElementById('light-count'),
      fpsValue: document.getElementById('fps-value'),
      fitSceneBtn: document.getElementById('fit-scene-btn')
    };
  }

  /**
   * Initialize the viewer
   */
  async init() {
    this.log('Initializing viewer...', 'info');

    // Initialize Three.js
    this.initThreeJS();

    // Initialize TinyUSDZ loader
    await this.initLoader();

    // Initialize bridge components
    this.initBridge();

    // Setup event listeners
    this.setupEventListeners();

    // Start render loop
    this.animate();

    this.log('Viewer ready', 'success');
  }

  /**
   * Initialize Three.js scene
   */
  initThreeJS() {
    // Scene
    this.scene = new THREE.Scene();
    this.scene.background = new THREE.Color(DEFAULT_BACKGROUND_COLOR);

    // Camera
    const aspect = window.innerWidth / window.innerHeight;
    this.camera = new THREE.PerspectiveCamera(45, aspect, 0.01, 1000);
    this.camera.position.set(5, 3, 5);

    // Renderer
    this.renderer = new THREE.WebGLRenderer({
      antialias: true,
      alpha: true
    });
    this.renderer.setSize(window.innerWidth, window.innerHeight);
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 1.0;
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;
    this.elements.container.appendChild(this.renderer.domElement);

    // Controls
    this.controls = new OrbitControls(this.camera, this.renderer.domElement);
    this.controls.enableDamping = true;
    this.controls.dampingFactor = 0.05;

    // PMREM for environment maps
    this.pmremGenerator = new THREE.PMREMGenerator(this.renderer);
    this.pmremGenerator.compileEquirectangularShader();

    // Default lighting
    this.setupDefaultLighting();

    // Handle resize
    window.addEventListener('resize', () => this.onResize());
  }

  /**
   * Setup default lighting
   */
  setupDefaultLighting() {
    // Ambient
    const ambient = new THREE.AmbientLight(0xffffff, 0.3);
    this.scene.add(ambient);

    // Key light
    const keyLight = new THREE.DirectionalLight(0xffffff, 1.0);
    keyLight.position.set(5, 10, 5);
    keyLight.castShadow = true;
    this.scene.add(keyLight);

    // Fill light
    const fillLight = new THREE.DirectionalLight(0x8888ff, 0.3);
    fillLight.position.set(-5, 5, -5);
    this.scene.add(fillLight);
  }

  /**
   * Initialize TinyUSDZ loader
   */
  async initLoader() {
    this.showLoading('Initializing TinyUSDZ WASM...');

    try {
      this.loader = new TinyUSDZLoader();
      await this.loader.init({ useMemory64: false });
      this.loaderReady = true;
      this.log('TinyUSDZ loader ready', 'success');
    } catch (err) {
      this.log(`Failed to initialize loader: ${err.message}`, 'error');
      throw err;
    } finally {
      this.hideLoading();
    }
  }

  /**
   * Initialize bridge components
   */
  initBridge() {
    this.bridgeClient = new BridgeClient();
    this.parameterSync = new ParameterSync();
    this.parameterSync.setControls(this.controls);

    // Setup bridge event handlers
    this.bridgeClient.onSceneUpload = (data) => this.handleSceneUpload(data);
    this.bridgeClient.onParameterUpdate = (data) => this.handleParameterUpdate(data);
    this.bridgeClient.onError = (error) => this.log(`Error: ${error.message}`, 'error');

    this.bridgeClient.addEventListener('connected', () => {
      this.setConnectionStatus('connected');
      this.elements.statsPanel.classList.remove('hidden');
      this.elements.messageLog.classList.remove('hidden');
    });

    this.bridgeClient.addEventListener('disconnected', () => {
      this.setConnectionStatus('disconnected');
    });
  }

  /**
   * Setup UI event listeners
   */
  setupEventListeners() {
    this.elements.connectBtn.addEventListener('click', () => this.connect());
    this.elements.disconnectBtn.addEventListener('click', () => this.disconnect());
    this.elements.fitSceneBtn.addEventListener('click', () => this.fitCameraToScene());

    // Allow Enter key to connect
    this.elements.sessionId.addEventListener('keypress', (e) => {
      if (e.key === 'Enter') this.connect();
    });
  }

  /**
   * Connect to bridge server
   */
  async connect() {
    const serverUrl = this.elements.serverUrl.value;
    const sessionId = this.elements.sessionId.value.trim();

    if (!sessionId) {
      this.log('Please enter a session ID', 'warning');
      return;
    }

    this.setConnectionStatus('connecting');
    this.log(`Connecting to ${serverUrl} (session: ${sessionId})...`, 'info');

    try {
      this.bridgeClient.serverUrl = serverUrl;
      await this.bridgeClient.connect(sessionId);
      this.elements.connectBtn.disabled = true;
      this.elements.disconnectBtn.disabled = false;
      this.log('Connected successfully', 'success');
    } catch (err) {
      this.log(`Connection failed: ${err.message}`, 'error');
      this.setConnectionStatus('disconnected');
    }
  }

  /**
   * Disconnect from bridge server
   */
  disconnect() {
    this.bridgeClient.disconnect();
    this.elements.connectBtn.disabled = false;
    this.elements.disconnectBtn.disabled = true;
    this.log('Disconnected', 'info');
  }

  /**
   * Handle scene upload from Blender
   */
  async handleSceneUpload(data) {
    this.log(`Receiving scene: ${data.scene?.name || 'unknown'} (${data.binaryData.length} bytes)`, 'info');
    this.showLoading('Loading scene...');

    try {
      // Clear previous scene
      this.clearScene();

      // Parse USD data
      const result = await this.parseUSD(data.binaryData);

      if (result) {
        this.sceneRoot = result;
        this.scene.add(this.sceneRoot);

        // Register objects with parameter sync
        this.registerSceneObjects(this.sceneRoot);

        // Fit camera to scene
        this.fitCameraToScene();

        // Update stats
        this.updateSceneStats();

        this.log('Scene loaded successfully', 'success');
        this.elements.sceneStatus.textContent = 'Loaded';
      }
    } catch (err) {
      this.log(`Failed to load scene: ${err.message}`, 'error');
      this.bridgeClient.sendError('PARSE_ERROR', err.message);
    } finally {
      this.hideLoading();
    }
  }

  /**
   * Parse USD binary data and build Three.js scene
   */
  async parseUSD(binaryData) {
    // Create blob URL for loader
    const blob = new Blob([binaryData], { type: 'model/vnd.usdz+zip' });
    const url = URL.createObjectURL(blob);

    try {
      // Load USD and get native loader
      const nativeLoader = await new Promise((resolve, reject) => {
        this.loader.load(
          url,
          (usd) => {
            console.log('loaded');
            resolve(usd);
          },
          (progress) => {
            if (progress.percentage !== undefined) {
              this.elements.loadingMessage.textContent =
                `Loading: ${Math.round(progress.percentage)}%`;
            }
          },
          (error) => {
            reject(error);
          }
        );
      });

      // Store native loader for parameter sync
      this.nativeLoader = nativeLoader;

      // Get USD root node
      const usdRootNode = nativeLoader.getDefaultRootNode();

      // Create default material
      const defaultMaterial = new THREE.MeshPhysicalMaterial({
        color: 0x808080,
        roughness: 0.5,
        metalness: 0.0
      });

      // Build Three.js scene from USD
      const options = {
        envMap: this.envMap,
        envMapIntensity: 1.0,
        preferredMaterialType: 'auto'
      };

      const root = await TinyUSDZLoaderUtils.buildThreeNode(
        usdRootNode,
        defaultMaterial,
        nativeLoader,
        options
      );

      return root;
    } finally {
      URL.revokeObjectURL(url);
    }
  }

  /**
   * Clear current scene content
   */
  clearScene() {
    if (this.sceneRoot) {
      this.scene.remove(this.sceneRoot);
      this.disposeObject(this.sceneRoot);
      this.sceneRoot = null;
    }
    this.parameterSync.clear();
  }

  /**
   * Recursively dispose Three.js objects
   */
  disposeObject(object) {
    object.traverse((child) => {
      if (child.geometry) {
        child.geometry.dispose();
      }
      if (child.material) {
        if (Array.isArray(child.material)) {
          child.material.forEach(m => this.disposeMaterial(m));
        } else {
          this.disposeMaterial(child.material);
        }
      }
    });
  }

  /**
   * Dispose material and its textures
   */
  disposeMaterial(material) {
    for (const key of Object.keys(material)) {
      const value = material[key];
      if (value && value.isTexture) {
        value.dispose();
      }
    }
    material.dispose();
  }

  /**
   * Register scene objects with parameter sync
   */
  registerSceneObjects(root) {
    root.traverse((object) => {
      // Get USD path from userData if available
      const path = object.userData?.usdPath || object.name;

      if (object.isMesh && object.material) {
        const materials = Array.isArray(object.material) ? object.material : [object.material];
        materials.forEach((mat, i) => {
          const matPath = mat.userData?.usdPath || `${path}/material_${i}`;
          this.parameterSync.registerMaterial(matPath, mat);
        });
      }

      if (object.isLight) {
        this.parameterSync.registerLight(path, object);
      }

      if (object.isCamera) {
        this.parameterSync.registerCamera(path, object);
      }

      // Register all objects for transforms
      this.parameterSync.registerObject(path, object);
    });
  }

  /**
   * Handle parameter update from Blender
   */
  handleParameterUpdate(data) {
    const { target, changes } = data;
    this.log(`Parameter update: ${target.type} ${target.path}`, 'info');

    // Handle Blender viewport camera directly
    if (target.type === 'camera' && target.path === '/BlenderViewport') {
      this.applyBlenderCamera(changes);
      return;
    }

    const applied = this.parameterSync.applyUpdate(data);
    if (!applied) {
      this.log(`Failed to apply update to ${target.path}`, 'warning');
    }
  }

  /**
   * Apply Blender viewport camera to Three.js camera
   */
  applyBlenderCamera(changes) {
    if (changes.position) {
      // Blender uses Z-up, Three.js uses Y-up
      // Swap Y and Z for coordinate conversion
      this.camera.position.set(
        changes.position[0],
        changes.position[2],
        -changes.position[1]
      );
    }

    if (changes.target) {
      this.controls.target.set(
        changes.target[0],
        changes.target[2],
        -changes.target[1]
      );
    }

    if (changes.fov) {
      this.camera.fov = changes.fov;
      this.camera.updateProjectionMatrix();
    }

    this.controls.update();
    this.log('Applied Blender camera', 'success');
  }

  /**
   * Fit camera to scene bounding box
   */
  fitCameraToScene() {
    if (!this.sceneRoot) return;

    const box = new THREE.Box3().setFromObject(this.sceneRoot);
    const size = box.getSize(new THREE.Vector3());
    const center = box.getCenter(new THREE.Vector3());

    const maxDim = Math.max(size.x, size.y, size.z);
    const fov = this.camera.fov * (Math.PI / 180);
    const distance = (maxDim * CAMERA_PADDING) / (2 * Math.tan(fov / 2));

    this.camera.position.copy(center);
    this.camera.position.z += distance;
    this.camera.lookAt(center);

    this.controls.target.copy(center);
    this.controls.update();
  }

  /**
   * Update scene statistics display
   */
  updateSceneStats() {
    if (!this.sceneRoot) return;

    let meshCount = 0;
    let materialCount = 0;
    let lightCount = 0;
    const materials = new Set();

    this.sceneRoot.traverse((object) => {
      if (object.isMesh) {
        meshCount++;
        const mats = Array.isArray(object.material) ? object.material : [object.material];
        mats.forEach(m => materials.add(m));
      }
      if (object.isLight) lightCount++;
    });

    materialCount = materials.size;

    this.elements.meshCount.textContent = meshCount;
    this.elements.materialCount.textContent = materialCount;
    this.elements.lightCount.textContent = lightCount;
  }

  /**
   * Animation loop
   */
  animate() {
    requestAnimationFrame(() => this.animate());

    this.controls.update();
    this.renderer.render(this.scene, this.camera);

    // Update FPS
    this.frameCount++;
    const now = performance.now();
    if (now - this.lastFpsUpdate >= 1000) {
      this.fps = this.frameCount;
      this.frameCount = 0;
      this.lastFpsUpdate = now;
      this.elements.fpsValue.textContent = this.fps;
    }
  }

  /**
   * Handle window resize
   */
  onResize() {
    const width = window.innerWidth;
    const height = window.innerHeight;

    this.camera.aspect = width / height;
    this.camera.updateProjectionMatrix();
    this.renderer.setSize(width, height);
  }

  /**
   * Set connection status UI
   */
  setConnectionStatus(status) {
    const el = this.elements.connectionStatus;
    el.className = 'status ' + status;

    switch (status) {
      case 'connected':
        el.textContent = 'Connected';
        break;
      case 'connecting':
        el.textContent = 'Connecting...';
        break;
      case 'disconnected':
        el.textContent = 'Disconnected';
        break;
    }
  }

  /**
   * Show loading overlay
   */
  showLoading(message) {
    this.elements.loadingMessage.textContent = message;
    this.elements.loadingOverlay.classList.remove('hidden');
  }

  /**
   * Hide loading overlay
   */
  hideLoading() {
    this.elements.loadingOverlay.classList.add('hidden');
  }

  /**
   * Log message to UI
   */
  log(message, level = 'info') {
    console.log(`[${level.toUpperCase()}] ${message}`);

    const entry = document.createElement('div');
    entry.className = `log-entry ${level}`;
    entry.textContent = `[${new Date().toLocaleTimeString()}] ${message}`;
    this.elements.logContent.appendChild(entry);
    this.elements.logContent.scrollTop = this.elements.logContent.scrollHeight;

    // Keep only last 100 entries
    while (this.elements.logContent.children.length > 100) {
      this.elements.logContent.removeChild(this.elements.logContent.firstChild);
    }
  }
}

// ============================================================================
// Initialize
// ============================================================================

const viewer = new BlenderBridgeViewer();
viewer.init().catch(err => {
  console.error('Failed to initialize viewer:', err);
});
