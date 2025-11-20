// Real-Time G-Buffer Viewer
// Display all AOV channels simultaneously in a grid layout for comprehensive material debugging

import * as THREE from 'three';

export class GBufferViewer {
    constructor(renderer, scene, camera) {
        this.renderer = renderer;
        this.scene = scene;
        this.camera = camera;
        this.enabled = false;
        this.gridLayout = '3x3'; // '2x2', '3x3', '4x4'

        // Render targets for each AOV channel
        this.renderTargets = new Map();

        // Canvas overlays for each channel
        this.overlayCanvas = null;
        this.overlayCtx = null;

        // Channel configurations
        this.channels = [
            { name: 'Final Render', aov: null, enabled: true },
            { name: 'Albedo', aov: 'albedo', enabled: true },
            { name: 'Normal', aov: 'normal', enabled: true },
            { name: 'Depth', aov: 'depth', enabled: true },
            { name: 'Metalness', aov: 'metalness', enabled: true },
            { name: 'Roughness', aov: 'roughness', enabled: true },
            { name: 'Emissive', aov: 'emissive', enabled: true },
            { name: 'AO', aov: 'ambient_occlusion', enabled: true },
            { name: 'UV', aov: 'uv', enabled: true }
        ];
    }

    // Enable G-Buffer viewer
    enable() {
        this.enabled = true;
        this.createRenderTargets();
        this.createOverlayCanvas();
    }

    // Disable G-Buffer viewer
    disable() {
        this.enabled = false;
        this.destroyRenderTargets();
        this.destroyOverlayCanvas();
    }

    // Create render targets for each AOV channel
    createRenderTargets() {
        const width = this.renderer.domElement.width;
        const height = this.renderer.domElement.height;

        this.channels.forEach(channel => {
            if (channel.enabled) {
                const rt = new THREE.WebGLRenderTarget(width, height, {
                    minFilter: THREE.LinearFilter,
                    magFilter: THREE.LinearFilter,
                    format: THREE.RGBAFormat,
                    type: THREE.UnsignedByteType
                });
                this.renderTargets.set(channel.name, rt);
            }
        });
    }

    // Destroy render targets
    destroyRenderTargets() {
        this.renderTargets.forEach(rt => {
            rt.dispose();
        });
        this.renderTargets.clear();
    }

    // Create overlay canvas for displaying grid
    createOverlayCanvas() {
        this.overlayCanvas = document.createElement('canvas');
        this.overlayCanvas.id = 'gbuffer-overlay';
        this.overlayCanvas.style.position = 'absolute';
        this.overlayCanvas.style.top = '0';
        this.overlayCanvas.style.left = '0';
        this.overlayCanvas.style.pointerEvents = 'none';
        this.overlayCanvas.style.zIndex = '10';

        const container = this.renderer.domElement.parentElement;
        container.style.position = 'relative';
        container.appendChild(this.overlayCanvas);

        this.overlayCtx = this.overlayCanvas.getContext('2d');
        this.resizeOverlay();
    }

    // Destroy overlay canvas
    destroyOverlayCanvas() {
        if (this.overlayCanvas && this.overlayCanvas.parentElement) {
            this.overlayCanvas.parentElement.removeChild(this.overlayCanvas);
        }
        this.overlayCanvas = null;
        this.overlayCtx = null;
    }

    // Resize overlay to match renderer
    resizeOverlay() {
        if (this.overlayCanvas) {
            this.overlayCanvas.width = this.renderer.domElement.width;
            this.overlayCanvas.height = this.renderer.domElement.height;
            this.overlayCanvas.style.width = this.renderer.domElement.style.width;
            this.overlayCanvas.style.height = this.renderer.domElement.style.height;
        }
    }

    // Set grid layout
    setGridLayout(layout) {
        this.gridLayout = layout;
    }

