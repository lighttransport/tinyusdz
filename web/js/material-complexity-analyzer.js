// Material Complexity Analyzer
// Measure and display material rendering cost for performance optimization

import * as THREE from 'three';

export class MaterialComplexityAnalyzer {
    constructor() {
        this.costWeights = {
            // Texture sampling costs
            baseTexture: 10,
            normalMap: 12,  // Requires tangent space transform
            roughnessMap: 8,
            metalnessMap: 8,
            aoMap: 8,
            emissiveMap: 10,

            // Advanced feature costs
            transmission: 50,  // Very expensive (requires extra passes)
            clearcoat: 20,
            sheen: 15,
            iridescence: 25,
            anisotropy: 18,

            // Other features
            alphaTest: 5,
            envMap: 15,
            lightMap: 10
        };
    }

    // Analyze a single material
    analyzeMaterial(material) {
        const analysis = {
            name: material.name || 'Unnamed',
            complexity: 'Low',
            relativeCost: 0,
            textures: {
                count: 0,
                list: [],
                totalMemoryMB: 0
            },
            features: {
                active: [],
                inactive: []
            },
            suggestions: []
        };

        // Count and analyze textures
        const textureProps = {
            'map': 'Base Color',
            'normalMap': 'Normal Map',
            'roughnessMap': 'Roughness',
            'metalnessMap': 'Metalness',
            'aoMap': 'Ambient Occlusion',
            'emissiveMap': 'Emissive',
            'clearcoatMap': 'Clearcoat',
            'clearcoatNormalMap': 'Clearcoat Normal',
            'clearcoatRoughnessMap': 'Clearcoat Roughness',
            'sheenColorMap': 'Sheen Color',
            'sheenRoughnessMap': 'Sheen Roughness',
            'transmissionMap': 'Transmission',
            'thicknessMap': 'Thickness',
            'specularColorMap': 'Specular Color',
            'specularIntensityMap': 'Specular Intensity',
            'iridescenceMap': 'Iridescence',
            'iridescenceThicknessMap': 'Iridescence Thickness'
        };

        Object.keys(textureProps).forEach(prop => {
            if (material[prop] && material[prop].image) {
                const texture = material[prop];
                const width = texture.image.width;
                const height = texture.image.height;
                const memoryBytes = width * height * 4; // RGBA
                const memoryMB = memoryBytes / (1024 * 1024);

                analysis.textures.count++;
                analysis.textures.totalMemoryMB += memoryMB;
                analysis.textures.list.push({
                    name: textureProps[prop],
                    property: prop,
                    dimensions: `${width}×${height}`,
                    memoryMB: memoryMB,
                    isPowerOfTwo: this.isPowerOfTwo(width) && this.isPowerOfTwo(height)
                });

                // Add texture cost
                const costKey = prop === 'map' ? 'baseTexture' :
                               prop === 'normalMap' ? 'normalMap' :
                               'baseTexture';
                analysis.relativeCost += this.costWeights[costKey] || 8;
            }
        });

        // Analyze advanced features
        const features = {
            'Transmission': material.transmission > 0,
            'Clearcoat': material.clearcoat > 0,
            'Sheen': material.sheen > 0,
            'Iridescence': material.iridescence > 0,
            'Anisotropy': material.anisotropy > 0,
            'Alpha Test': material.alphaTest > 0,
            'Environment Map': material.envMap !== null,
            'Normal Mapping': material.normalMap !== null,
            'PBR Workflow': material.isMeshStandardMaterial || material.isMeshPhysicalMaterial,
            'Double Sided': material.side === THREE.DoubleSide
        };

        Object.keys(features).forEach(featureName => {
            if (features[featureName]) {
                analysis.features.active.push(featureName);

                // Add feature costs
                const costKey = featureName.toLowerCase().replace(' ', '');
                if (this.costWeights[costKey]) {
                    analysis.relativeCost += this.costWeights[costKey];
                }
            } else {
                analysis.features.inactive.push(featureName);
            }
        });

        // Determine complexity level
        if (analysis.relativeCost < 50) {
            analysis.complexity = 'Low';
        } else if (analysis.relativeCost < 150) {
            analysis.complexity = 'Medium';
        } else if (analysis.relativeCost < 300) {
            analysis.complexity = 'High';
        } else {
            analysis.complexity = 'Very High';
        }

        // Generate optimization suggestions
        analysis.suggestions = this.generateSuggestions(analysis, material);

        return analysis;
    }

