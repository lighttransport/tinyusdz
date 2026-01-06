// Texture Tiling Detector
// Detect repeating patterns and tiling artifacts in textures

import * as THREE from 'three';

export class TextureTilingDetector {
    constructor() {
        this.canvas = null;
        this.ctx = null;
    }

    // Analyze texture for tiling patterns
    analyzeTexture(texture) {
        if (!texture || !texture.image) {
            return null;
        }

        // Create temporary canvas
        if (!this.canvas) {
            this.canvas = document.createElement('canvas');
            this.ctx = this.canvas.getContext('2d', { willReadFrequently: true });
        }

        const img = texture.image;
        this.canvas.width = img.width;
        this.canvas.height = img.height;

        // Draw image
        this.ctx.drawImage(img, 0, 0);

        // Get pixel data
        const imageData = this.ctx.getImageData(0, 0, img.width, img.height);
        const data = imageData.data;

        const analysis = {
            width: img.width,
            height: img.height,
            tilingScore: 0,
            tilingDetected: false,
            edgeSeamScore: 0,
            hasSeams: false,
            repetitionPattern: null,
            issues: []
        };

        // Check edge seams (horizontal and vertical)
        analysis.edgeSeamScore = this.detectEdgeSeams(data, img.width, img.height);
        analysis.hasSeams = analysis.edgeSeamScore > 0.1;

        if (analysis.hasSeams) {
            analysis.issues.push({
                type: 'edge_seam',
                severity: analysis.edgeSeamScore > 0.3 ? 'high' : 'medium',
                message: `Visible seams detected at texture edges (score: ${analysis.edgeSeamScore.toFixed(2)})`
            });
        }

        // Detect repetition using autocorrelation-like analysis
        const repetition = this.detectRepetition(data, img.width, img.height);
        analysis.tilingScore = repetition.score;
        analysis.tilingDetected = repetition.score > 0.3;
        analysis.repetitionPattern = repetition.pattern;

        if (analysis.tilingDetected) {
            analysis.issues.push({
                type: 'tiling_pattern',
                severity: repetition.score > 0.6 ? 'high' : 'medium',
                message: `Repeating pattern detected (score: ${repetition.score.toFixed(2)}, pattern: ${repetition.pattern})`
            });
        }

        // Check for regular grid patterns
        const gridPattern = this.detectGridPattern(data, img.width, img.height);
        if (gridPattern.detected) {
            analysis.issues.push({
                type: 'grid_pattern',
                severity: 'medium',
                message: `Regular grid pattern detected (${gridPattern.spacing}px spacing)`
            });
        }

        // Check texture resolution vs tiling
        if (img.width < 512 || img.height < 512) {
            analysis.issues.push({
                type: 'low_resolution',
                severity: 'info',
                message: `Low resolution (${img.width}×${img.height}) may show tiling artifacts when scaled`
            });
        }

        return analysis;
    }

    // Detect edge seams by comparing opposite edges
    detectEdgeSeams(data, width, height) {
        let totalDiff = 0;
        let samples = 0;

        // Compare left edge with right edge
        for (let y = 0; y < height; y++) {
            const leftIdx = (y * width + 0) * 4;
            const rightIdx = (y * width + (width - 1)) * 4;

            const diffR = Math.abs(data[leftIdx] - data[rightIdx]);
            const diffG = Math.abs(data[leftIdx + 1] - data[rightIdx + 1]);
            const diffB = Math.abs(data[leftIdx + 2] - data[rightIdx + 2]);

            totalDiff += (diffR + diffG + diffB) / 3;
            samples++;
        }

        // Compare top edge with bottom edge
        for (let x = 0; x < width; x++) {
            const topIdx = (0 * width + x) * 4;
            const bottomIdx = ((height - 1) * width + x) * 4;

            const diffR = Math.abs(data[topIdx] - data[bottomIdx]);
            const diffG = Math.abs(data[topIdx + 1] - data[bottomIdx + 1]);
            const diffB = Math.abs(data[topIdx + 2] - data[bottomIdx + 2]);

            totalDiff += (diffR + diffG + diffB) / 3;
            samples++;
        }

        // Normalize to 0-1 range
        return (totalDiff / samples) / 255;
    }

