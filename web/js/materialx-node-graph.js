// MaterialX Node Graph Viewer using LiteGraph.js
// This module provides visualization for MaterialX/OpenPBR material node graphs

// Global variables for node graph
let nodeGraph = null; // LGraph instance
let nodeGraphCanvas = null; // LGraphCanvas instance
let currentMaterialForGraph = null; // Material currently displayed in graph

// Initialize node graph system
export function initializeNodeGraph() {
    console.log('Initializing MaterialX Node Graph system...');

    // Check if LiteGraph is available
    if (typeof LiteGraph === 'undefined' || typeof LGraph === 'undefined') {
        console.error('LiteGraph.js not loaded! Node graph functionality will be disabled.');
        return false;
    }

    console.log('LiteGraph.js loaded successfully');
    return true;
}

// Custom MaterialX node types for LiteGraph

// OpenPBR Surface Shader Node
function OpenPBRSurfaceNode() {
    this.addOutput("Surface", "surface");

    // Base layer inputs
    this.addInput("Base Color", "color3");
    this.addInput("Base Weight", "float");
    this.addInput("Base Metalness", "float");
    this.addInput("Base Roughness", "float");

    // Specular layer inputs
    this.addInput("Specular Weight", "float");
    this.addInput("Specular Color", "color3");
    this.addInput("Specular Roughness", "float");
    this.addInput("Specular IOR", "float");
    this.addInput("Specular Anisotropy", "float");

    // Transmission inputs
    this.addInput("Transmission Weight", "float");
    this.addInput("Transmission Color", "color3");

    // Coat inputs
    this.addInput("Coat Weight", "float");
    this.addInput("Coat Color", "color3");
    this.addInput("Coat Roughness", "float");
    this.addInput("Coat IOR", "float");

    // Emission inputs
    this.addInput("Emission Color", "color3");
    this.addInput("Emission Luminance", "float");

    // Geometry inputs
    this.addInput("Opacity", "float");
    this.addInput("Normal", "vector3");

    this.properties = {};
    this.color = "#673AB7";
    this.bgcolor = "#1a1a1a";
    this.size = [220, 360];
}

OpenPBRSurfaceNode.title = "OpenPBR Surface";
OpenPBRSurfaceNode.desc = "OpenPBR Surface Shader";

// Image/Texture Node
function ImageNode() {
    this.addInput("UV", "vector2");
    this.addOutput("Color", "color3");
    this.addOutput("R", "float");
    this.addOutput("G", "float");
    this.addOutput("B", "float");
    this.addOutput("A", "float");

    this.addProperty("file", "", "string");
    this.addProperty("colorspace", "srgb", "enum", {
        values: ["srgb", "linear", "rec709", "aces", "acescg", "raw"]
    });

    this.color = "#4CAF50";
    this.bgcolor = "#1a1a1a";
    this.size = [180, 120];
}

ImageNode.title = "Image";
ImageNode.desc = "Image/Texture Node";

ImageNode.prototype.onDrawBackground = function(ctx) {
    if (this.flags.collapsed) return;

    // Draw texture info
    ctx.fillStyle = "#888";
    ctx.font = "10px monospace";
    const filename = this.properties.file ? this.properties.file.split('/').pop() : "No file";
    ctx.fillText(filename.substring(0, 20), 10, this.size[1] - 10);
};

// Constant Color Node
function ConstantColorNode() {
    this.addOutput("Color", "color3");

    this.addProperty("color", [1, 1, 1], "vec3");
    this.widget = this.addWidget("color", "value", this.properties.color, (v) => {
        this.properties.color = v;
    });

    this.color = "#FFC107";
    this.bgcolor = "#1a1a1a";
    this.size = [140, 60];
}

ConstantColorNode.title = "Color";
ConstantColorNode.desc = "Constant Color Value";

ConstantColorNode.prototype.onExecute = function() {
    this.setOutputData(0, this.properties.color);
};

ConstantColorNode.prototype.onDrawBackground = function(ctx) {
    if (this.flags.collapsed) return;

    // Draw color preview
    const c = this.properties.color;
    ctx.fillStyle = `rgb(${c[0] * 255}, ${c[1] * 255}, ${c[2] * 255})`;
    ctx.fillRect(10, 30, this.size[0] - 20, 20);
};

// Constant Float Node
function ConstantFloatNode() {
    this.addOutput("Value", "float");

    this.addProperty("value", 0.5, "number");
    this.widget = this.addWidget("number", "value", this.properties.value, (v) => {
        this.properties.value = v;
    });

    this.color = "#03A9F4";
    this.bgcolor = "#1a1a1a";
    this.size = [140, 50];
}

