// Material Gradient/Ramp Editor
// Create and apply custom color/value gradients for procedural materials

import * as THREE from 'three';

export class GradientRampEditor {
    constructor() {
        this.gradients = new Map(); // name -> gradient definition
        this.activeGradient = null;
        this.canvas = null;
        this.texture = null;

        // Load default gradients
        this.loadDefaultGradients();
    }

    // Load default gradient presets
    loadDefaultGradients() {
        // Color ramps
        this.addGradient('Grayscale', [
            { position: 0.0, color: [0, 0, 0] },
            { position: 1.0, color: [1, 1, 1] }
        ]);

        this.addGradient('Heatmap', [
            { position: 0.0, color: [0, 0, 0] },      // Black
            { position: 0.25, color: [0, 0, 1] },     // Blue
            { position: 0.5, color: [0, 1, 0] },      // Green
            { position: 0.75, color: [1, 1, 0] },     // Yellow
            { position: 1.0, color: [1, 0, 0] }       // Red
        ]);

        this.addGradient('Rainbow', [
            { position: 0.0, color: [1, 0, 0] },      // Red
            { position: 0.17, color: [1, 0.5, 0] },   // Orange
            { position: 0.33, color: [1, 1, 0] },     // Yellow
            { position: 0.5, color: [0, 1, 0] },      // Green
            { position: 0.67, color: [0, 0, 1] },     // Blue
            { position: 0.83, color: [0.29, 0, 0.51] }, // Indigo
            { position: 1.0, color: [0.56, 0, 1] }    // Violet
        ]);

        this.addGradient('Turbo', [
            { position: 0.0, color: [0.19, 0.07, 0.23] },
            { position: 0.25, color: [0.12, 0.57, 0.55] },
            { position: 0.5, color: [0.99, 0.72, 0.22] },
            { position: 0.75, color: [0.98, 0.27, 0.16] },
            { position: 1.0, color: [0.48, 0.01, 0.01] }
        ]);

        this.addGradient('Sunset', [
            { position: 0.0, color: [0.1, 0.05, 0.2] },  // Deep purple
            { position: 0.3, color: [0.8, 0.2, 0.3] },   // Red-pink
            { position: 0.6, color: [1.0, 0.5, 0.2] },   // Orange
            { position: 1.0, color: [1.0, 0.9, 0.4] }    // Yellow
        ]);

        this.addGradient('Ocean', [
            { position: 0.0, color: [0.0, 0.05, 0.15] }, // Deep blue
            { position: 0.5, color: [0.0, 0.4, 0.7] },   // Medium blue
            { position: 1.0, color: [0.3, 0.8, 0.9] }    // Cyan
        ]);

        this.addGradient('Forest', [
            { position: 0.0, color: [0.1, 0.2, 0.05] },  // Dark green
            { position: 0.5, color: [0.2, 0.5, 0.1] },   // Green
            { position: 1.0, color: [0.5, 0.8, 0.3] }    // Light green
        ]);

        this.addGradient('Metal', [
            { position: 0.0, color: [0.3, 0.3, 0.3] },
            { position: 0.5, color: [0.7, 0.7, 0.7] },
            { position: 1.0, color: [0.95, 0.95, 0.95] }
        ]);

        // Value ramps (for roughness, metalness, etc.)
        this.addGradient('Smooth to Rough', [
            { position: 0.0, color: [0, 0, 0] },
            { position: 1.0, color: [1, 1, 1] }
        ]);

        this.addGradient('Inverted', [
            { position: 0.0, color: [1, 1, 1] },
            { position: 1.0, color: [0, 0, 0] }
        ]);
    }

    // Add gradient definition
    addGradient(name, stops) {
        // Sort stops by position
        stops.sort((a, b) => a.position - b.position);

        this.gradients.set(name, {
            name: name,
            stops: stops
        });
    }

    // Get gradient by name
    getGradient(name) {
        return this.gradients.get(name);
    }