    // Render all AOV channels in grid
    render() {
        if (!this.enabled) return;

        const width = this.renderer.domElement.width;
        const height = this.renderer.domElement.height;

        // Parse grid layout
        const [cols, rows] = this.gridLayout.split('x').map(Number);
        const cellWidth = Math.floor(width / cols);
        const cellHeight = Math.floor(height / rows);

        // Store original render target
        const originalRenderTarget = this.renderer.getRenderTarget();
        const originalScissorTest = this.renderer.getScissorTest();

        // Enable scissor test
        this.renderer.setScissorTest(true);

        // Store original materials
        const originalMaterials = new Map();
        this.scene.traverse(obj => {
            if (obj.isMesh && obj.material) {
                originalMaterials.set(obj.uuid, obj.material);
            }
        });

        let channelIndex = 0;
        const enabledChannels = this.channels.filter(ch => ch.enabled);

        for (let row = 0; row < rows; row++) {
            for (let col = 0; col < cols; col++) {
                if (channelIndex >= enabledChannels.length) break;

                const channel = enabledChannels[channelIndex];
                const x = col * cellWidth;
                const y = (rows - 1 - row) * cellHeight; // Flip Y for WebGL

                // Set viewport and scissor
                this.renderer.setViewport(x, y, cellWidth, cellHeight);
                this.renderer.setScissor(x, y, cellWidth, cellHeight);

                // Render channel
                if (channel.aov === null) {
                    // Final render - use original materials
                    this.renderer.render(this.scene, this.camera);
                } else {
                    // AOV render - apply AOV materials
                    this.applyAOVMaterials(channel.aov);
                    this.renderer.render(this.scene, this.camera);
                }

                channelIndex++;
            }
        }

        // Restore original materials
        this.scene.traverse(obj => {
            if (obj.isMesh && originalMaterials.has(obj.uuid)) {
                obj.material = originalMaterials.get(obj.uuid);
            }
        });

        // Restore renderer state
        this.renderer.setScissorTest(originalScissorTest);
        this.renderer.setRenderTarget(originalRenderTarget);
        this.renderer.setViewport(0, 0, width, height);

        // Draw grid overlay
        this.drawGridOverlay(cols, rows, cellWidth, cellHeight);
    }

    // Apply AOV materials to scene
    applyAOVMaterials(aovMode) {
        this.scene.traverse(obj => {
            if (obj.isMesh && obj.material) {
                const aovMaterial = this.createAOVMaterial(aovMode, obj.material);
                if (aovMaterial) {
                    obj.material = aovMaterial;
                }
            }
        });
    }

    // Create AOV material (simplified version - should use same logic as materialx.js)
    createAOVMaterial(aovMode, originalMaterial) {
        // This is a simplified version - in practice, you'd want to import
        // the createAOVMaterial function from materialx.js or share the logic

        let shader = null;

        switch (aovMode) {
            case 'albedo':
                shader = {
                    vertexShader: `
                        varying vec2 vUv;
                        void main() {
                            vUv = uv;
                            gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
                        }
                    `,
                    fragmentShader: `
                        uniform vec3 baseColor;
                        uniform sampler2D baseColorMap;
                        uniform bool hasBaseColorMap;
                        varying vec2 vUv;
                        void main() {
                            vec3 color = baseColor;
                            if (hasBaseColorMap) {
                                color *= texture2D(baseColorMap, vUv).rgb;
                            }
                            gl_FragColor = vec4(color, 1.0);
                        }
                    `,
                    uniforms: {
                        baseColor: { value: originalMaterial.color || new THREE.Color(1, 1, 1) },
                        baseColorMap: { value: originalMaterial.map || null },
                        hasBaseColorMap: { value: originalMaterial.map !== null }
                    }
                };
                break;

            case 'normal':
                shader = {
                    vertexShader: `
                        varying vec3 vNormal;
                        void main() {
                            vNormal = normalize(normalMatrix * normal);
                            gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
                        }
                    `,
                    fragmentShader: `
                        varying vec3 vNormal;
                        void main() {
                            vec3 n = normalize(vNormal);
                            gl_FragColor = vec4(n * 0.5 + 0.5, 1.0);
                        }
                    `
                };
                break;

            case 'depth':
                shader = {
                    vertexShader: `
                        varying float vDepth;
                        void main() {
                            vec4 mvPosition = modelViewMatrix * vec4(position, 1.0);
                            vDepth = -mvPosition.z;
                            gl_Position = projectionMatrix * mvPosition;
                        }
                    `,
                    fragmentShader: `
                        varying float vDepth;
                        uniform float cameraNear;
                        uniform float cameraFar;
                        void main() {
                            float depth = (vDepth - cameraNear) / (cameraFar - cameraNear);
                            gl_FragColor = vec4(vec3(depth), 1.0);
                        }
                    `,
                    uniforms: {
                        cameraNear: { value: this.camera.near },
                        cameraFar: { value: this.camera.far }
                    }
                };
                break;

            case 'metalness':
                shader = {
                    vertexShader: `
                        varying vec2 vUv;
                        void main() {
                            vUv = uv;
                            gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
                        }
                    `,
                    fragmentShader: `
                        uniform float metalness;
                        uniform sampler2D metalnessMap;
                        uniform bool hasMetalnessMap;
                        varying vec2 vUv;
                        void main() {
                            float m = metalness;
                            if (hasMetalnessMap) {
                                m *= texture2D(metalnessMap, vUv).b;
                            }
                            gl_FragColor = vec4(vec3(m), 1.0);
                        }
                    `,
                    uniforms: {
                        metalness: { value: originalMaterial.metalness !== undefined ? originalMaterial.metalness : 0.0 },
                        metalnessMap: { value: originalMaterial.metalnessMap || null },
                        hasMetalnessMap: { value: originalMaterial.metalnessMap !== null }
                    }
                };
                break;

            case 'roughness':
                shader = {
                    vertexShader: `
                        varying vec2 vUv;
                        void main() {
                            vUv = uv;
                            gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
                        }
                    `,
                    fragmentShader: `
                        uniform float roughness;
                        uniform sampler2D roughnessMap;
                        uniform bool hasRoughnessMap;
                        varying vec2 vUv;
                        void main() {
                            float r = roughness;
                            if (hasRoughnessMap) {
                                r *= texture2D(roughnessMap, vUv).g;
                            }
                            gl_FragColor = vec4(vec3(r), 1.0);
                        }
                    `,
                    uniforms: {
                        roughness: { value: originalMaterial.roughness !== undefined ? originalMaterial.roughness : 1.0 },
                        roughnessMap: { value: originalMaterial.roughnessMap || null },
                        hasRoughnessMap: { value: originalMaterial.roughnessMap !== null }
                    }
                };
                break;

            case 'uv':
                shader = {
                    vertexShader: `
                        varying vec2 vUv;
                        void main() {
                            vUv = uv;
                            gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
                        }
                    `,
                    fragmentShader: `
                        varying vec2 vUv;
                        void main() {
                            gl_FragColor = vec4(fract(vUv), 0.0, 1.0);
                        }
                    `
                };
                break;

            default:
                return null;
        }

        if (shader) {
            return new THREE.ShaderMaterial({
                vertexShader: shader.vertexShader,
                fragmentShader: shader.fragmentShader,
                uniforms: shader.uniforms || {}
            });
        }

        return null;
    }

