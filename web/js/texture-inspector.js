// Texture Channel Inspector
// Analyze texture channel statistics, histograms, and detect issues

import * as THREE from 'three';

export class TextureInspector {
    constructor() {
        this.canvas = null;
        this.ctx = null;
        this.currentTexture = null;
        this.currentStats = null;
    }

    // Analyze texture and generate statistics
    analyzeTexture(texture) {
        if (!texture || !texture.image) {
            return null;
        }

        // Create temporary canvas to read pixel data
        if (!this.canvas) {
            this.canvas = document.createElement('canvas');
            this.ctx = this.canvas.getContext('2d', { willReadFrequently: true });
        }

        const img = texture.image;
        this.canvas.width = img.width;
        this.canvas.height = img.height;

        // Draw image to canvas
        this.ctx.drawImage(img, 0, 0);

        // Get pixel data
        const imageData = this.ctx.getImageData(0, 0, img.width, img.height);
        const data = imageData.data;

        // Initialize statistics
        const stats = {
            width: img.width,
            height: img.height,
            pixelCount: img.width * img.height,
            channels: {
                r: this.initChannelStats(),
                g: this.initChannelStats(),
                b: this.initChannelStats(),
                a: this.initChannelStats()
            },
            issues: []
        };

        // Analyze pixels
        for (let i = 0; i < data.length; i += 4) {
            const r = data[i];
            const g = data[i + 1];
            const b = data[i + 2];
            const a = data[i + 3];

            this.updateChannelStats(stats.channels.r, r);
            this.updateChannelStats(stats.channels.g, g);
            this.updateChannelStats(stats.channels.b, b);
            this.updateChannelStats(stats.channels.a, a);
        }

        // Finalize statistics
        ['r', 'g', 'b', 'a'].forEach(ch => {
            this.finalizeChannelStats(stats.channels[ch], stats.pixelCount);
        });

        // Detect issues
        stats.issues = this.detectIssues(stats);

        this.currentStats = stats;
        return stats;
    }

    // Initialize channel statistics
    initChannelStats() {
        return {
            min: 255,
            max: 0,
            sum: 0,
            sumSquares: 0,
            mean: 0,
            stdDev: 0,
            histogram: new Array(256).fill(0),
            unique: new Set()
        };
    }

    // Update channel statistics with a new value
    updateChannelStats(channelStats, value) {
        channelStats.min = Math.min(channelStats.min, value);
        channelStats.max = Math.max(channelStats.max, value);
        channelStats.sum += value;
        channelStats.sumSquares += value * value;
        channelStats.histogram[value]++;
        channelStats.unique.add(value);
    }

    // Finalize channel statistics (calculate mean, stdDev)
    finalizeChannelStats(channelStats, pixelCount) {
        channelStats.mean = channelStats.sum / pixelCount;

        // Calculate standard deviation
        const variance = (channelStats.sumSquares / pixelCount) - (channelStats.mean * channelStats.mean);
        channelStats.stdDev = Math.sqrt(Math.max(0, variance));

        // Calculate median from histogram
        channelStats.median = this.calculateMedian(channelStats.histogram, pixelCount);

        // Count of unique values
        channelStats.uniqueCount = channelStats.unique.size;

        // Clean up Set to save memory
        delete channelStats.unique;
    }

    // Calculate median from histogram
    calculateMedian(histogram, pixelCount) {
        const halfCount = pixelCount / 2;
        let accumulated = 0;

        for (let i = 0; i < 256; i++) {
            accumulated += histogram[i];
            if (accumulated >= halfCount) {
                return i;
            }
        }

        return 127; // fallback
    }