ConstantFloatNode.title = "Float";
ConstantFloatNode.desc = "Constant Float Value";

ConstantFloatNode.prototype.onExecute = function() {
    this.setOutputData(0, this.properties.value);
};

// Material Output Node
function MaterialOutputNode() {
    this.addInput("Surface", "surface");

    this.properties = {};
    this.color = "#E91E63";
    this.bgcolor = "#1a1a1a";
    this.size = [140, 40];
}

MaterialOutputNode.title = "Material Output";
MaterialOutputNode.desc = "Final Material Output";

// Register custom node types
export function registerMaterialXNodeTypes() {
    if (typeof LiteGraph === 'undefined') {
        console.error('Cannot register node types: LiteGraph not loaded');
        return false;
    }

    LiteGraph.registerNodeType("materialx/openpbr_surface", OpenPBRSurfaceNode);
    LiteGraph.registerNodeType("materialx/image", ImageNode);
    LiteGraph.registerNodeType("materialx/constant_color", ConstantColorNode);
    LiteGraph.registerNodeType("materialx/constant_float", ConstantFloatNode);
    LiteGraph.registerNodeType("materialx/material_output", MaterialOutputNode);

    console.log('Registered MaterialX node types');
    return true;
}

// Create node graph from material data
export function createNodeGraphFromMaterial(materialData) {
    if (!materialData) {
        console.error('No material data provided');
        return null;
    }

    const graph = new LGraph();

    // Determine material type
    const useOpenPBR = materialData.hasOpenPBR;
    const useUsdPreview = !useOpenPBR && materialData.hasUsdPreviewSurface;

    if (!useOpenPBR && !useUsdPreview) {
        console.warn('Material has neither OpenPBR nor UsdPreviewSurface data');
        return graph;
    }

    // Create material output node
    const outputNode = LiteGraph.createNode("materialx/material_output");
    outputNode.pos = [800, 200];
    graph.add(outputNode);

    if (useOpenPBR) {
        return createOpenPBRGraph(graph, materialData, outputNode);
    } else {
        return createUsdPreviewSurfaceGraph(graph, materialData, outputNode);
    }
}

