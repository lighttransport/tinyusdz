// Interactive PBR Theory Guide
// Educational tooltips and explanations for PBR material properties

export class PBRTheoryGuide {
    constructor() {
        this.tooltipPanel = null;
        this.enabled = false;

        // PBR Theory content database
        this.content = {
            // Material Properties
            baseColor: {
                title: 'Base Color (Albedo)',
                description: 'The inherent color of the material surface, without lighting.',
                theory: 'Base color represents the percentage of light reflected at each wavelength. For metals, this should be colored (e.g., gold = yellow). For non-metals (dielectrics), use desaturated colors.',
                ranges: {
                    metals: 'RGB 180-255 (typically bright)',
                    nonMetals: 'RGB 50-240 (avoid pure white/black)',
                    typical: 'Most surfaces: 50-200 sRGB'
                },
                tips: [
                    'Pure white (255,255,255) is unrealistic for most materials',
                    'Pure black (0,0,0) doesn\'t exist in nature',
                    'Metals should have colored base colors',
                    'Dielectrics (plastics, wood) should be mostly desaturated'
                ],
                examples: {
                    'Charcoal': 'RGB(50, 50, 50)',
                    'Concrete': 'RGB(128, 128, 128)',
                    'White Paint': 'RGB(240, 240, 240)',
                    'Gold': 'RGB(255, 195, 86)',
                    'Copper': 'RGB(244, 162, 137)'
                }
            },

            metalness: {
                title: 'Metalness',
                description: 'Whether the material is metallic (1.0) or non-metallic/dielectric (0.0).',
                theory: 'Metalness controls the fundamental behavior of light interaction. Metals have no diffuse reflection - all light is reflected specularly with colored reflections. Non-metals have both diffuse and specular reflection with achromatic (white) specular.',
                ranges: {
                    metal: '1.0 (pure metal)',
                    nonMetal: '0.0 (dielectric)',
                    avoid: '0.1-0.9 (physically incorrect)'
                },
                tips: [
                    'Should be binary: 0.0 or 1.0 (no in-between)',
                    'Use texture maps for metal/non-metal transitions',
                    'Rusty metal: Use texture with metalness map',
                    'Painted metal: Metalness = 0.0 (paint is dielectric)'
                ],
                examples: {
                    'Metals': 'Iron, Gold, Aluminum = 1.0',
                    'Non-metals': 'Wood, Plastic, Stone = 0.0',
                    'Painted surfaces': 'Always 0.0',
                    'Rusty metal': 'Use metalness texture'
                }
            },

            roughness: {
                title: 'Roughness',
                description: 'Surface micro-facet roughness controlling specular blur.',
                theory: 'Roughness represents microscopic surface irregularities. Lower values create sharp, mirror-like reflections. Higher values create blurry, diffuse-like reflections. Based on microfacet theory (GGX/Beckmann).',
                ranges: {
                    mirror: '0.0-0.1 (polished surfaces)',
                    glossy: '0.2-0.5 (plastics, glossy paint)',
                    matte: '0.5-0.8 (matte plastic, unfinished wood)',
                    rough: '0.8-1.0 (concrete, rough stone)'
                },
                tips: [
                    'Most real materials: 0.2-0.8',
                    'Perfect mirror (0.0) is rare in nature',
                    'Combine with roughness maps for variation',
                    'Rough metals still show reflections (just blurry)'
                ],
                examples: {
                    'Polished Chrome': '0.05',
                    'Glossy Plastic': '0.3',
                    'Matte Plastic': '0.7',
                    'Rough Concrete': '0.9'
                }
            },

            ior: {
                title: 'IOR (Index of Refraction)',
                description: 'How much light bends when entering the material.',
                theory: 'IOR controls Fresnel reflectance (F0) for dielectrics. Higher IOR = stronger reflections. Related to F0 by: F0 = ((IOR-1)/(IOR+1))². Only affects dielectrics (metalness=0).',
                ranges: {
                    common: '1.4-1.6 (most materials)',
                    low: '1.0-1.3 (air, water)',
                    high: '1.6-3.0 (glass, diamond)'
                },
                tips: [
                    'Water: 1.33',
                    'Most plastics: 1.4-1.6',
                    'Glass: 1.5-1.9',
                    'Diamond: 2.42',
                    'Only affects non-metals'
                ],
                examples: {
                    'Air': '1.0',
                    'Water': '1.333',
                    'Plastic': '1.5',
                    'Glass': '1.5',
                    'Diamond': '2.42'
                }
            },

            transmission: {
                title: 'Transmission',
                description: 'How much light passes through the material (transparency).',
                theory: 'Transmission enables realistic glass/liquid rendering. Combines with IOR for proper refraction. Very expensive - requires extra render passes for refraction.',
                ranges: {
                    opaque: '0.0 (no transmission)',
                    translucent: '0.3-0.7 (frosted glass)',
                    transparent: '0.9-1.0 (clear glass, water)'
                },
                tips: [
                    'Very expensive to render (screen-space refraction)',
                    'Use with IOR for realistic glass',
                    'Combine with roughness for frosted glass',
                    'Set transparent=true for proper blending'
                ],
                examples: {
                    'Clear Glass': 'Transmission: 1.0, IOR: 1.5',
                    'Frosted Glass': 'Transmission: 0.8, Roughness: 0.3',
                    'Tinted Glass': 'Transmission: 0.9, BaseColor: tint'
                }
            },

            clearcoat: {
                title: 'Clearcoat',
                description: 'Extra specular layer on top of base material (car paint, varnish).',
                theory: 'Clearcoat adds a second specular BRDF layer with its own roughness. Common for automotive paint, varnished wood, and layered materials. Based on multi-layer BRDF model.',
                ranges: {
                    none: '0.0 (no clearcoat)',
                    subtle: '0.3-0.6 (slight shine)',
                    strong: '0.8-1.0 (car paint)'
                },
                tips: [
                    'Car paint: Clearcoat=1.0, ClearcoatRoughness=0.1',
                    'Varnished wood: Clearcoat=0.5-0.8',
                    'Use separate roughness for clearcoat layer',
                    'Adds rendering cost (extra BRDF evaluation)'
                ],
                examples: {
                    'Car Paint': 'Clearcoat: 1.0, Roughness: 0.05',
                    'Varnished Wood': 'Clearcoat: 0.6, Roughness: 0.2',
                    'Glossy Plastic': 'Clearcoat: 0.3'
                }
            },

            sheen: {
                title: 'Sheen',
                description: 'Soft specular reflection at grazing angles (fabric, velvet).',
                theory: 'Sheen adds a soft, colored specular lobe near 90° (grazing angles). Designed for cloth and fabric rendering. Based on Charlie sheen BRDF model.',
                ranges: {
                    none: '0.0 (no sheen)',
                    subtle: '0.3-0.6 (silk)',
                    strong: '0.8-1.0 (velvet)'
                },
                tips: [
                    'Best for fabric materials',
                    'Use sheenColor for colored fabrics',
                    'Combine with high roughness (0.8-1.0)',
                    'Makes edges glow softly'
                ],
                examples: {
                    'Velvet': 'Sheen: 1.0, SheenColor: fabric color',
                    'Silk': 'Sheen: 0.6, Roughness: 0.4',
                    'Cotton': 'Sheen: 0.3, Roughness: 0.8'
                }
            },

            // Textures
            normalMap: {
                title: 'Normal Map',
                description: 'Texture encoding surface normal variations for detail.',
                theory: 'Normal maps add geometric detail without extra polygons. Encode XYZ normals in RGB channels. Blue-dominant (Z-up). Must be in linear color space (NOT sRGB).',
                tips: [
                    'Must use linear color space (not sRGB)',
                    'Blue channel should dominate (Z-up)',
                    'Flat surface = RGB(128, 128, 255) in tangent space',
                    'DirectX vs OpenGL Y-axis can be flipped',
                    'Use normalScale to control intensity'
                ],
                common_issues: [
                    'Using sRGB encoding (causes incorrect lighting)',
                    'Flipped Y-channel (DX vs GL)',
                    'Too strong (unrealistic bumps)',
                    'Missing tangent vectors'
                ]
            },

            aoMap: {
                title: 'Ambient Occlusion Map',
                description: 'Pre-baked shadows in surface crevices.',
                theory: 'AO approximates how much ambient light reaches each point. Dark in crevices, bright on exposed surfaces. Multiplied with final color. Should be in linear space.',
                tips: [
                    'Use grayscale texture (R=G=B)',
                    'White = fully exposed, Black = fully occluded',
                    'Linear color space',
                    'aoMapIntensity controls strength (default 1.0)',
                    'Complements real-time lighting (not replacement)'
                ],
                common_issues: [
                    'Too dark (losing detail)',
                    'sRGB encoding (incorrect gamma)',
                    'Applied to emissive surfaces (wrong)'
                ]
            },

            // Advanced
            energyConservation: {
                title: 'Energy Conservation',
                description: 'Material cannot reflect more light than it receives.',
                theory: 'Physically correct materials obey energy conservation: diffuse + specular ≤ 1.0. Metals have no diffuse. Dielectrics balance diffuse and specular via Fresnel.',
                rules: [
                    'Metals: baseColor controls specular color, no diffuse',
                    'Dielectrics: baseColor = diffuse, specular = white (F0 ~0.04)',
                    'Base color should never be pure white for metals',
                    'Total reflected light cannot exceed incident light'
                ],
                violations: [
                    'Bright base color + high metalness + low roughness = too bright',
                    'Using colored specular on non-metals',
                    'Emissive values too high'
                ]
            },

            fresnel: {
                title: 'Fresnel Effect',
                description: 'Reflections stronger at grazing angles.',
                theory: 'The Fresnel effect describes how reflectance increases at glancing angles. All materials exhibit this. F0 (reflectance at normal incidence) varies by material: ~0.04 for dielectrics, 0.5-1.0 for metals.',
                observations: [
                    'Look at floor at your feet: dark (diffuse)',
                    'Look at floor far away: bright (reflective)',
                    'Water edge: transparent. Water horizon: mirror.',
                    'All materials show this (fundamental physics)'
                ],
                tips: [
                    'Cannot be disabled (part of BRDF)',
                    'Controlled by IOR for dielectrics',
                    'Controlled by baseColor for metals',
                    'Makes materials look realistic'
                ]
            }
        };
    }