    // Detect common texture issues
    detectIssues(stats) {
        const issues = [];

        // Check each channel
        ['r', 'g', 'b', 'a'].forEach(ch => {
            const channel = stats.channels[ch];
            const chName = ch.toUpperCase();

            // All zeros
            if (channel.max === 0) {
                issues.push({
                    severity: 'error',
                    channel: ch,
                    type: 'all_zeros',
                    message: `${chName} channel: All zeros - texture may not be loaded`
                });
            }

            // All same value (constant channel)
            if (channel.min === channel.max && channel.max > 0) {
                issues.push({
                    severity: 'warning',
                    channel: ch,
                    type: 'constant',
                    message: `${chName} channel: Constant value (${channel.min}) - no variation`
                });
            }

            // Only using 2 values (may indicate binary mask)
            if (channel.uniqueCount === 2) {
                issues.push({
                    severity: 'info',
                    channel: ch,
                    type: 'binary',
                    message: `${chName} channel: Only 2 unique values (${channel.min}, ${channel.max}) - binary mask?`
                });
            }

            // Clamped to extremes (many pixels at 0 or 255)
            const clampedLow = channel.histogram[0] / stats.pixelCount;
            const clampedHigh = channel.histogram[255] / stats.pixelCount;

            if (clampedLow > 0.5) {
                issues.push({
                    severity: 'warning',
                    channel: ch,
                    type: 'clamped_low',
                    message: `${chName} channel: ${(clampedLow * 100).toFixed(1)}% pixels at 0 (clamped/underexposed)`
                });
            }

            if (clampedHigh > 0.5) {
                issues.push({
                    severity: 'warning',
                    channel: ch,
                    type: 'clamped_high',
                    message: `${chName} channel: ${(clampedHigh * 100).toFixed(1)}% pixels at 255 (clamped/overexposed)`
                });
            }

            // Very limited range (low contrast)
            const range = channel.max - channel.min;
            if (range < 50 && range > 0) {
                issues.push({
                    severity: 'info',
                    channel: ch,
                    type: 'low_contrast',
                    message: `${chName} channel: Low contrast (range ${range}) - may be washed out`
                });
            }
        });

        // Check if RGB channels are identical (grayscale)
        const rChannel = stats.channels.r;
        const gChannel = stats.channels.g;
        const bChannel = stats.channels.b;

        if (rChannel.mean === gChannel.mean &&
            gChannel.mean === bChannel.mean &&
            rChannel.stdDev === gChannel.stdDev &&
            gChannel.stdDev === bChannel.stdDev) {
            issues.push({
                severity: 'info',
                channel: 'rgb',
                type: 'grayscale',
                message: 'RGB channels identical - this is a grayscale texture'
            });
        }

        // Check alpha channel usage
        const aChannel = stats.channels.a;
        if (aChannel.min === 255 && aChannel.max === 255) {
            issues.push({
                severity: 'info',
                channel: 'a',
                type: 'opaque',
                message: 'Alpha channel unused (all opaque) - wasting memory'
            });
        }

        return issues;
    }

    // Render histogram to canvas
    renderHistogram(canvas, channelStats, color = '#4CAF50') {
        const ctx = canvas.getContext('2d');
        const width = canvas.width;
        const height = canvas.height;

        // Clear canvas
        ctx.fillStyle = '#1a1a1a';
        ctx.fillRect(0, 0, width, height);

        const histogram = channelStats.histogram;
        const maxCount = Math.max(...histogram);

        if (maxCount === 0) return;

        const barWidth = width / 256;

        // Draw bars
        ctx.fillStyle = color;
        for (let i = 0; i < 256; i++) {
            const barHeight = (histogram[i] / maxCount) * height;
            const x = i * barWidth;
            const y = height - barHeight;

            ctx.fillRect(x, y, Math.max(1, barWidth), barHeight);
        }

        // Draw mean line
        ctx.strokeStyle = 'rgba(255, 255, 0, 0.8)';
        ctx.lineWidth = 2;
        ctx.beginPath();
        const meanX = (channelStats.mean / 255) * width;
        ctx.moveTo(meanX, 0);
        ctx.lineTo(meanX, height);
        ctx.stroke();

        // Draw median line
        ctx.strokeStyle = 'rgba(255, 165, 0, 0.8)';
        ctx.lineWidth = 1;
        ctx.beginPath();
        const medianX = (channelStats.median / 255) * width;
        ctx.moveTo(medianX, 0);
        ctx.lineTo(medianX, height);
        ctx.stroke();

        // Draw labels
        ctx.fillStyle = 'rgba(255, 255, 255, 0.7)';
        ctx.font = '10px monospace';
        ctx.fillText(`Min: ${channelStats.min}`, 5, height - 5);
        ctx.fillText(`Max: ${channelStats.max}`, width - 60, height - 5);
        ctx.fillText(`Mean: ${channelStats.mean.toFixed(1)}`, 5, 12);
        ctx.fillText(`σ: ${channelStats.stdDev.toFixed(1)}`, width - 60, 12);
    }

