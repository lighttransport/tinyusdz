// Mip-Map Level Visualizer
// Show which mip-map level is being sampled for textures

import * as THREE from 'three';

export class MipMapVisualizer {
    constructor() {
        this.originalMaterials = new Map();
        this.enabled = false;
        this.textureToVisualize = 'baseColor'; // 'baseColor', 'normal', 'roughness', 'metalness'
    }

    // Enable mip-map visualization
    enable(scene, textureType = 'baseColor') {
        this.enabled = true;
        this.textureToVisualize = textureType;
        this.applyVisualization(scene);
    }

    // Disable mip-map visualization
    disable(scene) {
        this.enabled = false;
        this.restoreOriginals(scene);
    }

    // Set which texture to visualize
    setTextureType(textureType) {
        this.textureToVisualize = textureType;
    }

    // Apply mip-map visualization to scene
    applyVisualization(scene) {
        scene.traverse(obj => {
            if (obj.isMesh && obj.material) {
                // Store original material
                if (!this.originalMaterials.has(obj.uuid)) {
                    this.originalMaterials.set(obj.uuid, obj.material);
                }

                // Get texture to visualize
                let texture = null;
                let textureSize = new THREE.Vector2(1024, 1024);

                switch (this.textureToVisualize) {
                    case 'baseColor':
                        texture = obj.material.map;
                        break;
                    case 'normal':
                        texture = obj.material.normalMap;
                        break;
                    case 'roughness':
                        texture = obj.material.roughnessMap;
                        break;
                    case 'metalness':
                        texture = obj.material.metalnessMap;
                        break;
                }

                if (texture && texture.image) {
                    textureSize.set(texture.image.width, texture.image.height);
                }

                // Create mip-map visualization material
                const mipMapMaterial = new THREE.ShaderMaterial({
                    vertexShader: `
                        varying vec2 vUv;
                        void main() {
                            vUv = uv;
                            gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
                        }
                    `,
                    fragmentShader: `
                        varying vec2 vUv;
                        uniform vec2 textureSize;
                        uniform bool hasTexture;

                        // Mip level to color mapping
                        vec3 mipLevelToColor(float level) {
                            // Clamp to reasonable range
                            level = clamp(level, 0.0, 8.0);

                            // Color gradient:
                            // Level 0 (highest detail) = Red
                            // Level 1 = Orange
                            // Level 2 = Yellow
                            // Level 3 = Green
                            // Level 4 = Cyan
                            // Level 5 = Blue
                            // Level 6+ = Purple/Magenta

                            if (level < 1.0) {
                                // Red to Orange
                                return mix(vec3(1.0, 0.0, 0.0), vec3(1.0, 0.5, 0.0), level);
                            } else if (level < 2.0) {
                                // Orange to Yellow
                                return mix(vec3(1.0, 0.5, 0.0), vec3(1.0, 1.0, 0.0), level - 1.0);
                            } else if (level < 3.0) {
                                // Yellow to Green
                                return mix(vec3(1.0, 1.0, 0.0), vec3(0.0, 1.0, 0.0), level - 2.0);
                            } else if (level < 4.0) {
                                // Green to Cyan
                                return mix(vec3(0.0, 1.0, 0.0), vec3(0.0, 1.0, 1.0), level - 3.0);
                            } else if (level < 5.0) {
                                // Cyan to Blue
                                return mix(vec3(0.0, 1.0, 1.0), vec3(0.0, 0.0, 1.0), level - 4.0);
                            } else {
                                // Blue to Purple
                                float t = clamp((level - 5.0) / 3.0, 0.0, 1.0);
                                return mix(vec3(0.0, 0.0, 1.0), vec3(0.5, 0.0, 1.0), t);
                            }
                        }

                        void main() {
                            if (!hasTexture) {
                                // No texture - show gray
                                gl_FragColor = vec4(0.5, 0.5, 0.5, 1.0);
                                return;
                            }

                            // Calculate mip level based on UV derivatives
                            // This estimates which mip level the GPU would sample
                            vec2 uvDx = dFdx(vUv * textureSize);
                            vec2 uvDy = dFdy(vUv * textureSize);
                            float delta = max(dot(uvDx, uvDx), dot(uvDy, uvDy));
                            float mipLevel = 0.5 * log2(max(delta, 1e-6));

                            // Clamp to valid range
                            mipLevel = max(0.0, mipLevel);

                            // Convert mip level to color
                            vec3 color = mipLevelToColor(mipLevel);

                            gl_FragColor = vec4(color, 1.0);
                        }
                    `,
                    uniforms: {
                        textureSize: { value: textureSize },
                        hasTexture: { value: texture !== null }
                    }
                });

                obj.material = mipMapMaterial;
            }
        });
    }

