// BRDF Visualizer
// Interactive 3D visualization of Bidirectional Reflectance Distribution Function

import * as THREE from 'three';

export class BRDFVisualizer {
    constructor(renderer) {
        this.renderer = renderer;
        this.enabled = false;

        // BRDF visualization parameters
        this.material = null;
        this.viewAngle = 0; // 0-90 degrees (0 = normal, 90 = grazing)
        this.lightAngle = 45; // 0-90 degrees
        this.resolution = 64;

        // Visualization objects
        this.brdfScene = null;
        this.brdfCamera = null;
        this.brdfMesh = null;
        this.canvas = null;
        this.texture = null;

        // Display overlay
        this.overlayDiv = null;
    }

    // Enable BRDF visualizer
    enable() {
        this.enabled = true;
        this.createOverlay();
    }

    // Disable BRDF visualizer
    disable() {
        this.enabled = false;
        this.removeOverlay();
    }

    // Set material to visualize
    setMaterial(material) {
        this.material = material;
        if (this.enabled) {
            this.updateVisualization();
        }
    }

    // Set view angle (0-90 degrees from normal)
    setViewAngle(angle) {
        this.viewAngle = Math.max(0, Math.min(90, angle));
        if (this.enabled) {
            this.updateVisualization();
        }
    }

    // Set light angle (0-90 degrees from normal)
    setLightAngle(angle) {
        this.lightAngle = Math.max(0, Math.min(90, angle));
        if (this.enabled) {
            this.updateVisualization();
        }
    }

    // Create overlay panel
    createOverlay() {
        this.overlayDiv = document.createElement('div');
        this.overlayDiv.id = 'brdf-visualizer-overlay';
        this.overlayDiv.style.cssText = `
            position: fixed;
            top: 10px;
            right: 10px;
            width: 320px;
            background: rgba(0, 0, 0, 0.9);
            border: 2px solid #4CAF50;
            border-radius: 8px;
            padding: 15px;
            color: #fff;
            font-family: 'Segoe UI', Arial, sans-serif;
            font-size: 13px;
            z-index: 10000;
        `;

        this.overlayDiv.innerHTML = `
            <h3 style="margin: 0 0 10px 0; color: #4CAF50;">BRDF Visualizer</h3>
            <canvas id="brdf-canvas" width="256" height="256" style="width: 100%; border: 1px solid #666;"></canvas>
            <div id="brdf-info" style="margin-top: 10px; font-size: 11px; color: #aaa;"></div>
        `;

        document.body.appendChild(this.overlayDiv);
        this.canvas = document.getElementById('brdf-canvas');

        this.updateVisualization();
    }

    // Remove overlay panel
    removeOverlay() {
        if (this.overlayDiv && this.overlayDiv.parentElement) {
            this.overlayDiv.parentElement.removeChild(this.overlayDiv);
        }
        this.overlayDiv = null;
        this.canvas = null;
    }

    // Update BRDF visualization
    updateVisualization() {
        if (!this.canvas || !this.material) return;

        // Create BRDF scene
        if (!this.brdfScene) {
            this.brdfScene = new THREE.Scene();
            this.brdfCamera = new THREE.OrthographicCamera(-1, 1, 1, -1, 0.1, 10);
            this.brdfCamera.position.set(0, 0, 1);
            this.brdfCamera.lookAt(0, 0, 0);
        }

        // Generate BRDF lobe visualization
        this.generateBRDFLobe();

        // Render to canvas
        this.renderToCanvas();
    }

