// Split View Comparison System
// Side-by-side comparison with horizontal, vertical, and diagonal split modes

import * as THREE from 'three';

export class SplitViewComparison {
    constructor(renderer, scene, camera) {
        this.renderer = renderer;
        this.primaryScene = scene;
        this.secondaryScene = null; // Clone or different scene
        this.camera = camera;

        this.splitMode = 'vertical'; // 'vertical', 'horizontal', 'diagonal'
        this.splitPosition = 0.5; // 0.0-1.0 (position of divider)
        this.active = false;

        // Divider line visualization
        this.showDivider = true;
        this.dividerColor = new THREE.Color(1, 1, 0); // Yellow divider
        this.dividerWidth = 2; // pixels

        // For diagonal split
        this.stencilEnabled = false;
    }

    // Enable split view with a cloned scene or different scene
    enable(secondaryScene = null) {
        if (!secondaryScene) {
            // Clone the primary scene
            this.secondaryScene = this.primaryScene.clone(true);
        } else {
            this.secondaryScene = secondaryScene;
        }

        this.active = true;
        console.log(`Split view enabled (${this.splitMode} mode)`);
    }

    // Disable split view
    disable() {
        this.active = false;
        this.secondaryScene = null;

        // Restore full viewport
        const width = this.renderer.domElement.width;
        const height = this.renderer.domElement.height;
        this.renderer.setViewport(0, 0, width, height);
        this.renderer.setScissor(0, 0, width, height);
        this.renderer.setScissorTest(false);

        console.log('Split view disabled');
    }

    // Set split mode
    setSplitMode(mode) {
        if (['vertical', 'horizontal', 'diagonal'].includes(mode)) {
            this.splitMode = mode;
            console.log(`Split mode: ${mode}`);
        } else {
            console.error(`Invalid split mode: ${mode}`);
        }
    }

    // Set split position (0.0-1.0)
    setSplitPosition(position) {
        this.splitPosition = Math.max(0.0, Math.min(1.0, position));
    }

    // Render split view
    render() {
        if (!this.active || !this.secondaryScene) {
            // Normal rendering
            this.renderer.render(this.primaryScene, this.camera);
            return;
        }

        const width = this.renderer.domElement.width;
        const height = this.renderer.domElement.height;

        if (this.splitMode === 'vertical') {
            this.renderVerticalSplit(width, height);
        } else if (this.splitMode === 'horizontal') {
            this.renderHorizontalSplit(width, height);
        } else if (this.splitMode === 'diagonal') {
            this.renderDiagonalSplit(width, height);
        }

        // Draw divider line
        if (this.showDivider) {
            this.drawDivider(width, height);
        }
    }

    // Vertical split (left/right)
    renderVerticalSplit(width, height) {
        const splitX = Math.floor(width * this.splitPosition);

        // Left side: primary scene
        this.renderer.setViewport(0, 0, splitX, height);
        this.renderer.setScissor(0, 0, splitX, height);
        this.renderer.setScissorTest(true);
        this.renderer.render(this.primaryScene, this.camera);

        // Right side: secondary scene
        this.renderer.setViewport(splitX, 0, width - splitX, height);
        this.renderer.setScissor(splitX, 0, width - splitX, height);
        this.renderer.render(this.secondaryScene, this.camera);

        this.renderer.setScissorTest(false);
    }

    // Horizontal split (top/bottom)
    renderHorizontalSplit(width, height) {
        const splitY = Math.floor(height * this.splitPosition);

        // Bottom: primary scene
        this.renderer.setViewport(0, 0, width, splitY);
        this.renderer.setScissor(0, 0, width, splitY);
        this.renderer.setScissorTest(true);
        this.renderer.render(this.primaryScene, this.camera);

        // Top: secondary scene
        this.renderer.setViewport(0, splitY, width, height - splitY);
        this.renderer.setScissor(0, splitY, width, height - splitY);
        this.renderer.render(this.secondaryScene, this.camera);

        this.renderer.setScissorTest(false);
    }

    // Diagonal split (top-left / bottom-right)
    renderDiagonalSplit(width, height) {
        // For diagonal split, we need to use stencil buffer or render to texture
        // This is a simplified version using multiple passes

        // Calculate diagonal line equation
        // Line from (0, height * (1 - splitPosition)) to (width, height * splitPosition)

        const gl = this.renderer.getContext();

        // Enable stencil test
        gl.enable(gl.STENCIL_TEST);

        // Clear stencil buffer
        gl.clearStencil(0);
        gl.clear(gl.STENCIL_BUFFER_BIT);

        // Write to stencil buffer (draw diagonal region)
        gl.stencilFunc(gl.ALWAYS, 1, 0xFF);
        gl.stencilOp(gl.KEEP, gl.KEEP, gl.REPLACE);
        gl.stencilMask(0xFF);
        gl.colorMask(false, false, false, false);
        gl.depthMask(false);

        // Draw diagonal triangle (top-left region)
        this.drawDiagonalStencil(width, height, true);

        // Render primary scene where stencil = 1
        gl.stencilFunc(gl.EQUAL, 1, 0xFF);
        gl.stencilMask(0x00);
        gl.colorMask(true, true, true, true);
        gl.depthMask(true);

        this.renderer.render(this.primaryScene, this.camera);

        // Render secondary scene where stencil = 0
        gl.stencilFunc(gl.EQUAL, 0, 0xFF);
        this.renderer.render(this.secondaryScene, this.camera);

        // Disable stencil test
        gl.disable(gl.STENCIL_TEST);
    }

