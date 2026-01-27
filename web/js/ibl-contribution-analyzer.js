// IBL Contribution Analyzer
// Split Image-Based Lighting into diffuse and specular contributions for debugging

import * as THREE from 'three';

export class IBLContributionAnalyzer {
    constructor(scene, renderer) {
        this.scene = scene;
        this.renderer = renderer;
        this.originalMaterials = new Map();
        this.currentMode = 'full'; // 'full', 'diffuse', 'specular', 'none'
    }

    // Set visualization mode
    setMode(mode) {
        this.currentMode = mode;
        this.applyMode();
    }

    // Apply current mode to all materials
    applyMode() {
        this.scene.traverse(obj => {
            if (obj.isMesh && obj.material) {
                // Store original material if not already stored
                if (!this.originalMaterials.has(obj.uuid)) {
                    this.originalMaterials.set(obj.uuid, {
                        envMapIntensity: obj.material.envMapIntensity !== undefined ? obj.material.envMapIntensity : 1.0,
                        metalness: obj.material.metalness !== undefined ? obj.material.metalness : 0.0,
                        roughness: obj.material.roughness !== undefined ? obj.material.roughness : 1.0
                    });
                }

                const original = this.originalMaterials.get(obj.uuid);

                switch (this.currentMode) {
                    case 'full':
                        // Restore full IBL contribution
                        if (obj.material.envMapIntensity !== undefined) {
                            obj.material.envMapIntensity = original.envMapIntensity;
                        }
                        if (obj.material.metalness !== undefined) {
                            obj.material.metalness = original.metalness;
                        }
                        if (obj.material.roughness !== undefined) {
                            obj.material.roughness = original.roughness;
                        }
                        break;

                    case 'diffuse':
                        // Show only diffuse IBL (force non-metallic, high roughness)
                        if (obj.material.metalness !== undefined) {
                            obj.material.metalness = 0.0;
                        }
                        if (obj.material.roughness !== undefined) {
                            obj.material.roughness = 1.0;
                        }
                        if (obj.material.envMapIntensity !== undefined) {
                            obj.material.envMapIntensity = original.envMapIntensity;
                        }
                        break;

                    case 'specular':
                        // Show only specular IBL (force metallic, reduce roughness)
                        if (obj.material.metalness !== undefined) {
                            obj.material.metalness = 1.0;
                        }
                        if (obj.material.roughness !== undefined) {
                            // Use original roughness but clamp to see clearer reflections
                            obj.material.roughness = Math.min(original.roughness, 0.3);
                        }
                        if (obj.material.envMapIntensity !== undefined) {
                            obj.material.envMapIntensity = original.envMapIntensity;
                        }
                        break;

                    case 'none':
                        // Disable IBL entirely
                        if (obj.material.envMapIntensity !== undefined) {
                            obj.material.envMapIntensity = 0.0;
                        }
                        break;
                }

                obj.material.needsUpdate = true;
            }
        });
    }

    // Reset to original materials
    reset() {
        this.scene.traverse(obj => {
            if (obj.isMesh && this.originalMaterials.has(obj.uuid)) {
                const original = this.originalMaterials.get(obj.uuid);
                if (obj.material.envMapIntensity !== undefined) {
                    obj.material.envMapIntensity = original.envMapIntensity;
                }
                if (obj.material.metalness !== undefined) {
                    obj.material.metalness = original.metalness;
                }
                if (obj.material.roughness !== undefined) {
                    obj.material.roughness = original.roughness;
                }
                obj.material.needsUpdate = true;
            }
        });
        this.originalMaterials.clear();
        this.currentMode = 'full';
    }