    // Generate BRDF lobe geometry
    generateBRDFLobe() {
        // Remove existing mesh
        if (this.brdfMesh) {
            this.brdfScene.remove(this.brdfMesh);
            this.brdfMesh.geometry.dispose();
            this.brdfMesh.material.dispose();
        }

        // Create geometry for BRDF lobe
        const geometry = new THREE.BufferGeometry();
        const vertices = [];
        const colors = [];

        const segments = this.resolution;
        const thetaSegments = segments;
        const phiSegments = segments * 2;

        // Convert angles to radians
        const viewTheta = this.viewAngle * Math.PI / 180;
        const lightTheta = this.lightAngle * Math.PI / 180;

        // View direction (incident)
        const viewDir = new THREE.Vector3(
            Math.sin(viewTheta),
            0,
            Math.cos(viewTheta)
        );

        // Light direction
        const lightDir = new THREE.Vector3(
            Math.sin(lightTheta),
            0,
            Math.cos(lightTheta)
        );

        // Generate BRDF lobe points
        const maxRadius = 0.8;

        for (let t = 0; t <= thetaSegments; t++) {
            const theta = (t / thetaSegments) * Math.PI / 2; // 0 to 90 degrees

            for (let p = 0; p <= phiSegments; p++) {
                const phi = (p / phiSegments) * Math.PI * 2; // 0 to 360 degrees

                // Outgoing direction (reflection)
                const outDir = new THREE.Vector3(
                    Math.sin(theta) * Math.cos(phi),
                    Math.sin(theta) * Math.sin(phi),
                    Math.cos(theta)
                );

                // Calculate BRDF value (simplified approximation)
                const brdfValue = this.evaluateBRDF(viewDir, lightDir, outDir);

                // Scale by BRDF value
                const radius = brdfValue * maxRadius;

                // Position
                const x = outDir.x * radius;
                const y = outDir.y * radius;
                const z = outDir.z * radius;

                vertices.push(x, y, z);

                // Color based on BRDF value (heatmap)
                const color = this.valueToColor(brdfValue);
                colors.push(color.r, color.g, color.b);
            }
        }

        // Create indices for triangles
        const indices = [];
        for (let t = 0; t < thetaSegments; t++) {
            for (let p = 0; p < phiSegments; p++) {
                const i0 = t * (phiSegments + 1) + p;
                const i1 = i0 + 1;
                const i2 = i0 + (phiSegments + 1);
                const i3 = i2 + 1;

                indices.push(i0, i2, i1);
                indices.push(i1, i2, i3);
            }
        }

        geometry.setAttribute('position', new THREE.Float32BufferAttribute(vertices, 3));
        geometry.setAttribute('color', new THREE.Float32BufferAttribute(colors, 3));
        geometry.setIndex(indices);
        geometry.computeVertexNormals();

        const material = new THREE.MeshBasicMaterial({
            vertexColors: true,
            side: THREE.DoubleSide,
            wireframe: false
        });

        this.brdfMesh = new THREE.Mesh(geometry, material);
        this.brdfScene.add(this.brdfMesh);

        // Update info display
        this.updateInfoDisplay();
    }

    // Simplified BRDF evaluation (GGX microfacet model approximation)
    evaluateBRDF(viewDir, lightDir, outDir) {
        if (!this.material) return 0;

        const normal = new THREE.Vector3(0, 0, 1);

        // Calculate half vector
        const halfVec = new THREE.Vector3()
            .addVectors(lightDir, outDir)
            .normalize();

        const NdotH = Math.max(0, normal.dot(halfVec));
        const NdotV = Math.max(0, normal.dot(viewDir));
        const NdotL = Math.max(0, normal.dot(lightDir));
        const VdotH = Math.max(0, viewDir.dot(halfVec));

        if (NdotL <= 0 || NdotV <= 0) return 0;

        // Get material parameters
        const roughness = this.material.roughness !== undefined ? this.material.roughness : 0.5;
        const metalness = this.material.metalness !== undefined ? this.material.metalness : 0.0;

        // GGX normal distribution
        const alpha = roughness * roughness;
        const alphaSq = alpha * alpha;
        const denom = NdotH * NdotH * (alphaSq - 1) + 1;
        const D = alphaSq / (Math.PI * denom * denom);

        // Simplified geometry term
        const k = (roughness + 1) * (roughness + 1) / 8;
        const G1V = NdotV / (NdotV * (1 - k) + k);
        const G1L = NdotL / (NdotL * (1 - k) + k);
        const G = G1V * G1L;

        // Fresnel (Schlick approximation)
        const F0 = metalness * 0.04 + (1 - metalness) * 0.04;
        const F = F0 + (1 - F0) * Math.pow(1 - VdotH, 5);

        // Specular BRDF
        const specular = (D * G * F) / (4 * NdotV * NdotL + 0.001);

        // Diffuse component (Lambertian)
        const diffuse = (1 - metalness) * (1 - F) / Math.PI;

        return specular + diffuse;
    }

    // Convert BRDF value to heatmap color
    valueToColor(value) {
        // Normalize value (typically 0-2 for PBR)
        const t = Math.min(1, value / 2);

        const color = new THREE.Color();

        if (t < 0.25) {
            // Black to blue
            color.setRGB(0, 0, t * 4);
        } else if (t < 0.5) {
            // Blue to cyan
            const s = (t - 0.25) * 4;
            color.setRGB(0, s, 1);
        } else if (t < 0.75) {
            // Cyan to yellow
            const s = (t - 0.5) * 4;
            color.setRGB(s, 1, 1 - s);
        } else {
            // Yellow to red
            const s = (t - 0.75) * 4;
            color.setRGB(1, 1 - s, 0);
        }

        return color;
    }

    // Render BRDF to canvas
    renderToCanvas() {
        if (!this.canvas || !this.brdfScene) return;

        const width = this.canvas.width;
        const height = this.canvas.height;

        // Save original render target
        const originalTarget = this.renderer.getRenderTarget();
        const originalSize = this.renderer.getSize(new THREE.Vector2());

        // Set canvas size
        this.renderer.setSize(width, height, false);

        // Render BRDF scene
        this.renderer.setRenderTarget(null);
        this.renderer.render(this.brdfScene, this.brdfCamera);

        // Copy to canvas
        const ctx = this.canvas.getContext('2d');
        const glCanvas = this.renderer.domElement;
        ctx.drawImage(glCanvas, 0, 0, width, height);

        // Restore original render target and size
        this.renderer.setRenderTarget(originalTarget);
        this.renderer.setSize(originalSize.x, originalSize.y, false);
    }