    // Draw diagonal region to stencil buffer
    drawDiagonalStencil(width, height, topLeft) {
        // This requires drawing a triangle/quad to the stencil buffer
        // For simplicity, using a shader approach would be better
        // Here's a placeholder implementation

        const gl = this.renderer.getContext();

        // Create a simple shader to draw the diagonal mask
        // In practice, this would use a custom geometry or fullscreen quad
        // with a shader that tests against the diagonal line

        console.warn('Diagonal split stencil drawing not fully implemented');
    }

    // Draw divider line
    drawDivider(width, height) {
        const gl = this.renderer.getContext();

        // Save state
        gl.disable(gl.DEPTH_TEST);

        const lineWidth = this.dividerWidth;

        if (this.splitMode === 'vertical') {
            const x = Math.floor(width * this.splitPosition);

            // Draw vertical line using gl.lineWidth and gl.drawArrays
            // For WebGL, we'd need to create a simple line geometry
            // Simplified: just clear a thin strip
            gl.scissor(x - lineWidth/2, 0, lineWidth, height);
            gl.enable(gl.SCISSOR_TEST);
            gl.clearColor(this.dividerColor.r, this.dividerColor.g, this.dividerColor.b, 1);
            gl.clear(gl.COLOR_BUFFER_BIT);
            gl.disable(gl.SCISSOR_TEST);

        } else if (this.splitMode === 'horizontal') {
            const y = Math.floor(height * this.splitPosition);

            gl.scissor(0, y - lineWidth/2, width, lineWidth);
            gl.enable(gl.SCISSOR_TEST);
            gl.clearColor(this.dividerColor.r, this.dividerColor.g, this.dividerColor.b, 1);
            gl.clear(gl.COLOR_BUFFER_BIT);
            gl.disable(gl.SCISSOR_TEST);
        }

        // Restore state
        gl.enable(gl.DEPTH_TEST);
    }

    // Apply different material to secondary scene
    applyMaterialToSecondary(materialModifierFn) {
        if (!this.secondaryScene) {
            console.error('No secondary scene active');
            return;
        }

        this.secondaryScene.traverse(obj => {
            if (obj.isMesh && obj.material) {
                materialModifierFn(obj.material);
            }
        });
    }

    // Apply different AOV mode to secondary scene
    applyAOVToSecondary(aovMaterialCreator) {
        if (!this.secondaryScene) {
            console.error('No secondary scene active');
            return;
        }

        this.secondaryScene.traverse(obj => {
            if (obj.isMesh && obj.material) {
                const aovMaterial = aovMaterialCreator(obj.material);
                if (aovMaterial) {
                    obj.material = aovMaterial;
                }
            }
        });
    }

    // Handle mouse interaction for dragging divider
    handleMouseMove(event, canvas) {
        if (!this.active) return false;

        const rect = canvas.getBoundingClientRect();
        const x = event.clientX - rect.left;
        const y = event.clientY - rect.top;

        const width = rect.width;
        const height = rect.height;

        if (this.splitMode === 'vertical') {
            this.setSplitPosition(x / width);
        } else if (this.splitMode === 'horizontal') {
            this.setSplitPosition(1.0 - (y / height));
        }
        // Diagonal would require calculating distance from diagonal line

        return true; // Event handled
    }

    // Get current state
    getState() {
        return {
            active: this.active,
            mode: this.splitMode,
            position: this.splitPosition,
            showDivider: this.showDivider
        };
    }
}

// Comparison presets
export const COMPARISON_PRESETS = {
    // Compare final render vs. albedo
    FINAL_VS_ALBEDO: {
        name: 'Final vs Albedo',
        description: 'Compare final render with base albedo',
        secondaryAOV: 'albedo'
    },

    // Compare with vs. without normal maps
    WITH_VS_WITHOUT_NORMALS: {
        name: 'With vs Without Normals',
        description: 'Compare normal mapping effect',
        secondaryModifier: (material) => {
            material.normalMap = null;
            material.needsUpdate = true;
        }
    },

    // Compare metallic vs. dielectric
    METAL_VS_DIELECTRIC: {
        name: 'Metallic vs Dielectric',
        description: 'Compare metalness values',
        secondaryModifier: (material) => {
            material.metalness = material.metalness > 0.5 ? 0.0 : 1.0;
            material.needsUpdate = true;
        }
    },

    // Compare rough vs. smooth
    ROUGH_VS_SMOOTH: {
        name: 'Rough vs Smooth',
        description: 'Compare roughness extremes',
        secondaryModifier: (material) => {
            material.roughness = material.roughness > 0.5 ? 0.0 : 1.0;
            material.needsUpdate = true;
        }
    },

    // Compare with vs. without textures
    TEXTURED_VS_FLAT: {
        name: 'Textured vs Flat',
        description: 'See texture contribution',
        secondaryModifier: (material) => {
            material.map = null;
            material.normalMap = null;
            material.roughnessMap = null;
            material.metalnessMap = null;
            material.needsUpdate = true;
        }
    }
};

// Make class globally accessible
if (typeof window !== 'undefined') {
    window.SplitViewComparison = SplitViewComparison;
    window.COMPARISON_PRESETS = COMPARISON_PRESETS;
}