    // Analyze IBL contribution for a single material
    analyzeMaterial(material) {
        const analysis = {
            hasEnvMap: material.envMap !== null,
            envMapIntensity: material.envMapIntensity !== undefined ? material.envMapIntensity : 1.0,
            metalness: material.metalness !== undefined ? material.metalness : 0.0,
            roughness: material.roughness !== undefined ? material.roughness : 1.0,
            estimatedDiffuseContribution: 0,
            estimatedSpecularContribution: 0,
            dominantContribution: 'none'
        };

        if (!analysis.hasEnvMap) {
            analysis.dominantContribution = 'none';
            return analysis;
        }

        // Estimate contributions based on material properties
        // These are rough approximations of the actual BRDF integration

        // Diffuse contribution: stronger when non-metallic, affected by roughness
        const diffuseFactor = (1.0 - analysis.metalness) * analysis.envMapIntensity;
        analysis.estimatedDiffuseContribution = diffuseFactor;

        // Specular contribution: stronger when metallic or low roughness
        const specularFactor = (analysis.metalness + (1.0 - analysis.roughness) * 0.5) * analysis.envMapIntensity;
        analysis.estimatedSpecularContribution = specularFactor;

        // Determine dominant contribution
        if (analysis.estimatedDiffuseContribution > analysis.estimatedSpecularContribution * 1.2) {
            analysis.dominantContribution = 'diffuse';
        } else if (analysis.estimatedSpecularContribution > analysis.estimatedDiffuseContribution * 1.2) {
            analysis.dominantContribution = 'specular';
        } else {
            analysis.dominantContribution = 'balanced';
        }

        return analysis;
    }

    // Analyze entire scene
    analyzeScene(scene) {
        const sceneAnalysis = {
            totalMaterials: 0,
            materialsWithIBL: 0,
            averageEnvMapIntensity: 0,
            averageMetalness: 0,
            averageRoughness: 0,
            contributionBreakdown: {
                diffuseDominant: 0,
                specularDominant: 0,
                balanced: 0,
                noIBL: 0
            },
            materials: []
        };

        const materialsSet = new Set();

        scene.traverse(obj => {
            if (obj.isMesh && obj.material) {
                if (!materialsSet.has(obj.material.uuid)) {
                    materialsSet.add(obj.material.uuid);
                    sceneAnalysis.totalMaterials++;

                    const analysis = this.analyzeMaterial(obj.material);
                    sceneAnalysis.materials.push({
                        name: obj.material.name || 'Unnamed',
                        ...analysis
                    });

                    if (analysis.hasEnvMap) {
                        sceneAnalysis.materialsWithIBL++;
                        sceneAnalysis.averageEnvMapIntensity += analysis.envMapIntensity;
                        sceneAnalysis.averageMetalness += analysis.metalness;
                        sceneAnalysis.averageRoughness += analysis.roughness;

                        switch (analysis.dominantContribution) {
                            case 'diffuse':
                                sceneAnalysis.contributionBreakdown.diffuseDominant++;
                                break;
                            case 'specular':
                                sceneAnalysis.contributionBreakdown.specularDominant++;
                                break;
                            case 'balanced':
                                sceneAnalysis.contributionBreakdown.balanced++;
                                break;
                        }
                    } else {
                        sceneAnalysis.contributionBreakdown.noIBL++;
                    }
                }
            }
        });

        // Calculate averages
        if (sceneAnalysis.materialsWithIBL > 0) {
            sceneAnalysis.averageEnvMapIntensity /= sceneAnalysis.materialsWithIBL;
            sceneAnalysis.averageMetalness /= sceneAnalysis.materialsWithIBL;
            sceneAnalysis.averageRoughness /= sceneAnalysis.materialsWithIBL;
        }

        return sceneAnalysis;
    }