    // Update info display
    updateInfoDisplay() {
        const infoDiv = document.getElementById('brdf-info');
        if (!infoDiv || !this.material) return;

        const roughness = this.material.roughness !== undefined ? this.material.roughness : 0.5;
        const metalness = this.material.metalness !== undefined ? this.material.metalness : 0.0;

        infoDiv.innerHTML = `
            <strong>Material Properties:</strong><br>
            Roughness: ${roughness.toFixed(3)}<br>
            Metalness: ${metalness.toFixed(3)}<br>
            <br>
            <strong>Visualization:</strong><br>
            View Angle: ${this.viewAngle.toFixed(1)}°<br>
            Light Angle: ${this.lightAngle.toFixed(1)}°<br>
            Resolution: ${this.resolution}×${this.resolution * 2}
        `;
    }

    // Generate analysis report
    generateReport() {
        if (!this.material) {
            return '# BRDF Analysis\n\n**Status**: No material selected.\n';
        }

        const roughness = this.material.roughness !== undefined ? this.material.roughness : 0.5;
        const metalness = this.material.metalness !== undefined ? this.material.metalness : 0.0;

        let report = '# BRDF Visualization Report\n\n';
        report += '## Material Properties\n\n';
        report += `**Roughness**: ${roughness.toFixed(3)}\n`;
        report += `**Metalness**: ${metalness.toFixed(3)}\n`;
        report += `**Material Type**: ${this.material.type}\n\n`;

        report += '## BRDF Characteristics\n\n';

        if (metalness > 0.9) {
            report += '**Material Type**: Metal\n';
            report += '- Specular reflections only (no diffuse)\n';
            report += '- Colored reflections based on base color\n';
            report += '- F0 determined by base color\n\n';
        } else if (metalness < 0.1) {
            report += '**Material Type**: Dielectric (Non-metal)\n';
            report += '- Both diffuse and specular reflection\n';
            report += '- Achromatic (white) specular\n';
            report += '- F0 ≈ 0.04 (4% reflectance at normal incidence)\n\n';
        } else {
            report += '**Material Type**: Mixed (Physically Incorrect)\n';
            report += '⚠️ Metalness should be binary: 0.0 or 1.0\n\n';
        }

        if (roughness < 0.1) {
            report += '**Surface**: Very smooth / glossy\n';
            report += '- Sharp, mirror-like reflections\n';
            report += '- Tight specular lobe\n';
        } else if (roughness < 0.5) {
            report += '**Surface**: Smooth to medium\n';
            report += '- Clear but slightly blurred reflections\n';
            report += '- Moderate specular lobe width\n';
        } else if (roughness < 0.8) {
            report += '**Surface**: Rough\n';
            report += '- Blurry reflections\n';
            report += '- Wide specular lobe\n';
        } else {
            report += '**Surface**: Very rough / matte\n';
            report += '- Diffuse-like appearance\n';
            report += '- Very wide specular lobe\n';
        }

        report += '\n## Visualization Settings\n\n';
        report += `**View Angle**: ${this.viewAngle.toFixed(1)}° from normal\n`;
        report += `**Light Angle**: ${this.lightAngle.toFixed(1)}° from normal\n`;
        report += `**Resolution**: ${this.resolution}×${this.resolution * 2}\n\n`;

        report += '## Interpretation\n\n';
        report += 'The 3D lobe shape represents how light reflects off the surface:\n';
        report += '- **Height/Radius**: Reflection intensity in that direction\n';
        report += '- **Width**: How spread out reflections are (roughness)\n';
        report += '- **Color**: Intensity (blue=low, red=high)\n';

        return report;
    }

    // Log analysis to console
    logAnalysis() {
        if (!this.material) {
            console.warn('No material selected for BRDF analysis');
            return;
        }

        const roughness = this.material.roughness !== undefined ? this.material.roughness : 0.5;
        const metalness = this.material.metalness !== undefined ? this.material.metalness : 0.0;

        console.group('📊 BRDF Analysis');
        console.log(`Material Type: ${this.material.type}`);
        console.log(`Roughness: ${roughness.toFixed(3)}`);
        console.log(`Metalness: ${metalness.toFixed(3)}`);
        console.log(`View Angle: ${this.viewAngle}°`);
        console.log(`Light Angle: ${this.lightAngle}°`);
        console.groupEnd();
    }
}

// Make class globally accessible
if (typeof window !== 'undefined') {
    window.BRDFVisualizer = BRDFVisualizer;
}