// Create OpenPBR material node graph
function createOpenPBRGraph(graph, materialData, outputNode) {
    const openPBR = materialData.openPBR;
    if (!openPBR) {
        console.error('No OpenPBR data in material');
        return graph;
    }

    // Create OpenPBR surface shader node
    const shaderNode = LiteGraph.createNode("materialx/openpbr_surface");
    shaderNode.pos = [400, 50];
    graph.add(shaderNode);

    // Connect shader to output
    shaderNode.connect(0, outputNode, 0);

    let inputIndex = 0;
    let yOffset = 50;
    const xStart = 50;

    // Helper function to create input nodes
    const createInputNode = (value, type, label, shaderInputIndex) => {
        let node;

        if (type === 'color3') {
            // Check if there's a texture
            if (value && value.textureId !== undefined && value.textureId >= 0) {
                node = LiteGraph.createNode("materialx/image");
                node.properties.file = `texture_${value.textureId}`;
                node.title = `${label} Texture`;
            } else {
                node = LiteGraph.createNode("materialx/constant_color");
                const colorValue = value?.value || value || [1, 1, 1];
                node.properties.color = Array.isArray(colorValue) ? colorValue : [colorValue, colorValue, colorValue];
                node.title = label;
            }
        } else if (type === 'float') {
            // Check if there's a texture
            if (value && value.textureId !== undefined && value.textureId >= 0) {
                node = LiteGraph.createNode("materialx/image");
                node.properties.file = `texture_${value.textureId}`;
                node.title = `${label} Texture`;
            } else {
                node = LiteGraph.createNode("materialx/constant_float");
                node.properties.value = value?.value !== undefined ? value.value : (value !== undefined ? value : 0.5);
                node.title = label;
            }
        } else {
            return null;
        }

        node.pos = [xStart, yOffset];
        yOffset += 80;
        graph.add(node);

        // Connect to shader
        node.connect(0, shaderNode, shaderInputIndex);

        return node;
    };

    // Base layer
    if (openPBR.base) {
        createInputNode(openPBR.base.color, 'color3', 'Base Color', inputIndex++);
        createInputNode(openPBR.base.weight, 'float', 'Base Weight', inputIndex++);
        createInputNode(openPBR.base.metalness, 'float', 'Metalness', inputIndex++);
        createInputNode(openPBR.base.diffuse_roughness, 'float', 'Base Roughness', inputIndex++);
    }

    // Specular layer
    if (openPBR.specular) {
        createInputNode(openPBR.specular.weight, 'float', 'Specular Weight', inputIndex++);
        createInputNode(openPBR.specular.color, 'color3', 'Specular Color', inputIndex++);
        createInputNode(openPBR.specular.roughness, 'float', 'Specular Roughness', inputIndex++);
        createInputNode(openPBR.specular.ior, 'float', 'Specular IOR', inputIndex++);
        createInputNode(openPBR.specular.anisotropy, 'float', 'Anisotropy', inputIndex++);
    }

    // Transmission
    if (openPBR.transmission && openPBR.transmission.weight > 0) {
        createInputNode(openPBR.transmission.weight, 'float', 'Transmission Weight', inputIndex++);
        createInputNode(openPBR.transmission.color, 'color3', 'Transmission Color', inputIndex++);
    }

    // Coat
    if (openPBR.coat && openPBR.coat.weight > 0) {
        createInputNode(openPBR.coat.weight, 'float', 'Coat Weight', inputIndex++);
        createInputNode(openPBR.coat.color, 'color3', 'Coat Color', inputIndex++);
        createInputNode(openPBR.coat.roughness, 'float', 'Coat Roughness', inputIndex++);
        createInputNode(openPBR.coat.ior, 'float', 'Coat IOR', inputIndex++);
    }

    // Emission
    if (openPBR.emission && (openPBR.emission.luminance > 0 ||
        (openPBR.emission.color && (openPBR.emission.color[0] > 0 || openPBR.emission.color[1] > 0 || openPBR.emission.color[2] > 0)))) {
        createInputNode(openPBR.emission.color, 'color3', 'Emission Color', inputIndex++);
        createInputNode(openPBR.emission.luminance, 'float', 'Emission Luminance', inputIndex++);
    }

    // Geometry
    if (openPBR.geometry) {
        if (openPBR.geometry.opacity !== undefined && openPBR.geometry.opacity < 1.0) {
            createInputNode(openPBR.geometry.opacity, 'float', 'Opacity', inputIndex++);
        }
    }

    return graph;
}

// Create UsdPreviewSurface material node graph
function createUsdPreviewSurfaceGraph(graph, materialData, outputNode) {
    // Similar structure but for UsdPreviewSurface
    // Implementation simplified for brevity
    console.log('Creating UsdPreviewSurface graph (simplified)');

    const shaderNode = LiteGraph.createNode("materialx/openpbr_surface");
    shaderNode.title = "UsdPreviewSurface";
    shaderNode.pos = [400, 200];
    graph.add(shaderNode);

    shaderNode.connect(0, outputNode, 0);

    return graph;
}

// Show node graph panel
export function showNodeGraph(materialData) {
    if (!materialData) {
        console.error('No material data to visualize');
        return;
    }

    currentMaterialForGraph = materialData;

    const wrapper = document.getElementById('node-graph-wrapper');
    const canvas = document.getElementById('node-graph-canvas');
    const title = document.getElementById('node-graph-title');

    if (!wrapper || !canvas) {
        console.error('Node graph DOM elements not found');
        return;
    }

    // Update title
    title.textContent = `MaterialX Node Graph - ${materialData.name || 'Material'}`;

    // Show wrapper
    wrapper.classList.add('visible');

    // Create or recreate graph
    nodeGraph = createNodeGraphFromMaterial(materialData);

    if (!nodeGraph) {
        console.error('Failed to create node graph');
        return;
    }

    // Create or update canvas
    if (nodeGraphCanvas) {
        nodeGraphCanvas.setGraph(nodeGraph);
    } else {
        nodeGraphCanvas = new LGraphCanvas(canvas, nodeGraph);
        nodeGraphCanvas.background_image = null;
        nodeGraphCanvas.clear_background = true;
        nodeGraphCanvas.render_shadows = true;
        nodeGraphCanvas.render_connections_shadows = false;
        nodeGraphCanvas.render_connection_arrows = true;

        // Customize appearance
        nodeGraphCanvas.default_link_color = "#9FA8DA";
        nodeGraphCanvas.highquality_render = true;

        // Set zoom limits for better control
        nodeGraphCanvas.ds.min_scale = 0.05;  // Allow zooming out to 5%
        nodeGraphCanvas.ds.max_scale = 2.0;   // Allow zooming in to 200%

        // Handle node selection
        nodeGraphCanvas.onNodeSelected = function(node) {
            updateNodeGraphInfo();
        };

        // Handle zoom/pan updates by listening to mouse wheel and drag events
        canvas.addEventListener('wheel', function() {
            // Update info after wheel zoom with a small delay
            setTimeout(updateNodeGraphInfo, 10);
        });

        canvas.addEventListener('mousemove', function(e) {
            // Update info during pan (when mouse button is held)
            if (e.buttons > 0) {
                updateNodeGraphInfo();
            }
        });
    }

    // Start graph execution
    nodeGraph.start();

    // Center and fit graph
    setTimeout(() => {
        centerNodeGraph();
        updateNodeGraphInfo();
    }, 100);

    console.log('Node graph displayed');
}