    // Generate text report
    generateReport(sceneAnalysis) {
        let report = '# IBL Contribution Analysis Report\n\n';
        report += `**Total Materials**: ${sceneAnalysis.totalMaterials}\n`;
        report += `**Materials with IBL**: ${sceneAnalysis.materialsWithIBL}\n`;

        if (sceneAnalysis.materialsWithIBL > 0) {
            report += `**Average EnvMap Intensity**: ${sceneAnalysis.averageEnvMapIntensity.toFixed(2)}\n`;
            report += `**Average Metalness**: ${sceneAnalysis.averageMetalness.toFixed(2)}\n`;
            report += `**Average Roughness**: ${sceneAnalysis.averageRoughness.toFixed(2)}\n\n`;

            // Contribution breakdown
            report += '## Contribution Breakdown\n\n';
            const breakdown = sceneAnalysis.contributionBreakdown;

            if (breakdown.diffuseDominant > 0) {
                const pct = (breakdown.diffuseDominant / sceneAnalysis.materialsWithIBL * 100).toFixed(1);
                report += `- **Diffuse Dominant**: ${breakdown.diffuseDominant} materials (${pct}%)\n`;
            }

            if (breakdown.specularDominant > 0) {
                const pct = (breakdown.specularDominant / sceneAnalysis.materialsWithIBL * 100).toFixed(1);
                report += `- **Specular Dominant**: ${breakdown.specularDominant} materials (${pct}%)\n`;
            }

            if (breakdown.balanced > 0) {
                const pct = (breakdown.balanced / sceneAnalysis.materialsWithIBL * 100).toFixed(1);
                report += `- **Balanced**: ${breakdown.balanced} materials (${pct}%)\n`;
            }

            if (breakdown.noIBL > 0) {
                report += `- **No IBL**: ${breakdown.noIBL} materials\n`;
            }

            report += '\n';

            // Material details
            report += '## Material Details\n\n';
            sceneAnalysis.materials.forEach(mat => {
                if (mat.hasEnvMap) {
                    report += `### ${mat.name}\n`;
                    report += `- **EnvMap Intensity**: ${mat.envMapIntensity.toFixed(2)}\n`;
                    report += `- **Metalness**: ${mat.metalness.toFixed(2)}\n`;
                    report += `- **Roughness**: ${mat.roughness.toFixed(2)}\n`;
                    report += `- **Estimated Diffuse**: ${mat.estimatedDiffuseContribution.toFixed(2)}\n`;
                    report += `- **Estimated Specular**: ${mat.estimatedSpecularContribution.toFixed(2)}\n`;
                    report += `- **Dominant**: ${mat.dominantContribution}\n\n`;
                }
            });
        } else {
            report += '\n**No materials with IBL found in scene.**\n';
        }

        return report;
    }

    // Log results to console
    logResults(sceneAnalysis) {
        console.group('🌍 IBL Contribution Analysis');
        console.log(`Total Materials: ${sceneAnalysis.totalMaterials}`);
        console.log(`Materials with IBL: ${sceneAnalysis.materialsWithIBL}`);

        if (sceneAnalysis.materialsWithIBL > 0) {
            console.log(`Avg EnvMap Intensity: ${sceneAnalysis.averageEnvMapIntensity.toFixed(2)}`);
            console.log(`Avg Metalness: ${sceneAnalysis.averageMetalness.toFixed(2)}`);
            console.log(`Avg Roughness: ${sceneAnalysis.averageRoughness.toFixed(2)}`);

            console.group('Contribution Breakdown');
            const breakdown = sceneAnalysis.contributionBreakdown;
            if (breakdown.diffuseDominant > 0) {
                console.log(`Diffuse Dominant: ${breakdown.diffuseDominant}`);
            }
            if (breakdown.specularDominant > 0) {
                console.log(`Specular Dominant: ${breakdown.specularDominant}`);
            }
            if (breakdown.balanced > 0) {
                console.log(`Balanced: ${breakdown.balanced}`);
            }
            if (breakdown.noIBL > 0) {
                console.log(`No IBL: ${breakdown.noIBL}`);
            }
            console.groupEnd();

            console.group('Material Details');
            sceneAnalysis.materials.forEach(mat => {
                if (mat.hasEnvMap) {
                    console.log(`${mat.name}: Diffuse=${mat.estimatedDiffuseContribution.toFixed(2)}, Specular=${mat.estimatedSpecularContribution.toFixed(2)}, Dominant=${mat.dominantContribution}`);
                }
            });
            console.groupEnd();
        }

        console.groupEnd();
    }
}

// Make class globally accessible
if (typeof window !== 'undefined') {
    window.IBLContributionAnalyzer = IBLContributionAnalyzer;
}