    // Enable theory guide
    enable() {
        this.enabled = true;
        this.createTooltipPanel();
    }

    // Disable theory guide
    disable() {
        this.enabled = false;
        this.destroyTooltipPanel();
    }

    // Create tooltip panel
    createTooltipPanel() {
        this.tooltipPanel = document.createElement('div');
        this.tooltipPanel.id = 'pbr-theory-tooltip';
        this.tooltipPanel.style.cssText = `
            position: fixed;
            bottom: 10px;
            left: 10px;
            max-width: 400px;
            background: rgba(0, 0, 0, 0.95);
            border: 2px solid #4CAF50;
            border-radius: 8px;
            padding: 15px;
            color: #fff;
            font-family: 'Segoe UI', Arial, sans-serif;
            font-size: 13px;
            line-height: 1.6;
            z-index: 10000;
            box-shadow: 0 4px 12px rgba(0,0,0,0.5);
            display: none;
        `;

        document.body.appendChild(this.tooltipPanel);
    }

    // Destroy tooltip panel
    destroyTooltipPanel() {
        if (this.tooltipPanel && this.tooltipPanel.parentElement) {
            this.tooltipPanel.parentElement.removeChild(this.tooltipPanel);
        }
        this.tooltipPanel = null;
    }

    // Show tooltip for a property
    showTooltip(property) {
        if (!this.enabled || !this.tooltipPanel) return;

        const content = this.content[property];
        if (!content) {
            this.hideTooltip();
            return;
        }

        let html = `<h3 style="margin: 0 0 10px 0; color: #4CAF50; font-size: 16px;">${content.title}</h3>`;

        if (content.description) {
            html += `<p style="margin: 0 0 10px 0; font-weight: bold;">${content.description}</p>`;
        }

        if (content.theory) {
            html += `<p style="margin: 0 0 10px 0; font-style: italic; color: #aaa;">${content.theory}</p>`;
        }

        if (content.ranges) {
            html += `<div style="margin: 10px 0;"><strong>Typical Ranges:</strong><ul style="margin: 5px 0; padding-left: 20px;">`;
            Object.keys(content.ranges).forEach(key => {
                html += `<li><strong>${key}:</strong> ${content.ranges[key]}</li>`;
            });
            html += `</ul></div>`;
        }

        if (content.tips) {
            html += `<div style="margin: 10px 0;"><strong>Tips:</strong><ul style="margin: 5px 0; padding-left: 20px;">`;
            content.tips.forEach(tip => {
                html += `<li>${tip}</li>`;
            });
            html += `</ul></div>`;
        }

        if (content.examples) {
            html += `<div style="margin: 10px 0;"><strong>Examples:</strong><ul style="margin: 5px 0; padding-left: 20px;">`;
            Object.keys(content.examples).forEach(key => {
                html += `<li><strong>${key}:</strong> ${content.examples[key]}</li>`;
            });
            html += `</ul></div>`;
        }

        if (content.rules) {
            html += `<div style="margin: 10px 0;"><strong>Rules:</strong><ul style="margin: 5px 0; padding-left: 20px;">`;
            content.rules.forEach(rule => {
                html += `<li>${rule}</li>`;
            });
            html += `</ul></div>`;
        }

        if (content.observations) {
            html += `<div style="margin: 10px 0;"><strong>Observations:</strong><ul style="margin: 5px 0; padding-left: 20px;">`;
            content.observations.forEach(obs => {
                html += `<li>${obs}</li>`;
            });
            html += `</ul></div>`;
        }

        if (content.common_issues) {
            html += `<div style="margin: 10px 0;"><strong>⚠️ Common Issues:</strong><ul style="margin: 5px 0; padding-left: 20px; color: #ff9800;">`;
            content.common_issues.forEach(issue => {
                html += `<li>${issue}</li>`;
            });
            html += `</ul></div>`;
        }

        this.tooltipPanel.innerHTML = html;
        this.tooltipPanel.style.display = 'block';
    }