    // Generate text report
    generateReport(stats) {
        if (!stats) return 'No texture analyzed';

        let report = '# Texture Analysis Report\n\n';
        report += `**Dimensions**: ${stats.width} × ${stats.height} (${stats.pixelCount.toLocaleString()} pixels)\n\n`;

        // Channel statistics
        report += '## Channel Statistics\n\n';
        ['r', 'g', 'b', 'a'].forEach(ch => {
            const channel = stats.channels[ch];
            const chName = ch.toUpperCase();

            report += `### ${chName} Channel\n`;
            report += `- **Range**: ${channel.min} - ${channel.max} (span: ${channel.max - channel.min})\n`;
            report += `- **Mean**: ${channel.mean.toFixed(2)}\n`;
            report += `- **Median**: ${channel.median}\n`;
            report += `- **Std Dev**: ${channel.stdDev.toFixed(2)}\n`;
            report += `- **Unique Values**: ${channel.uniqueCount} / 256\n`;
            report += `\n`;
        });

        // Issues
        if (stats.issues.length > 0) {
            report += '## Issues Detected\n\n';

            const errors = stats.issues.filter(i => i.severity === 'error');
            const warnings = stats.issues.filter(i => i.severity === 'warning');
            const infos = stats.issues.filter(i => i.severity === 'info');

            if (errors.length > 0) {
                report += '### ❌ Errors\n';
                errors.forEach(issue => {
                    report += `- ${issue.message}\n`;
                });
                report += '\n';
            }

            if (warnings.length > 0) {
                report += '### ⚠️ Warnings\n';
                warnings.forEach(issue => {
                    report += `- ${issue.message}\n`;
                });
                report += '\n';
            }

            if (infos.length > 0) {
                report += '### ℹ️ Info\n';
                infos.forEach(issue => {
                    report += `- ${issue.message}\n`;
                });
                report += '\n';
            }
        } else {
            report += '## Issues\n\nNo issues detected ✓\n\n';
        }

        return report;
    }

    // Log results to console
    logResults(stats) {
        if (!stats) return;

        console.group('🖼️ Texture Analysis Results');
        console.log(`Dimensions: ${stats.width} × ${stats.height}`);
        console.log(`Total Pixels: ${stats.pixelCount.toLocaleString()}`);

        console.group('Channel Statistics');
        ['r', 'g', 'b', 'a'].forEach(ch => {
            const channel = stats.channels[ch];
            console.log(`${ch.toUpperCase()}: min=${channel.min}, max=${channel.max}, mean=${channel.mean.toFixed(2)}, σ=${channel.stdDev.toFixed(2)}, unique=${channel.uniqueCount}`);
        });
        console.groupEnd();

        if (stats.issues.length > 0) {
            console.group('Issues Detected');
            stats.issues.forEach(issue => {
                const icon = issue.severity === 'error' ? '❌' : issue.severity === 'warning' ? '⚠️' : 'ℹ️';
                console.log(`${icon} [${issue.channel.toUpperCase()}] ${issue.message}`);
            });
            console.groupEnd();
        } else {
            console.log('✓ No issues detected');
        }

        console.groupEnd();
    }
}

// Make class globally accessible
if (typeof window !== 'undefined') {
    window.TextureInspector = TextureInspector;
}