    // Get all gradient names
    getGradientNames() {
        return Array.from(this.gradients.keys());
    }

    // Evaluate gradient at position t (0.0 to 1.0)
    evaluateGradient(gradientName, t) {
        const gradient = this.gradients.get(gradientName);
        if (!gradient) return [0, 0, 0];

        t = Math.max(0, Math.min(1, t)); // Clamp to [0, 1]

        const stops = gradient.stops;

        // Find surrounding stops
        let before = stops[0];
        let after = stops[stops.length - 1];

        for (let i = 0; i < stops.length - 1; i++) {
            if (t >= stops[i].position && t <= stops[i + 1].position) {
                before = stops[i];
                after = stops[i + 1];
                break;
            }
        }

        // Handle edge cases
        if (t <= before.position) {
            return [...before.color];
        }
        if (t >= after.position) {
            return [...after.color];
        }

        // Linear interpolation
        const range = after.position - before.position;
        const localT = (t - before.position) / range;

        const r = before.color[0] + (after.color[0] - before.color[0]) * localT;
        const g = before.color[1] + (after.color[1] - before.color[1]) * localT;
        const b = before.color[2] + (after.color[2] - before.color[2]) * localT;

        return [r, g, b];
    }

    // Generate texture from gradient
    generateTexture(gradientName, width = 256, height = 1) {
        const gradient = this.gradients.get(gradientName);
        if (!gradient) {
            console.warn(`Gradient "${gradientName}" not found`);
            return null;
        }

        // Create canvas
        if (!this.canvas) {
            this.canvas = document.createElement('canvas');
        }
        this.canvas.width = width;
        this.canvas.height = height;

        const ctx = this.canvas.getContext('2d');

        // Create horizontal gradient
        const canvasGradient = ctx.createLinearGradient(0, 0, width, 0);

        gradient.stops.forEach(stop => {
            const r = Math.floor(stop.color[0] * 255);
            const g = Math.floor(stop.color[1] * 255);
            const b = Math.floor(stop.color[2] * 255);
            canvasGradient.addColorStop(stop.position, `rgb(${r}, ${g}, ${b})`);
        });

        ctx.fillStyle = canvasGradient;
        ctx.fillRect(0, 0, width, height);

        // Create Three.js texture
        this.texture = new THREE.CanvasTexture(this.canvas);
        this.texture.wrapS = THREE.ClampToEdgeWrapping;
        this.texture.wrapT = THREE.ClampToEdgeWrapping;
        this.texture.needsUpdate = true;

        this.activeGradient = gradientName;

        return this.texture;
    }

    // Apply gradient to material property
    applyToMaterial(material, property, gradientName, mapToUV = true) {
        const texture = this.generateTexture(gradientName);
        if (!texture) return false;

        switch (property) {
            case 'baseColor':
            case 'color':
                material.map = texture;
                break;
            case 'emissive':
                material.emissiveMap = texture;
                break;
            case 'roughness':
                material.roughnessMap = texture;
                break;
            case 'metalness':
                material.metalnessMap = texture;
                break;
            default:
                console.warn(`Property "${property}" not supported`);
                return false;
        }

        material.needsUpdate = true;
        return true;
    }

    // Create procedural material with gradient
    createProceduralMaterial(gradientName, options = {}) {
        const {
            property = 'baseColor',
            width = 256,
            height = 1,
            metalness = 0.0,
            roughness = 0.5,
            emissiveIntensity = 0.0
        } = options;

        const texture = this.generateTexture(gradientName, width, height);
        if (!texture) return null;

        const material = new THREE.MeshStandardMaterial({
            metalness: metalness,
            roughness: roughness,
            emissiveIntensity: emissiveIntensity
        });

        // Apply texture to specified property
        switch (property) {
            case 'baseColor':
            case 'color':
                material.map = texture;
                break;
            case 'emissive':
                material.emissiveMap = texture;
                material.emissive.setRGB(1, 1, 1);
                break;
            case 'roughness':
                material.roughnessMap = texture;
                break;
            case 'metalness':
                material.metalnessMap = texture;
                break;
        }

        return material;
    }