    // Draw grid overlay with labels
    drawGridOverlay(cols, rows, cellWidth, cellHeight) {
        if (!this.overlayCtx) return;

        const ctx = this.overlayCtx;
        const width = this.overlayCanvas.width;
        const height = this.overlayCanvas.height;

        // Clear canvas
        ctx.clearRect(0, 0, width, height);

        // Draw grid lines
        ctx.strokeStyle = 'rgba(255, 255, 255, 0.3)';
        ctx.lineWidth = 2;

        // Vertical lines
        for (let col = 1; col < cols; col++) {
            const x = col * cellWidth;
            ctx.beginPath();
            ctx.moveTo(x, 0);
            ctx.lineTo(x, height);
            ctx.stroke();
        }

        // Horizontal lines
        for (let row = 1; row < rows; row++) {
            const y = row * cellHeight;
            ctx.beginPath();
            ctx.moveTo(0, y);
            ctx.lineTo(width, y);
            ctx.stroke();
        }

        // Draw labels
        ctx.font = 'bold 14px monospace';
        ctx.fillStyle = 'rgba(255, 255, 255, 0.9)';
        ctx.strokeStyle = 'rgba(0, 0, 0, 0.8)';
        ctx.lineWidth = 3;

        let channelIndex = 0;
        const enabledChannels = this.channels.filter(ch => ch.enabled);

        for (let row = 0; row < rows; row++) {
            for (let col = 0; col < cols; col++) {
                if (channelIndex >= enabledChannels.length) break;

                const channel = enabledChannels[channelIndex];
                const x = col * cellWidth + 10;
                const y = row * cellHeight + 25;

                // Draw text with outline
                ctx.strokeText(channel.name, x, y);
                ctx.fillText(channel.name, x, y);

                channelIndex++;
            }
        }
    }

    // Toggle channel visibility
    toggleChannel(channelName, enabled) {
        const channel = this.channels.find(ch => ch.name === channelName);
        if (channel) {
            channel.enabled = enabled;
        }
    }

    // Get enabled channels
    getEnabledChannels() {
        return this.channels.filter(ch => ch.enabled);
    }

    // Get state
    getState() {
        return {
            enabled: this.enabled,
            gridLayout: this.gridLayout,
            channels: this.channels.map(ch => ({
                name: ch.name,
                enabled: ch.enabled
            }))
        };
    }
}

// Make class globally accessible
if (typeof window !== 'undefined') {
    window.GBufferViewer = GBufferViewer;
}
