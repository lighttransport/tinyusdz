// Pixel Inspector (Magnifying Glass)
// Examine individual pixels in detail with RGB values and material properties

import * as THREE from 'three';

export class PixelInspector {
    constructor(renderer, scene, camera, domElement) {
        this.renderer = renderer;
        this.scene = scene;
        this.camera = camera;
        this.domElement = domElement;

        this.enabled = false;
        this.zoomLevel = 10; // 10x magnification
        this.gridSize = 5; // 5x5 pixel grid

        // Mouse position
        this.mouseX = 0;
        this.mouseY = 0;

        // Pixel data
        this.pixelData = null;
        this.centerPixel = null;

        // UI elements
        this.inspectorPanel = null;
        this.canvas = null;
        this.ctx = null;

        // Raycaster for picking
        this.raycaster = new Raycaster();
        this.mouse = new THREE.Vector2();

        // Bind event handlers
        this.onMouseMove = this.handleMouseMove.bind(this);
        this.onMouseClick = this.handleMouseClick.bind(this);
    }

    // Enable pixel inspector
    enable() {
        this.enabled = true;
        this.createUI();
        this.domElement.addEventListener('mousemove', this.onMouseMove);
        this.domElement.addEventListener('click', this.onMouseClick);
        this.domElement.style.cursor = 'crosshair';
    }

    // Disable pixel inspector
    disable() {
        this.enabled = false;
        this.destroyUI();
        this.domElement.removeEventListener('mousemove', this.onMouseMove);
        this.domElement.removeEventListener('click', this.onMouseClick);
        this.domElement.style.cursor = 'default';
    }

    // Create UI panel
    createUI() {
        // Create panel
        this.inspectorPanel = document.createElement('div');
        this.inspectorPanel.id = 'pixel-inspector-panel';
        this.inspectorPanel.style.cssText = `
            position: absolute;
            top: 10px;
            right: 10px;
            width: 320px;
            background: rgba(0, 0, 0, 0.9);
            border: 2px solid #4CAF50;
            border-radius: 8px;
            padding: 15px;
            color: #fff;
            font-family: monospace;
            font-size: 12px;
            z-index: 1000;
            pointer-events: none;
        `;

        // Create canvas for magnified view
        this.canvas = document.createElement('canvas');
        this.canvas.width = 150;
        this.canvas.height = 150;
        this.canvas.style.cssText = `
            width: 150px;
            height: 150px;
            border: 1px solid #666;
            image-rendering: pixelated;
            margin-bottom: 10px;
        `;
        this.ctx = this.canvas.getContext('2d');

        // Create info container
        const infoContainer = document.createElement('div');
        infoContainer.id = 'pixel-info';
        infoContainer.innerHTML = '<p style="color: #888;">Hover over scene...</p>';

        this.inspectorPanel.appendChild(this.canvas);
        this.inspectorPanel.appendChild(infoContainer);

        // Add to DOM
        this.domElement.parentElement.appendChild(this.inspectorPanel);
    }

    // Destroy UI panel
    destroyUI() {
        if (this.inspectorPanel && this.inspectorPanel.parentElement) {
            this.inspectorPanel.parentElement.removeChild(this.inspectorPanel);
        }
        this.inspectorPanel = null;
        this.canvas = null;
        this.ctx = null;
    }

    // Handle mouse move
    handleMouseMove(event) {
        if (!this.enabled) return;

        const rect = this.domElement.getBoundingClientRect();
        this.mouseX = event.clientX - rect.left;
        this.mouseY = event.clientY - rect.top;

        // Update immediately
        this.updateInspector();
    }

    // Handle mouse click (lock inspection)
    handleMouseClick(event) {
        if (!this.enabled) return;
        // Currently just updates - could add "lock" feature later
        this.updateInspector();
    }