    // Detect repetition using simplified pattern matching
    detectRepetition(data, width, height) {
        // Sample-based approach for performance
        const sampleSize = 32; // Compare 32x32 blocks
        const stride = Math.max(1, Math.floor(Math.min(width, height) / 8));

        let maxSimilarity = 0;
        let bestPattern = 'none';

        // Check for horizontal repetition
        if (width >= sampleSize * 2) {
            const similarity = this.compareBlocks(
                data, width, height,
                0, 0, sampleSize, sampleSize,
                width / 2, 0, sampleSize, sampleSize
            );

            if (similarity > maxSimilarity) {
                maxSimilarity = similarity;
                bestPattern = 'horizontal';
            }
        }

        // Check for vertical repetition
        if (height >= sampleSize * 2) {
            const similarity = this.compareBlocks(
                data, width, height,
                0, 0, sampleSize, sampleSize,
                0, height / 2, sampleSize, sampleSize
            );

            if (similarity > maxSimilarity) {
                maxSimilarity = similarity;
                bestPattern = 'vertical';
            }
        }

        // Check for diagonal repetition
        if (width >= sampleSize * 2 && height >= sampleSize * 2) {
            const similarity = this.compareBlocks(
                data, width, height,
                0, 0, sampleSize, sampleSize,
                width / 2, height / 2, sampleSize, sampleSize
            );

            if (similarity > maxSimilarity) {
                maxSimilarity = similarity;
                bestPattern = 'diagonal';
            }
        }

        return {
            score: maxSimilarity,
            pattern: bestPattern
        };
    }

    // Compare two blocks of pixels
    compareBlocks(data, width, height, x1, y1, w1, h1, x2, y2, w2, h2) {
        let totalDiff = 0;
        let samples = 0;

        const blockWidth = Math.min(w1, w2);
        const blockHeight = Math.min(h1, h2);

        for (let dy = 0; dy < blockHeight; dy++) {
            for (let dx = 0; dx < blockWidth; dx++) {
                const idx1 = ((y1 + dy) * width + (x1 + dx)) * 4;
                const idx2 = ((y2 + dy) * width + (x2 + dx)) * 4;

                if (idx1 >= 0 && idx1 < data.length - 3 &&
                    idx2 >= 0 && idx2 < data.length - 3) {

                    const diffR = Math.abs(data[idx1] - data[idx2]);
                    const diffG = Math.abs(data[idx1 + 1] - data[idx2 + 1]);
                    const diffB = Math.abs(data[idx1 + 2] - data[idx2 + 2]);

                    totalDiff += (diffR + diffG + diffB) / 3;
                    samples++;
                }
            }
        }

        if (samples === 0) return 0;

        // Convert difference to similarity (0 = different, 1 = identical)
        const avgDiff = totalDiff / samples;
        return 1.0 - Math.min(1.0, avgDiff / 255);
    }

    // Detect regular grid patterns
    detectGridPattern(data, width, height) {
        // Look for repeating vertical and horizontal lines
        const threshold = 50; // Brightness difference threshold

        const verticalLines = [];
        const horizontalLines = [];

        // Sample every N pixels to find strong vertical lines
        const sampleInterval = 8;
        for (let x = 0; x < width; x += sampleInterval) {
            let edgeStrength = 0;
            for (let y = 1; y < height - 1; y++) {
                const idx = (y * width + x) * 4;
                const idxPrev = ((y - 1) * width + x) * 4;

                const diff = Math.abs(
                    (data[idx] + data[idx + 1] + data[idx + 2]) / 3 -
                    (data[idxPrev] + data[idxPrev + 1] + data[idxPrev + 2]) / 3
                );

                edgeStrength += diff;
            }

            if (edgeStrength / height > threshold) {
                verticalLines.push(x);
            }
        }

        // Check if lines are regularly spaced
        if (verticalLines.length >= 3) {
            const spacings = [];
            for (let i = 1; i < verticalLines.length; i++) {
                spacings.push(verticalLines[i] - verticalLines[i - 1]);
            }

            // Check consistency
            const avgSpacing = spacings.reduce((a, b) => a + b, 0) / spacings.length;
            const variance = spacings.reduce((sum, s) => sum + Math.pow(s - avgSpacing, 2), 0) / spacings.length;

            if (variance < avgSpacing * 0.2) { // Low variance = regular pattern
                return {
                    detected: true,
                    spacing: Math.round(avgSpacing)
                };
            }
        }

        return { detected: false };
    }