    // Preview gradient as data URL
    getGradientPreview(gradientName, width = 256, height = 32) {
        const gradient = this.gradients.get(gradientName);
        if (!gradient) return null;

        const canvas = document.createElement('canvas');
        canvas.width = width;
        canvas.height = height;
        const ctx = canvas.getContext('2d');

        const canvasGradient = ctx.createLinearGradient(0, 0, width, 0);
        gradient.stops.forEach(stop => {
            const r = Math.floor(stop.color[0] * 255);
            const g = Math.floor(stop.color[1] * 255);
            const b = Math.floor(stop.color[2] * 255);
            canvasGradient.addColorStop(stop.position, `rgb(${r}, ${g}, ${b})`);
        });

        ctx.fillStyle = canvasGradient;
        ctx.fillRect(0, 0, width, height);

        return canvas.toDataURL();
    }

    // Export gradient as JSON
    exportGradient(gradientName) {
        const gradient = this.gradients.get(gradientName);
        if (!gradient) return null;

        const json = JSON.stringify(gradient, null, 2);
        const blob = new Blob([json], { type: 'application/json' });
        const url = URL.createObjectURL(blob);

        const a = document.createElement('a');
        a.href = url;
        a.download = `gradient_${gradientName.replace(/\s+/g, '_')}.json`;
        a.click();

        URL.revokeObjectURL(url);
        return true;
    }

    // Import gradient from JSON
    importGradient(jsonString) {
        try {
            const gradient = JSON.parse(jsonString);
            if (!gradient.name || !gradient.stops) {
                throw new Error('Invalid gradient format');
            }

            this.addGradient(gradient.name, gradient.stops);
            return gradient.name;
        } catch (err) {
            console.error('Failed to import gradient:', err);
            return null;
        }
    }

    // Delete gradient
    deleteGradient(gradientName) {
        return this.gradients.delete(gradientName);
    }

    // Generate report
    generateReport() {
        let report = '# Material Gradient Library\n\n';
        report += `**Total Gradients**: ${this.gradients.size}\n\n`;

        this.gradients.forEach(gradient => {
            report += `## ${gradient.name}\n\n`;
            report += `**Stops**: ${gradient.stops.length}\n\n`;

            gradient.stops.forEach(stop => {
                const r = (stop.color[0] * 255).toFixed(0);
                const g = (stop.color[1] * 255).toFixed(0);
                const b = (stop.color[2] * 255).toFixed(0);
                report += `- **Position ${stop.position.toFixed(2)}**: RGB(${r}, ${g}, ${b})\n`;
            });

            report += '\n';
        });

        report += '## Usage\n\n';
        report += 'Gradients can be applied to:\n';
        report += '- Base Color (Albedo)\n';
        report += '- Emissive Color\n';
        report += '- Roughness Maps\n';
        report += '- Metalness Maps\n';

        return report;
    }

    // Log gradients to console
    logGradients() {
        console.group('📊 Material Gradient Library');
        console.log(`Total Gradients: ${this.gradients.size}`);

        this.gradients.forEach(gradient => {
            console.group(`Gradient: ${gradient.name}`);
            console.log(`Stops: ${gradient.stops.length}`);
            gradient.stops.forEach(stop => {
                const r = (stop.color[0] * 255).toFixed(0);
                const g = (stop.color[1] * 255).toFixed(0);
                const b = (stop.color[2] * 255).toFixed(0);
                console.log(`  ${stop.position.toFixed(2)}: RGB(${r}, ${g}, ${b})`);
            });
            console.groupEnd();
        });

        console.groupEnd();
    }
}

// Make class globally accessible
if (typeof window !== 'undefined') {
    window.GradientRampEditor = GradientRampEditor;
}
