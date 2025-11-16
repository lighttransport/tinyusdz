import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
// Gaussian Splatting renderer - using Three.js compatible implementation
// For production, integrate with @google/model-viewer or three-gpu-pathtracer GSplat support
// or antimatter15/splat

// TinyUSDZ loader
import createTinyUSDZModule from './src/tinyusdz/tinyusdz.js';

let scene, camera, renderer, controls;
let splatMeshes = [];
let stats = { fps: 0, frameCount: 0, lastTime: performance.now() };
let tinyusdz;

// Initialize Three.js scene
async function init() {
    // Scene setup
    scene = new THREE.Scene();
    scene.background = new THREE.Color(0x000000);

    // Camera
    camera = new THREE.PerspectiveCamera(
        75,
        window.innerWidth / window.innerHeight,
        0.1,
        1000
    );
    camera.position.set(0, 0, 5);

    // Renderer
    renderer = new THREE.WebGLRenderer({ antialias: true });
    renderer.setSize(window.innerWidth, window.innerHeight);
    renderer.setPixelRatio(window.devicePixelRatio);
    document.getElementById('canvas-container').appendChild(renderer.domElement);

    // Controls
    controls = new OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.05;
    controls.screenSpacePanning = false;
    controls.minDistance = 0.1;
    controls.maxDistance = 100;

    // Lighting
    const ambientLight = new THREE.AmbientLight(0x404040, 2);
    scene.add(ambientLight);

    const directionalLight = new THREE.DirectionalLight(0xffffff, 1);
    directionalLight.position.set(5, 5, 5);
    scene.add(directionalLight);

    // Grid helper
    const gridHelper = new THREE.GridHelper(10, 10, 0x444444, 0x222222);
    scene.add(gridHelper);

    // Window resize handler
    window.addEventListener('resize', onWindowResize, false);

    // Reset camera button
    document.getElementById('reset-camera').addEventListener('click', resetCamera);

    // File input handler
    document.getElementById('file-input').addEventListener('change', handleFileSelect);

    // Initialize TinyUSDZ WASM
    console.log('Initializing TinyUSDZ WASM...');
    try {
        tinyusdz = await createTinyUSDZModule();
        console.log('TinyUSDZ WASM initialized successfully');
        updateStatus('TinyUSDZ WASM loaded', 'success');
    } catch (err) {
        console.error('Failed to initialize TinyUSDZ:', err);
        updateStatus('Failed to load TinyUSDZ: ' + err.message, 'error');
    }

    // Start render loop
    animate();
}

function onWindowResize() {
    camera.aspect = window.innerWidth / window.innerHeight;
    camera.updateProjectionMatrix();
    renderer.setSize(window.innerWidth, window.innerHeight);
}

function resetCamera() {
    camera.position.set(0, 0, 5);
    controls.target.set(0, 0, 0);
    controls.update();
}

function updateStatus(message, type = 'info') {
    const statusDiv = document.createElement('div');
    statusDiv.className = `status ${type}`;
    statusDiv.textContent = message;

    const infoDiv = document.getElementById('splat-info');
    infoDiv.innerHTML = '';
    infoDiv.appendChild(statusDiv);
}

function showLoading(show) {
    const overlay = document.getElementById('loading-overlay');
    if (show) {
        overlay.classList.add('active');
    } else {
        overlay.classList.remove('active');
    }
}

function hideWelcome() {
    const welcome = document.getElementById('welcome');
    if (welcome) {
        welcome.style.display = 'none';
    }
}

async function handleFileSelect(event) {
    const file = event.target.files[0];
    if (!file) return;

    hideWelcome();
    showLoading(true);
    updateStatus(`Loading ${file.name}...`, 'info');

    try {
        // Read file as ArrayBuffer
        const arrayBuffer = await file.arrayBuffer();
        const uint8Array = new Uint8Array(arrayBuffer);

        console.log(`File loaded: ${file.name}, size: ${uint8Array.length} bytes`);

        // Determine file type
        const fileExt = file.name.toLowerCase().split('.').pop();
        console.log(`File type: ${fileExt}`);

        // Load file with TinyUSDZ (supports USD, SPZ, PLY)
        const result = await loadGSplatFile(uint8Array, file.name);

        if (result.success) {
            updateStatus(`✓ Loaded: ${result.splatCount} splats`, 'success');

            // Update info panel
            const infoHtml = `
                <div class="status success">✓ File: ${file.name}</div>
                <div class="status info">Splats: ${result.splatCount}</div>
                <div class="status info">Memory: ${(result.memoryUsage / 1024 / 1024).toFixed(2)} MB</div>
            `;
            document.getElementById('splat-info').innerHTML = infoHtml;

            // Update stats
            document.getElementById('splat-count').textContent = result.splatCount;
            document.getElementById('memory').textContent = (result.memoryUsage / 1024 / 1024).toFixed(2) + ' MB';
        } else {
            updateStatus(`✗ Error: ${result.error}`, 'error');
        }
    } catch (err) {
        console.error('Error loading file:', err);
        updateStatus(`✗ Error: ${err.message}`, 'error');
    } finally {
        showLoading(false);
    }
}