    // Analyze all textures in scene
    analyzeScene(scene) {
        const sceneAnalysis = {
            totalTextures: 0,
            texturesWithTiling: 0,
            texturesWithSeams: 0,
            averageTilingScore: 0,
            textures: []
        };

        const processedTextures = new Set();

        scene.traverse(obj => {
            if (obj.isMesh && obj.material) {
                const textureProps = ['map', 'normalMap', 'roughnessMap', 'metalnessMap', 'aoMap', 'emissiveMap'];

                textureProps.forEach(prop => {
                    const texture = obj.material[prop];
                    if (texture && texture.image && !processedTextures.has(texture.uuid)) {
                        processedTextures.add(texture.uuid);

                        const analysis = this.analyzeTexture(texture);
                        if (analysis) {
                            sceneAnalysis.totalTextures++;
                            sceneAnalysis.averageTilingScore += analysis.tilingScore;

                            if (analysis.tilingDetected) {
                                sceneAnalysis.texturesWithTiling++;
                            }

                            if (analysis.hasSeams) {
                                sceneAnalysis.texturesWithSeams++;
                            }

                            sceneAnalysis.textures.push({
                                name: prop,
                                materialName: obj.material.name || 'Unnamed',
                                ...analysis
                            });
                        }
                    }
                });
            }
        });

        if (sceneAnalysis.totalTextures > 0) {
            sceneAnalysis.averageTilingScore /= sceneAnalysis.totalTextures;
        }

        return sceneAnalysis;
    }

    // Generate report
    generateReport(sceneAnalysis) {
        let report = '# Texture Tiling Analysis Report\n\n';
        report += `**Total Textures Analyzed**: ${sceneAnalysis.totalTextures}\n`;
        report += `**Textures with Tiling Patterns**: ${sceneAnalysis.texturesWithTiling}\n`;
        report += `**Textures with Edge Seams**: ${sceneAnalysis.texturesWithSeams}\n`;
        report += `**Average Tiling Score**: ${sceneAnalysis.averageTilingScore.toFixed(3)}\n\n`;

        if (sceneAnalysis.textures.length > 0) {
            report += '## Texture Details\n\n';

            sceneAnalysis.textures.forEach(tex => {
                report += `### ${tex.materialName} - ${tex.name}\n`;
                report += `- **Resolution**: ${tex.width}×${tex.height}\n`;
                report += `- **Tiling Score**: ${tex.tilingScore.toFixed(3)} ${tex.tilingDetected ? '⚠️' : '✓'}\n`;
                report += `- **Edge Seam Score**: ${tex.edgeSeamScore.toFixed(3)} ${tex.hasSeams ? '⚠️' : '✓'}\n`;

                if (tex.repetitionPattern && tex.repetitionPattern !== 'none') {
                    report += `- **Repetition Pattern**: ${tex.repetitionPattern}\n`;
                }

                if (tex.issues.length > 0) {
                    report += `- **Issues**:\n`;
                    tex.issues.forEach(issue => {
                        const icon = issue.severity === 'high' ? '🔴' :
                                    issue.severity === 'medium' ? '🟡' : 'ℹ️';
                        report += `  ${icon} ${issue.message}\n`;
                    });
                }

                report += '\n';
            });
        }

        report += '## Recommendations\n\n';
        report += '- **Tiling Score > 0.6**: Strong repetition detected, consider using larger textures or texture variation\n';
        report += '- **Edge Seam Score > 0.3**: Visible seams, ensure texture wraps seamlessly\n';
        report += '- **Low Resolution**: Use higher resolution textures or procedural detail\n';
        report += '- **Grid Patterns**: May indicate authoring artifacts, review source textures\n';

        return report;
    }

    // Log results
    logResults(sceneAnalysis) {
        console.group('🔍 Texture Tiling Analysis');
        console.log(`Total Textures: ${sceneAnalysis.totalTextures}`);
        console.log(`Tiling Detected: ${sceneAnalysis.texturesWithTiling}`);
        console.log(`Edge Seams: ${sceneAnalysis.texturesWithSeams}`);
        console.log(`Average Tiling Score: ${sceneAnalysis.averageTilingScore.toFixed(3)}`);

        if (sceneAnalysis.textures.length > 0) {
            console.group('Texture Details');
            sceneAnalysis.textures.forEach(tex => {
                if (tex.issues.length > 0) {
                    console.group(`${tex.materialName} - ${tex.name}`);
                    tex.issues.forEach(issue => {
                        const icon = issue.severity === 'high' ? '🔴' :
                                    issue.severity === 'medium' ? '🟡' : 'ℹ️';
                        console.log(`${icon} ${issue.message}`);
                    });
                    console.groupEnd();
                }
            });
            console.groupEnd();
        }

        console.groupEnd();
    }
}

// Make class globally accessible
if (typeof window !== 'undefined') {
    window.TextureTilingDetector = TextureTilingDetector;
}