// Hide node graph panel
export function hideNodeGraph() {
    const wrapper = document.getElementById('node-graph-wrapper');
    if (wrapper) {
        wrapper.classList.remove('visible');
    }

    if (nodeGraph) {
        nodeGraph.stop();
    }
}

// Toggle node graph visibility
export function toggleNodeGraphVisibility() {
    const wrapper = document.getElementById('node-graph-wrapper');
    if (!wrapper) return;

    if (wrapper.classList.contains('visible')) {
        hideNodeGraph();
    } else {
        // Show graph for currently selected material
        // Get selected material from global scope
        const selectedMaterial = window.selectedMaterialForExport;
        if (selectedMaterial && selectedMaterial.data) {
            showNodeGraph(selectedMaterial.data);
        } else {
            alert('Please select a material from the Materials panel first');
        }
    }
}

// Center node graph view
export function centerNodeGraph() {
    if (!nodeGraphCanvas || !nodeGraph) return;

    // Calculate bounds of all nodes
    const nodes = nodeGraph._nodes;
    if (!nodes || nodes.length === 0) return;

    let minX = Infinity, minY = Infinity;
    let maxX = -Infinity, maxY = -Infinity;

    for (const node of nodes) {
        minX = Math.min(minX, node.pos[0]);
        minY = Math.min(minY, node.pos[1]);
        maxX = Math.max(maxX, node.pos[0] + node.size[0]);
        maxY = Math.max(maxY, node.pos[1] + node.size[1]);
    }

    const centerX = (minX + maxX) / 2;
    const centerY = (minY + maxY) / 2;
    const width = maxX - minX;
    const height = maxY - minY;

    // Calculate zoom to fit with lower initial zoom (0.25x max)
    const canvasWidth = nodeGraphCanvas.canvas.width;
    const canvasHeight = nodeGraphCanvas.canvas.height;
    const zoomX = canvasWidth / (width + 200);
    const zoomY = canvasHeight / (height + 200);
    const zoom = Math.min(zoomX, zoomY, 0.25); // Lower initial zoom factor (0.25x max)

    // Set camera - offset positions the graph center at canvas center
    nodeGraphCanvas.ds.scale = zoom;
    nodeGraphCanvas.ds.offset[0] = (canvasWidth / 2) - (centerX * zoom);
    nodeGraphCanvas.ds.offset[1] = (canvasHeight / 2) - (centerY * zoom);

    nodeGraphCanvas.setDirty(true, true);
    updateNodeGraphInfo();
}

// Update node graph info display
function updateNodeGraphInfo() {
    if (!nodeGraph || !nodeGraphCanvas) return;

    const zoomElem = document.getElementById('graph-zoom');
    const nodeCountElem = document.getElementById('graph-node-count');

    if (zoomElem) {
        zoomElem.textContent = (nodeGraphCanvas.ds.scale * 100).toFixed(0) + '%';
    }

    if (nodeCountElem) {
        nodeCountElem.textContent = nodeGraph._nodes.length;
    }
}

// Export node graph as JSON
export function exportNodeGraphAsJSON() {
    if (!nodeGraph) {
        console.error('No node graph to export');
        return;
    }

    const graphData = nodeGraph.serialize();
    const jsonString = JSON.stringify(graphData, null, 2);

    const blob = new Blob([jsonString], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = `${currentMaterialForGraph?.name || 'material'}_graph.json`;
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    URL.revokeObjectURL(url);

    console.log('Node graph exported as JSON');
}

// Make functions globally accessible
if (typeof window !== 'undefined') {
    window.toggleNodeGraph = toggleNodeGraphVisibility;
    window.centerNodeGraph = centerNodeGraph;
    window.exportNodeGraphJSON = exportNodeGraphAsJSON;
}