    // Update inspector display
    updateInspector() {
        // Read pixels around mouse position
        const halfGrid = Math.floor(this.gridSize / 2);
        const startX = Math.floor(this.mouseX) - halfGrid;
        const startY = Math.floor(this.mouseY) - halfGrid;

        // Read pixel data from framebuffer
        const pixelBuffer = new Uint8Array(this.gridSize * this.gridSize * 4);

        try {
            this.renderer.readRenderTargetPixels(
                this.renderer.getRenderTarget() || this.renderer,
                startX,
                this.renderer.domElement.height - startY - this.gridSize,
                this.gridSize,
                this.gridSize,
                pixelBuffer
            );
        } catch (e) {
            // Fallback: read from canvas if render target fails
            const gl = this.renderer.getContext();
            gl.readPixels(
                startX,
                this.renderer.domElement.height - startY - this.gridSize,
                this.gridSize,
                this.gridSize,
                gl.RGBA,
                gl.UNSIGNED_BYTE,
                pixelBuffer
            );
        }

        this.pixelData = pixelBuffer;

        // Get center pixel
        const centerIdx = (Math.floor(this.gridSize / 2) * this.gridSize + Math.floor(this.gridSize / 2)) * 4;
        this.centerPixel = {
            r: pixelBuffer[centerIdx],
            g: pixelBuffer[centerIdx + 1],
            b: pixelBuffer[centerIdx + 2],
            a: pixelBuffer[centerIdx + 3]
        };

        // Raycast to get 3D info
        this.mouse.x = (this.mouseX / this.domElement.width) * 2 - 1;
        this.mouse.y = -(this.mouseY / this.domElement.height) * 2 + 1;

        this.raycaster.setFromCamera(this.mouse, this.camera);
        const intersects = this.raycaster.intersectObjects(this.scene.children, true);

        let materialInfo = null;
        if (intersects.length > 0) {
            const hit = intersects[0];
            if (hit.object.material) {
                materialInfo = this.extractMaterialInfo(hit.object.material, hit.uv);
            }
        }

        // Update UI
        this.renderMagnifiedView();
        this.updateInfoPanel(materialInfo);
    }

    // Extract material information
    extractMaterialInfo(material, uv) {
        const info = {
            name: material.name || 'Unnamed',
            type: material.type || 'Unknown',
            uv: uv || new THREE.Vector2(0, 0)
        };

        // PBR properties
        if (material.color) {
            info.baseColor = [material.color.r, material.color.g, material.color.b];
        }
        if (material.metalness !== undefined) {
            info.metalness = material.metalness;
        }
        if (material.roughness !== undefined) {
            info.roughness = material.roughness;
        }
        if (material.emissive) {
            info.emissive = [material.emissive.r, material.emissive.g, material.emissive.b];
        }
        if (material.emissiveIntensity !== undefined) {
            info.emissiveIntensity = material.emissiveIntensity;
        }

        return info;
    }

    // Render magnified pixel view
    renderMagnifiedView() {
        if (!this.ctx || !this.pixelData) return;

        const pixelSize = this.canvas.width / this.gridSize;

        // Clear canvas
        this.ctx.fillStyle = '#000';
        this.ctx.fillRect(0, 0, this.canvas.width, this.canvas.height);

        // Draw pixels
        for (let y = 0; y < this.gridSize; y++) {
            for (let x = 0; x < this.gridSize; x++) {
                const idx = ((this.gridSize - 1 - y) * this.gridSize + x) * 4; // Flip Y
                const r = this.pixelData[idx];
                const g = this.pixelData[idx + 1];
                const b = this.pixelData[idx + 2];

                this.ctx.fillStyle = `rgb(${r}, ${g}, ${b})`;
                this.ctx.fillRect(x * pixelSize, y * pixelSize, pixelSize, pixelSize);
            }
        }

        // Draw grid lines
        this.ctx.strokeStyle = 'rgba(255, 255, 255, 0.3)';
        this.ctx.lineWidth = 1;
        for (let i = 0; i <= this.gridSize; i++) {
            this.ctx.beginPath();
            this.ctx.moveTo(i * pixelSize, 0);
            this.ctx.lineTo(i * pixelSize, this.canvas.height);
            this.ctx.stroke();

            this.ctx.beginPath();
            this.ctx.moveTo(0, i * pixelSize);
            this.ctx.lineTo(this.canvas.width, i * pixelSize);
            this.ctx.stroke();
        }

        // Highlight center pixel
        const centerX = Math.floor(this.gridSize / 2);
        const centerY = Math.floor(this.gridSize / 2);
        this.ctx.strokeStyle = '#4CAF50';
        this.ctx.lineWidth = 2;
        this.ctx.strokeRect(centerX * pixelSize, centerY * pixelSize, pixelSize, pixelSize);
    }