    // Restore original materials
    restoreOriginals(scene) {
        scene.traverse(obj => {
            if (obj.isMesh && this.originalMaterials.has(obj.uuid)) {
                obj.material = this.originalMaterials.get(obj.uuid);
            }
        });
        this.originalMaterials.clear();
    }

    // Get legend data for UI
    getLegend() {
        return [
            { level: 0, color: '#FF0000', label: 'Level 0 (Highest Detail)' },
            { level: 1, color: '#FF8000', label: 'Level 1' },
            { level: 2, color: '#FFFF00', label: 'Level 2' },
            { level: 3, color: '#00FF00', label: 'Level 3' },
            { level: 4, color: '#00FFFF', label: 'Level 4' },
            { level: 5, color: '#0000FF', label: 'Level 5' },
            { level: 6, color: '#8000FF', label: 'Level 6+' }
        ];
    }

    // Analyze scene mip-map usage
    analyzeScene(scene) {
        const analysis = {
            totalObjects: 0,
            objectsWithTextures: 0,
            textureStats: {
                baseColor: { count: 0, avgSize: 0, sizes: [] },
                normal: { count: 0, avgSize: 0, sizes: [] },
                roughness: { count: 0, avgSize: 0, sizes: [] },
                metalness: { count: 0, avgSize: 0, sizes: [] }
            }
        };

        scene.traverse(obj => {
            if (obj.isMesh && obj.material) {
                analysis.totalObjects++;

                const textures = {
                    baseColor: obj.material.map,
                    normal: obj.material.normalMap,
                    roughness: obj.material.roughnessMap,
                    metalness: obj.material.metalnessMap
                };

                let hasAnyTexture = false;

                Object.keys(textures).forEach(key => {
                    const texture = textures[key];
                    if (texture && texture.image) {
                        hasAnyTexture = true;
                        const size = texture.image.width * texture.image.height;
                        analysis.textureStats[key].count++;
                        analysis.textureStats[key].sizes.push(size);
                    }
                });

                if (hasAnyTexture) {
                    analysis.objectsWithTextures++;
                }
            }
        });

        // Calculate averages
        Object.keys(analysis.textureStats).forEach(key => {
            const stat = analysis.textureStats[key];
            if (stat.sizes.length > 0) {
                stat.avgSize = stat.sizes.reduce((a, b) => a + b, 0) / stat.sizes.length;
                stat.avgSize = Math.sqrt(stat.avgSize); // Convert to approximate dimension
            }
        });

        return analysis;
    }

    // Generate report
    generateReport(analysis) {
        let report = '# Mip-Map Analysis Report\n\n';
        report += `**Total Objects**: ${analysis.totalObjects}\n`;
        report += `**Objects with Textures**: ${analysis.objectsWithTextures}\n\n`;

        report += '## Texture Statistics\n\n';

        Object.keys(analysis.textureStats).forEach(key => {
            const stat = analysis.textureStats[key];
            if (stat.count > 0) {
                const label = key.charAt(0).toUpperCase() + key.slice(1);
                report += `### ${label} Textures\n`;
                report += `- **Count**: ${stat.count}\n`;
                report += `- **Average Resolution**: ${Math.round(stat.avgSize)}×${Math.round(stat.avgSize)}\n\n`;
            }
        });

        report += '## Mip-Map Level Color Legend\n\n';
        this.getLegend().forEach(item => {
            report += `- **Level ${item.level}**: ${item.label}\n`;
        });

        report += '\n## Interpretation\n\n';
        report += '- **Red areas**: Using highest detail (mip level 0) - close to camera\n';
        report += '- **Yellow/Green areas**: Using medium detail (mip levels 2-3) - mid-distance\n';
        report += '- **Blue/Purple areas**: Using low detail (mip levels 5+) - far from camera\n';
        report += '- **Gray areas**: No texture applied\n\n';

        report += '**Tips**:\n';
        report += '- If large areas are red, textures may be over-detailed (wasting memory)\n';
        report += '- If close objects are blue, texture resolution may be too low\n';
        report += '- Good distribution shows gradual color transition from red to blue\n';

        return report;
    }
}

// Make class globally accessible
if (typeof window !== 'undefined') {
    window.MipMapVisualizer = MipMapVisualizer;
}