    // Analyze all materials in a scene
    analyzeScene(scene) {
        const sceneAnalysis = {
            totalMaterials: 0,
            analyzedMaterials: 0,
            totalTextures: 0,
            totalMemoryMB: 0,
            averageComplexity: 0,
            complexityDistribution: {
                'Low': 0,
                'Medium': 0,
                'High': 0,
                'Very High': 0
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
                    sceneAnalysis.analyzedMaterials++;
                    sceneAnalysis.totalTextures += analysis.textures.count;
                    sceneAnalysis.totalMemoryMB += analysis.textures.totalMemoryMB;
                    sceneAnalysis.averageComplexity += analysis.relativeCost;
                    sceneAnalysis.complexityDistribution[analysis.complexity]++;
                    sceneAnalysis.materials.push(analysis);
                }
            }
        });

        if (sceneAnalysis.analyzedMaterials > 0) {
            sceneAnalysis.averageComplexity /= sceneAnalysis.analyzedMaterials;
        }

        return sceneAnalysis;
    }

    // Generate optimization suggestions
    generateSuggestions(analysis, material) {
        const suggestions = [];

        // Texture resolution suggestions
        analysis.textures.list.forEach(tex => {
            const [width] = tex.dimensions.split('×').map(Number);

            if (width >= 4096) {
                const savings = tex.memoryMB * 0.75; // 4K → 2K saves 75%
                suggestions.push({
                    type: 'texture_resolution',
                    severity: 'medium',
                    message: `Reduce ${tex.name} from ${tex.dimensions} to 2048×2048 (saves ${savings.toFixed(1)}MB)`
                });
            } else if (width >= 2048 && tex.memoryMB > 10) {
                const savings = tex.memoryMB * 0.75; // 2K → 1K saves 75%
                suggestions.push({
                    type: 'texture_resolution',
                    severity: 'low',
                    message: `Consider reducing ${tex.name} to 1024×1024 (saves ${savings.toFixed(1)}MB)`
                });
            }

            if (!tex.isPowerOfTwo) {
                suggestions.push({
                    type: 'texture_format',
                    severity: 'low',
                    message: `${tex.name} is not power-of-two (${tex.dimensions}) - may cause performance issues`
                });
            }
        });

        // Feature cost suggestions
        if (analysis.features.active.includes('Transmission')) {
            suggestions.push({
                type: 'feature_cost',
                severity: 'high',
                message: 'Transmission is very expensive (requires extra rendering passes) - remove if not essential'
            });
        }

        if (analysis.features.active.includes('Iridescence')) {
            suggestions.push({
                type: 'feature_cost',
                severity: 'medium',
                message: 'Iridescence adds significant shader cost - consider disabling if subtle'
            });
        }

        if (analysis.features.active.includes('Clearcoat')) {
            suggestions.push({
                type: 'feature_cost',
                severity: 'medium',
                message: 'Clearcoat adds extra BRDF layer - consider combining with base layer if possible'
            });
        }

        // Texture combination suggestions
        const hasRoughness = analysis.textures.list.some(t => t.property === 'roughnessMap');
        const hasMetalness = analysis.textures.list.some(t => t.property === 'metalnessMap');
        const hasAO = analysis.textures.list.some(t => t.property === 'aoMap');

        if (hasRoughness && hasMetalness && hasAO) {
            suggestions.push({
                type: 'texture_packing',
                severity: 'medium',
                message: 'Combine Roughness (R), Metalness (G), AO (B) into single ORM texture (saves texture slots + memory)'
            });
        } else if (hasRoughness && hasMetalness) {
            suggestions.push({
                type: 'texture_packing',
                severity: 'low',
                message: 'Pack Roughness and Metalness into single texture (RG channels)'
            });
        }

        // Memory budget suggestions
        if (analysis.textures.totalMemoryMB > 100) {
            suggestions.push({
                type: 'memory',
                severity: 'high',
                message: `Total texture memory (${analysis.textures.totalMemoryMB.toFixed(1)}MB) is very high - consider compression or resolution reduction`
            });
        } else if (analysis.textures.totalMemoryMB > 50) {
            suggestions.push({
                type: 'memory',
                severity: 'medium',
                message: `Texture memory (${analysis.textures.totalMemoryMB.toFixed(1)}MB) is high - monitor performance on mobile devices`
            });
        }

        return suggestions;
    }

    // Check if number is power of two
    isPowerOfTwo(n) {
        return n > 0 && (n & (n - 1)) === 0;
    }

    // Generate text report
    generateReport(sceneAnalysis) {
        let report = '# Material Complexity Analysis Report\n\n';
        report += `**Total Materials**: ${sceneAnalysis.totalMaterials}\n`;
        report += `**Total Textures**: ${sceneAnalysis.totalTextures}\n`;
        report += `**Total Memory**: ${sceneAnalysis.totalMemoryMB.toFixed(2)} MB\n`;
        report += `**Average Complexity**: ${sceneAnalysis.averageComplexity.toFixed(1)} (cost units)\n\n`;

        // Complexity distribution
        report += '## Complexity Distribution\n\n';
        Object.keys(sceneAnalysis.complexityDistribution).forEach(level => {
            const count = sceneAnalysis.complexityDistribution[level];
            if (count > 0) {
                const percentage = (count / sceneAnalysis.totalMaterials * 100).toFixed(1);
                report += `- **${level}**: ${count} materials (${percentage}%)\n`;
            }
        });
        report += '\n';

        // Material details
        report += '## Material Details\n\n';
        sceneAnalysis.materials.forEach(mat => {
            report += `### ${mat.name}\n`;
            report += `- **Complexity**: ${mat.complexity}\n`;
            report += `- **Relative Cost**: ${mat.relativeCost}\n`;
            report += `- **Textures**: ${mat.textures.count} (${mat.textures.totalMemoryMB.toFixed(2)} MB)\n`;
            report += `- **Active Features**: ${mat.features.active.join(', ') || 'None'}\n`;

            if (mat.suggestions.length > 0) {
                report += `- **Suggestions**:\n`;
                mat.suggestions.forEach(sug => {
                    const icon = sug.severity === 'high' ? '🔴' : sug.severity === 'medium' ? '🟡' : 'ℹ️';
                    report += `  ${icon} ${sug.message}\n`;
                });
            }

            report += '\n';
        });

        return report;
    }

    // Log results to console
    logResults(sceneAnalysis) {
        console.group('⚡ Material Complexity Analysis');
        console.log(`Materials: ${sceneAnalysis.totalMaterials}`);
        console.log(`Textures: ${sceneAnalysis.totalTextures}`);
        console.log(`Memory: ${sceneAnalysis.totalMemoryMB.toFixed(2)} MB`);
        console.log(`Average Complexity: ${sceneAnalysis.averageComplexity.toFixed(1)}`);

        console.group('Complexity Distribution');
        Object.keys(sceneAnalysis.complexityDistribution).forEach(level => {
            const count = sceneAnalysis.complexityDistribution[level];
            if (count > 0) {
                console.log(`${level}: ${count}`);
            }
        });
        console.groupEnd();

        sceneAnalysis.materials.forEach(mat => {
            if (mat.suggestions.length > 0) {
                console.group(`${mat.name} (${mat.complexity})`);
                mat.suggestions.forEach(sug => {
                    const icon = sug.severity === 'high' ? '🔴' : sug.severity === 'medium' ? '🟡' : 'ℹ️';
                    console.log(`${icon} ${sug.message}`);
                });
                console.groupEnd();
            }
        });

        console.groupEnd();
    }
}

// Make class globally accessible
if (typeof window !== 'undefined') {
    window.MaterialComplexityAnalyzer = MaterialComplexityAnalyzer;
}