async function loadGSplatFile(uint8Array, filename) {
    if (!tinyusdz) {
        throw new Error('TinyUSDZ not initialized');
    }

    try {
        // Allocate memory in WASM
        const dataPtr = tinyusdz._malloc(uint8Array.length);
        tinyusdz.HEAPU8.set(uint8Array, dataPtr);

        // Load file (USD, SPZ, or PLY)
        // TinyUSDZ will detect the format based on magic number and file extension
        console.log('Parsing file...');
        const loadResult = tinyusdz.LoadUSDFromMemory(dataPtr, uint8Array.length, filename);

        // Free allocated memory
        tinyusdz._free(dataPtr);

        if (!loadResult || loadResult.error) {
            throw new Error(loadResult ? loadResult.error : 'Failed to load file');
        }

        console.log('File loaded, converting to RenderScene...');

        // Convert to RenderScene
        const scene = loadResult.GetRenderScene();
        if (!scene) {
            throw new Error('Failed to convert to RenderScene');
        }

        console.log('RenderScene obtained, checking for gsplats...');

        // Get Gaussian splat data
        const gsplats = scene.gsplats;
        if (!gsplats || gsplats.size() === 0) {
            throw new Error('No Gaussian splat data found in file. For USD: ensure GeomPoints has primvars:gsplat:* attributes. For SPZ/PLY: ensure valid format.');
        }

        console.log(`Found ${gsplats.size()} gsplat objects`);

        // Clear previous splats
        clearSplats();

        let totalSplatCount = 0;
        let totalMemory = 0;

        // Process each gsplat object
        for (let i = 0; i < gsplats.size(); i++) {
            const gsplat = gsplats.get(i);
            console.log(`Processing gsplat ${i}: ${gsplat.prim_name}`);

            const splatData = extractGSplatData(gsplat);
            totalSplatCount += splatData.count;
            totalMemory += splatData.memoryUsage;

            // Create and add splat mesh to scene
            const splatMesh = createGaussianSplatMesh(splatData);
            if (splatMesh) {
                scene.add(splatMesh);
                splatMeshes.push(splatMesh);
            }
        }

        // Fit camera to scene
        fitCameraToScene();

        return {
            success: true,
            splatCount: totalSplatCount,
            memoryUsage: totalMemory
        };

    } catch (err) {
        console.error('Error in loadGSplatFile:', err);
        return {
            success: false,
            error: err.message
        };
    }
}