    // Update info panel
    updateInfoPanel(materialInfo) {
        const infoDiv = document.getElementById('pixel-info');
        if (!infoDiv || !this.centerPixel) return;

        let html = '<div style="line-height: 1.6;">';

        // Position
        html += `<p><strong>Position:</strong> (${Math.floor(this.mouseX)}, ${Math.floor(this.mouseY)})</p>`;

        // Center pixel RGB
        html += '<p><strong>Center Pixel RGB:</strong></p>';
        html += `<p style="margin-left: 10px;">`;
        html += `R: ${this.centerPixel.r} <span style="color: #ff6b6b;">●</span><br>`;
        html += `G: ${this.centerPixel.g} <span style="color: #51cf66;">●</span><br>`;
        html += `B: ${this.centerPixel.b} <span style="color: #339af0;">●</span><br>`;
        html += `</p>`;

        // Normalized values
        const nr = (this.centerPixel.r / 255).toFixed(3);
        const ng = (this.centerPixel.g / 255).toFixed(3);
        const nb = (this.centerPixel.b / 255).toFixed(3);
        html += `<p><strong>Normalized:</strong> (${nr}, ${ng}, ${nb})</p>`;

        // Hex color
        const hex = '#' +
            this.centerPixel.r.toString(16).padStart(2, '0') +
            this.centerPixel.g.toString(16).padStart(2, '0') +
            this.centerPixel.b.toString(16).padStart(2, '0');
        html += `<p><strong>Hex:</strong> ${hex.toUpperCase()}</p>`;

        // Material info if available
        if (materialInfo) {
            html += '<hr style="border-color: #444; margin: 10px 0;">';
            html += `<p><strong>Material:</strong> ${materialInfo.name}</p>`;
            html += `<p><strong>Type:</strong> ${materialInfo.type}</p>`;

            if (materialInfo.uv) {
                html += `<p><strong>UV:</strong> (${materialInfo.uv.x.toFixed(3)}, ${materialInfo.uv.y.toFixed(3)})</p>`;
            }

            if (materialInfo.baseColor) {
                const bc = materialInfo.baseColor;
                html += `<p><strong>Base Color:</strong> (${bc[0].toFixed(3)}, ${bc[1].toFixed(3)}, ${bc[2].toFixed(3)})</p>`;
            }

            if (materialInfo.metalness !== undefined) {
                html += `<p><strong>Metalness:</strong> ${materialInfo.metalness.toFixed(2)}</p>`;
            }

            if (materialInfo.roughness !== undefined) {
                html += `<p><strong>Roughness:</strong> ${materialInfo.roughness.toFixed(2)}</p>`;
            }
        }

        html += '</div>';
        infoDiv.innerHTML = html;
    }

    // Set zoom level
    setZoomLevel(level) {
        this.zoomLevel = level;
    }

    // Set grid size
    setGridSize(size) {
        this.gridSize = size;
        if (this.canvas) {
            // Recreate canvas with new size
            const parent = this.canvas.parentElement;
            parent.removeChild(this.canvas);

            this.canvas = document.createElement('canvas');
            this.canvas.width = 150;
            this.canvas.height = 150;
            this.canvas.style.cssText = `
                width: 150px;
                height: 150px;
                border: 1px solid #666;
                image-rendering: pixelated;
                margin-bottom: 10px;
            `;
            this.ctx = this.canvas.getContext('2d');
            parent.insertBefore(this.canvas, parent.firstChild);
        }
    }

    // Get state
    getState() {
        return {
            enabled: this.enabled,
            zoomLevel: this.zoomLevel,
            gridSize: this.gridSize,
            centerPixel: this.centerPixel
        };
    }
}

// Make class globally accessible
if (typeof window !== 'undefined') {
    window.PixelInspector = PixelInspector;
}