    // Hide tooltip
    hideTooltip() {
        if (this.tooltipPanel) {
            this.tooltipPanel.style.display = 'none';
        }
    }

    // Get all available topics
    getTopics() {
        return Object.keys(this.content).map(key => ({
            id: key,
            title: this.content[key].title
        }));
    }

    // Generate full guide as markdown
    generateGuide() {
        let guide = '# Interactive PBR Theory Guide\n\n';
        guide += 'Complete reference for physically-based rendering material properties.\n\n';
        guide += '---\n\n';

        Object.keys(this.content).forEach(key => {
            const content = this.content[key];
            guide += `## ${content.title}\n\n`;

            if (content.description) {
                guide += `**Description**: ${content.description}\n\n`;
            }

            if (content.theory) {
                guide += `**Theory**: ${content.theory}\n\n`;
            }

            if (content.ranges) {
                guide += '### Typical Ranges\n\n';
                Object.keys(content.ranges).forEach(rangeKey => {
                    guide += `- **${rangeKey}**: ${content.ranges[rangeKey]}\n`;
                });
                guide += '\n';
            }

            if (content.tips) {
                guide += '### Tips\n\n';
                content.tips.forEach(tip => {
                    guide += `- ${tip}\n`;
                });
                guide += '\n';
            }

            if (content.examples) {
                guide += '### Examples\n\n';
                Object.keys(content.examples).forEach(exKey => {
                    guide += `- **${exKey}**: ${content.examples[exKey]}\n`;
                });
                guide += '\n';
            }

            guide += '---\n\n';
        });

        return guide;
    }
}

// Make class globally accessible
if (typeof window !== 'undefined') {
    window.PBRTheoryGuide = PBRTheoryGuide;
}