function extractGSplatData(gsplat) {
    const positions = gsplat.positions;
    const scales = gsplat.scales;
    const rotations = gsplat.rotations;
    const alphas = gsplat.alphas;

    const count = positions.size();
    console.log(`Extracting ${count} splats...`);

    // Convert to arrays
    const posArray = new Float32Array(count * 3);
    const scaleArray = new Float32Array(count * 3);
    const rotArray = new Float32Array(count * 4);
    const alphaArray = new Float32Array(count);
    const colorArray = new Float32Array(count * 3);

    for (let i = 0; i < count; i++) {
        const pos = positions.get(i);
        posArray[i * 3 + 0] = pos[0];
        posArray[i * 3 + 1] = pos[1];
        posArray[i * 3 + 2] = pos[2];

        const scale = scales.get(i);
        scaleArray[i * 3 + 0] = scale[0];
        scaleArray[i * 3 + 1] = scale[1];
        scaleArray[i * 3 + 2] = scale[2];

        const rot = rotations.get(i);
        // Convert half to float and quath (imag, real) to (x, y, z, w)
        rotArray[i * 4 + 0] = rot.imag[0];
        rotArray[i * 4 + 1] = rot.imag[1];
        rotArray[i * 4 + 2] = rot.imag[2];
        rotArray[i * 4 + 3] = rot.real;

        alphaArray[i] = alphas.get(i);

        // Extract color from SH if available, otherwise use default
        if (gsplat.sh_l0 && gsplat.sh_l0.size() > 0) {
            const sh0 = gsplat.sh_l0.get(i);
            // SH DC component represents color
            colorArray[i * 3 + 0] = Math.max(0, Math.min(1, sh0[0]));
            colorArray[i * 3 + 1] = Math.max(0, Math.min(1, sh0[1]));
            colorArray[i * 3 + 2] = Math.max(0, Math.min(1, sh0[2]));
        } else {
            // Default white color
            colorArray[i * 3 + 0] = 1.0;
            colorArray[i * 3 + 1] = 1.0;
            colorArray[i * 3 + 2] = 1.0;
        }
    }

    const memoryUsage =
        posArray.byteLength +
        scaleArray.byteLength +
        rotArray.byteLength +
        alphaArray.byteLength +
        colorArray.byteLength;

    return {
        count,
        positions: posArray,
        scales: scaleArray,
        rotations: rotArray,
        alphas: alphaArray,
        colors: colorArray,
        memoryUsage
    };
}

function createGaussianSplatMesh(splatData) {
    // For now, render as point cloud until Spark/GSplat renderer is integrated
    // TODO: Integrate with proper Gaussian splatting renderer like:
    // - @google/model-viewer with splat support
    // - antimatter15/splat
    // - three-gpu-pathtracer GSplat

    console.log(`Creating point cloud for ${splatData.count} splats`);

    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute('position', new THREE.BufferAttribute(splatData.positions, 3));
    geometry.setAttribute('color', new THREE.BufferAttribute(splatData.colors, 3));

    // Use alpha for point size variation
    const sizes = new Float32Array(splatData.count);
    for (let i = 0; i < splatData.count; i++) {
        sizes[i] = splatData.alphas[i] * 10.0 + 1.0;
    }
    geometry.setAttribute('size', new THREE.BufferAttribute(sizes, 1));

    const material = new THREE.PointsMaterial({
        size: 0.05,
        vertexColors: true,
        transparent: true,
        opacity: 0.8,
        sizeAttenuation: true,
        blending: THREE.AdditiveBlending
    });

    const points = new THREE.Points(geometry, material);

    console.log('Point cloud created');
    return points;
}

function clearSplats() {
    splatMeshes.forEach(mesh => {
        if (mesh.geometry) mesh.geometry.dispose();
        if (mesh.material) mesh.material.dispose();
        scene.remove(mesh);
    });
    splatMeshes = [];
}

function fitCameraToScene() {
    if (splatMeshes.length === 0) return;

    const box = new THREE.Box3();
    splatMeshes.forEach(mesh => {
        box.expandByObject(mesh);
    });

    const size = box.getSize(new THREE.Vector3());
    const center = box.getCenter(new THREE.Vector3());

    const maxDim = Math.max(size.x, size.y, size.z);
    const fov = camera.fov * (Math.PI / 180);
    let cameraZ = Math.abs(maxDim / 2 / Math.tan(fov / 2));
    cameraZ *= 1.5; // Add some margin

    camera.position.set(center.x, center.y, center.z + cameraZ);
    controls.target.copy(center);
    controls.update();

    console.log(`Camera fitted to scene. BBox: ${size.x.toFixed(2)} x ${size.y.toFixed(2)} x ${size.z.toFixed(2)}`);
}

function updateStats() {
    stats.frameCount++;
    const now = performance.now();
    const elapsed = now - stats.lastTime;

    if (elapsed >= 1000) {
        stats.fps = Math.round((stats.frameCount * 1000) / elapsed);
        stats.frameCount = 0;
        stats.lastTime = now;

        document.getElementById('fps').textContent = stats.fps;
    }
}

function animate() {
    requestAnimationFrame(animate);

    controls.update();
    renderer.render(scene, camera);

    updateStats();
}

// Initialize on load
init().catch(err => {
    console.error('Initialization failed:', err);
    updateStatus('Failed to initialize: ' + err.message, 'error');
});
