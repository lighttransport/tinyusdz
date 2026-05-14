import * as THREE from 'three';
import { HDRCubeTextureLoader } from 'three/addons/loaders/HDRCubeTextureLoader.js';
import { RGBELoader } from 'three/addons/loaders/RGBELoader.js';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { TransformControls } from 'three/addons/controls/TransformControls.js';

import { GUI } from 'https://cdn.jsdelivr.net/npm/dat.gui@0.7.9/build/dat.gui.module.js';

import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js'
import { TinyUSDZLoaderUtils } from 'tinyusdz/TinyUSDZLoaderUtils.js'

// MCP
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StreamableHTTPClientTransport } from "@modelcontextprotocol/sdk/client/streamableHttp.js";
import { SSEClientTransport } from "@modelcontextprotocol/sdk/client/sse.js";

// Add CSS for selection box
const style = document.createElement('style');
style.textContent = `
  .selection-box {
    position: absolute;
    border: 2px dashed #00ff00;
    background-color: rgba(0, 255, 0, 0.1);
    pointer-events: none;
    z-index: 1000;
  }
  
  .region-selection-status {
    position: fixed;
    top: 10px;
    right: 10px;
    background: rgba(0, 255, 0, 0.8);
    color: white;
    padding: 10px;
    border-radius: 5px;
    font-family: Arial, sans-serif;
    font-size: 14px;
    z-index: 1001;
    display: none;
  }
`;
document.head.appendChild(style);



const gui = new GUI({ width: 450 });

let ui_state = {}
ui_state['rot_scale'] = 1.0;
ui_state['enable_rotation'] = false;
ui_state['defaultMtl'] = TinyUSDZLoaderUtils.createDefaultMaterial();

ui_state['envMapIntensity'] = 1.0; // NOTE: pi(3.14) is good for pisaHDR;
ui_state['ambient'] = 0.4;
ui_state['envMapType'] = 'goegap'; // 'pisa', 'goegap', 'studio'
ui_state['debugMaterial'] = {
  diffuseColor: { r: 1.0, g: 1.0, b: 1.0 },
  roughness: 0.5,
  clearcoat: 0.0,
  clearcoatRoughness: 0.0,
  enabled: false,
  diffuseMapEnabled: true,
  aoMapEnabled: true,
  roughnessMapEnabled: true,
  normalMapEnabled: true
};
let ambientLight = new THREE.AmbientLight(0x404040, ui_state['ambient']);
ui_state['needsMtlUpdate'] = false;
ui_state['renderer'] = null;
ui_state['camera'] = null;
ui_state['controls'] = null;
ui_state['mcpServer'] = "http://localhost:8085/mcp"; // MCP server URL
ui_state['mcpServerConnected'] = "Not connected";
ui_state['mcpClient'] = null;

ui_state['screenshot'] = null;
ui_state['usdLoader'] = null;

// Transform controls state
ui_state['transformControls'] = null;
ui_state['selectedObject'] = null;
ui_state['selectedObjects'] = []; // Array for multiple selection
ui_state['gizmoMode'] = 'translate'; // 'translate', 'rotate', 'scale'
ui_state['gizmoSpace'] = 'local'; // 'local', 'world'
ui_state['gizmoEnabled'] = true;
ui_state['raycaster'] = new THREE.Raycaster();
ui_state['mouse'] = new THREE.Vector2();

// Region selection state
ui_state['regionSelectionEnabled'] = false;
ui_state['isSelecting'] = false;
ui_state['selectionBox'] = null;
ui_state['selectionHelper'] = null;
ui_state['selectionStart'] = new THREE.Vector2();
ui_state['selectionEnd'] = new THREE.Vector2();


// Create a parameters object
const params = {
  envMapIntensity: ui_state['envMapIntensity'],
  envMapType: ui_state['envMapType'],
  rotationSpeed: ui_state['rot_scale'],
  enableRotation: ui_state['enable_rotation'],
  mcpServer: ui_state['mcpServer'],
  connectMcpServer: connectMCPServer,
  mcpServerConnected: ui_state['mcpServerConnected'],
  take_screenshot: takeScreenshot,
  send_screenshot_to_mcp: sendScreenshotToMCP,
  read_selected_assets: readSelectedAssets,
  clear_scene: clearScene,
  reset_camera: resetCamera,
  fit_to_scene: fitToScene,
  // Transform gizmo parameters
  gizmoEnabled: ui_state['gizmoEnabled'],
  gizmoMode: ui_state['gizmoMode'],
  gizmoSpace: ui_state['gizmoSpace'],
  // Region selection parameters
  regionSelectionEnabled: ui_state['regionSelectionEnabled'],
  // Material debug parameters
  debugMaterialEnabled: ui_state['debugMaterial'].enabled,
  diffuseR: ui_state['debugMaterial'].diffuseColor.r,
  diffuseG: ui_state['debugMaterial'].diffuseColor.g,
  diffuseB: ui_state['debugMaterial'].diffuseColor.b,
  roughness: ui_state['debugMaterial'].roughness,
  clearcoat: ui_state['debugMaterial'].clearcoat,
  clearcoatRoughness: ui_state['debugMaterial'].clearcoatRoughness,
  diffuseMapEnabled: ui_state['debugMaterial'].diffuseMapEnabled,
  aoMapEnabled: ui_state['debugMaterial'].aoMapEnabled,
  roughnessMapEnabled: ui_state['debugMaterial'].roughnessMapEnabled,
  normalMapEnabled: ui_state['debugMaterial'].normalMapEnabled
};

// Add controls
gui.add(params, 'envMapIntensity', 0, 20, 0.1).name('envMapIntensity').onChange((value) => {
  ui_state['envMapIntensity'] = value;
  ui_state['needsMtlUpdate'] = true;

});
gui.add(params, 'envMapType', ['pisa', 'goegap', 'studio']).name('Environment Map').onChange((value) => {
  ui_state['envMapType'] = value;
  switchEnvironmentMap(value);
});
gui.add(params, 'rotationSpeed', 0, 10).name('Rotation Speed').onChange((value) => {
  ui_state['rot_scale'] = value;
});
gui.add(params, 'enableRotation').name('Enable Rotation').onChange((value) => {
  ui_state['enable_rotation'] = value;
});

gui.add(params, 'mcpServer').name('MCP Server URL').onChange((value) => {
  ui_state['mcpServer'] = value;
});
gui.add(params, 'connectMcpServer').name('Connect MCP Server');
gui.add(params, 'mcpServerConnected').name('MCP Server Connected').listen();
gui.add(params, 'take_screenshot').name('Take Screenshot');
gui.add(params, 'send_screenshot_to_mcp').name('Send screenshot to MCP');
gui.add(params, 'read_selected_assets').name('Read selected assets');
gui.add(params, 'clear_scene').name('Clear Scene');
gui.add(params, 'reset_camera').name('Reset Camera');
gui.add(params, 'fit_to_scene').name('Fit to Scene');

// Transform Gizmo Controls
const gizmoFolder = gui.addFolder('Transform Gizmo');
gizmoFolder.add(params, 'gizmoEnabled').name('Enable Gizmo').onChange((value) => {
  ui_state['gizmoEnabled'] = value;
  const transformControls = ui_state['transformControls'];
  if (transformControls) {
    transformControls.visible = value;
    if (!value) {
      transformControls.detach();
      ui_state['selectedObject'] = null;
    }
  }
});
gizmoFolder.add(params, 'gizmoMode', ['translate', 'rotate', 'scale']).name('Gizmo Mode').onChange((value) => {
  ui_state['gizmoMode'] = value;
  const transformControls = ui_state['transformControls'];
  if (transformControls) {
    transformControls.setMode(value);
  }
});

gizmoFolder.add(params, 'gizmoSpace', ['local', 'world']).name('Gizmo Space').onChange((value) => {
  ui_state['gizmoSpace'] = value;
  const transformControls = ui_state['transformControls'];
  if (transformControls) {
    transformControls.setSpace(value);
  }
});

// Region Selection Controls
gizmoFolder.add(params, 'regionSelectionEnabled').name('Region Selection Mode').onChange((value) => {
  ui_state['regionSelectionEnabled'] = value;
  const controls = ui_state['controls'];
  const statusIndicator = ui_state['statusIndicator'];
  
  if (value) {
    // Disable OrbitControls when region selection is enabled
    if (controls) {
      controls.enabled = false;
    }
    if (statusIndicator) {
      statusIndicator.style.display = 'block';
    }
    console.log('Region selection mode enabled - OrbitControls disabled');
    
    // Clear any existing selection when switching modes
    clearSelection();
  } else {
    // Re-enable OrbitControls when region selection is disabled
    if (controls) {
      controls.enabled = true;
    }
    if (statusIndicator) {
      statusIndicator.style.display = 'none';
    }
    console.log('Region selection mode disabled - OrbitControls enabled');
    
    // Clear selection when disabling
    clearSelection();
  }
});

// Add debug button for gizmo
const gizmoDebug = {
  showGizmoInfo: function() {
    const transformControls = ui_state['transformControls'];
    const selectedObject = ui_state['selectedObject'];
    const selectedObjects = ui_state['selectedObjects'];
    console.log('=== Gizmo Debug Info ===');
    console.log('Gizmo enabled:', ui_state['gizmoEnabled']);
    console.log('Region selection enabled:', ui_state['regionSelectionEnabled']);
    console.log('Transform controls:', transformControls);
    console.log('Selected object (single):', selectedObject);
    console.log('Selected objects (array):', selectedObjects);
    console.log('Selection count:', selectedObjects.length);
    if (transformControls) {
      console.log('Gizmo visible:', transformControls.visible);
      console.log('Gizmo mode:', transformControls.getMode());
      console.log('Gizmo space:', transformControls.space);
      console.log('Gizmo object attached:', transformControls.object);
    }
    if (selectedObject) {
      console.log('Selected object position:', selectedObject.position);
      console.log('Selected object rotation:', selectedObject.rotation);
      console.log('Selected object scale:', selectedObject.scale);
      console.log('Selected object parent:', selectedObject.parent);
      console.log('Is multi-selection group:', selectedObject.userData.isMultiSelectionGroup);
    }
    console.log('=== End Debug Info ===');
  }
};
gizmoFolder.add(gizmoDebug, 'showGizmoInfo').name('Debug Gizmo Info');

gizmoFolder.open();

// Material Debug Controls
const materialFolder = gui.addFolder('Material Debug');
materialFolder.add(params, 'debugMaterialEnabled').name('Enable Debug Material').onChange((value) => {
  ui_state['debugMaterial'].enabled = value;
  ui_state['needsMtlUpdate'] = true;
});

// Add button to read material parameters from scene
const readMaterialParams = {
  readFromScene: function() {
    readMaterialParamsFromScene();
  }
};
materialFolder.add(readMaterialParams, 'readFromScene').name('Read from Scene');

materialFolder.add(params, 'diffuseR', 0, 1, 0.01).name('Diffuse R').onChange((value) => {
  ui_state['debugMaterial'].diffuseColor.r = value;
  ui_state['needsMtlUpdate'] = true;
});
materialFolder.add(params, 'diffuseG', 0, 1, 0.01).name('Diffuse G').onChange((value) => {
  ui_state['debugMaterial'].diffuseColor.g = value;
  ui_state['needsMtlUpdate'] = true;
});
materialFolder.add(params, 'diffuseB', 0, 1, 0.01).name('Diffuse B').onChange((value) => {
  ui_state['debugMaterial'].diffuseColor.b = value;
  ui_state['needsMtlUpdate'] = true;
});
materialFolder.add(params, 'roughness', 0, 1, 0.01).name('Roughness').onChange((value) => {
  ui_state['debugMaterial'].roughness = value;
  ui_state['needsMtlUpdate'] = true;
});
materialFolder.add(params, 'clearcoat', 0, 1, 0.01).name('Clearcoat').onChange((value) => {
  ui_state['debugMaterial'].clearcoat = value;
  ui_state['needsMtlUpdate'] = true;
});
materialFolder.add(params, 'clearcoatRoughness', 0, 1, 0.01).name('Clearcoat Roughness').onChange((value) => {
  ui_state['debugMaterial'].clearcoatRoughness = value;
  ui_state['needsMtlUpdate'] = true;
});

// Map controls
materialFolder.add(params, 'diffuseMapEnabled').name('Diffuse Map').onChange((value) => {
  ui_state['debugMaterial'].diffuseMapEnabled = value;
  ui_state['needsMtlUpdate'] = true;
});
materialFolder.add(params, 'aoMapEnabled').name('AO Map').onChange((value) => {
  ui_state['debugMaterial'].aoMapEnabled = value;
  ui_state['needsMtlUpdate'] = true;
});
materialFolder.add(params, 'roughnessMapEnabled').name('Roughness Map').onChange((value) => {
  ui_state['debugMaterial'].roughnessMapEnabled = value;
  ui_state['needsMtlUpdate'] = true;
});
materialFolder.add(params, 'normalMapEnabled').name('Normal Map').onChange((value) => {
  ui_state['debugMaterial'].normalMapEnabled = value;
  ui_state['needsMtlUpdate'] = true;
});
materialFolder.open();


async function switchEnvironmentMap(type) {
  console.log('Switching environment map to:', type);
  
  let envmap = null;
  
  switch (type) {
    case 'pisa':
      // Load HDR cube map (original)
      envmap = await new HDRCubeTextureLoader()
        .setPath('assets/textures/cube/pisaHDR/')
        .loadAsync(['px.hdr', 'nx.hdr', 'py.hdr', 'ny.hdr', 'pz.hdr', 'nz.hdr']);
      break;
      
    case 'goegap':
      // Load equirectangular HDR
      envmap = await new RGBELoader()
        .loadAsync('assets/textures/goegap_1k.hdr');
      envmap.mapping = THREE.EquirectangularReflectionMapping;
      break;
      
    case 'studio':
      // Generate synthetic studio lighting
      envmap = generateStudioLighting();
      break;
  }
  
  if (envmap) {
    // Update scene background and environment
    scene.background = envmap;
    scene.environment = envmap;
    
    // Update default material
    ui_state['defaultMtl'].envMap = envmap;
    ui_state['defaultMtl'].needsUpdate = true;
    
    // Update all materials in the scene immediately
    scene.traverse((object) => {
      if (object.material) {
        console.log("Updating material:", object.material);
        if (Array.isArray(object.material)) {
          object.material.forEach((material) => {
            if (material.envMap !== undefined) {
              material.envMap = envmap;
              material.needsUpdate = true;
            }
            // Set specularColor to 0 for MeshPhysicalMaterial
            if (material.isMeshPhysicalMaterial && material.specularColor !== undefined) {
              material.specularColor.setRGB(0, 0, 0);
              material.needsUpdate = true;
            }
            // Apply debug material parameters
            if (ui_state['debugMaterial'].enabled) {
              applyDebugMaterialParams(material);
            }
          });
        } else {
          if (object.material.envMap !== undefined) {
            object.material.envMap = envmap;
            object.material.needsUpdate = true;
          }
          // Set specularColor to 0 for MeshPhysicalMaterial
          if (object.material.isMeshPhysicalMaterial && object.material.specularColor !== undefined) {
            object.material.specularColor.setRGB(0, 0, 0);
            object.material.needsUpdate = true;
          }
          // Apply debug material parameters
          if (ui_state['debugMaterial'].enabled) {
            applyDebugMaterialParams(object.material);
          }
        }
      }
    });
    
    console.log('Environment map switched successfully');
  }
}

function generateStudioLighting() {
  // Create a simple synthetic environment map for studio lighting
  const width = 512;
  const height = 256;
  const data = new Uint8Array(width * height * 4); // RGBA format
  
  for (let i = 0; i < height; i++) {
    for (let j = 0; j < width; j++) {
      const index = (i * width + j) * 4;
      
      // Convert to spherical coordinates
      const phi = (j / width) * Math.PI * 2; // longitude
      const theta = (i / height) * Math.PI; // latitude
      
      // Create studio lighting: bright top, darker bottom, with some directional lights
      let intensity = 0.1; // base ambient
      
      // Top hemisphere brighter
      if (theta < Math.PI / 2) {
        intensity += 0.8 * (1 - theta / (Math.PI / 2));
      }
      
      // Add some directional key lights
      const keyLight1 = Math.max(0, Math.cos(phi - Math.PI / 4) * Math.cos(theta - Math.PI / 6));
      const keyLight2 = Math.max(0, Math.cos(phi - Math.PI * 1.2) * Math.cos(theta - Math.PI / 4));
      
      intensity += keyLight1 * 2.0 + keyLight2 * 1.5;
      
      // Clamp intensity and convert to 0-255 range
      intensity = Math.min(intensity, 3.0);
      const normalizedIntensity = Math.min(intensity / 3.0 * 255, 255);
      
      // Set RGBA values (slightly warm lighting)
      data[index] = normalizedIntensity * 1.0;     // R
      data[index + 1] = normalizedIntensity * 0.9; // G
      data[index + 2] = normalizedIntensity * 0.8; // B
      data[index + 3] = 255;                       // A (full opacity)
    }
  }
  
  const texture = new THREE.DataTexture(data, width, height, THREE.RGBAFormat, THREE.UnsignedByteType);
  texture.mapping = THREE.EquirectangularReflectionMapping;
  texture.needsUpdate = true;
  
  return texture;
}

function applyDebugMaterialParams(material) {
  const debugMat = ui_state['debugMaterial'];
  
  // Apply diffuse color (base color for PBR materials)
  if (material.color !== undefined) {
    material.color.setRGB(debugMat.diffuseColor.r, debugMat.diffuseColor.g, debugMat.diffuseColor.b);
  }
  
  // Apply roughness
  if (material.roughness !== undefined) {
    material.roughness = debugMat.roughness;
  }
  
  // Apply clearcoat (only for MeshPhysicalMaterial)
  if (material.isMeshPhysicalMaterial && material.clearcoat !== undefined) {
    material.clearcoat = debugMat.clearcoat;
  }
  
  // Apply clearcoat roughness (only for MeshPhysicalMaterial)
  if (material.isMeshPhysicalMaterial && material.clearcoatRoughness !== undefined) {
    material.clearcoatRoughness = debugMat.clearcoatRoughness;
  }
  
  // Store original maps for restoration
  if (!material._originalMaps) {
    material._originalMaps = {
      map: material.map,
      aoMap: material.aoMap,
      roughnessMap: material.roughnessMap,
      normalMap: material.normalMap
    };
  }
  
  // Control diffuse map (base color map)
  if (material._originalMaps.map !== undefined) {
    material.map = debugMat.diffuseMapEnabled ? material._originalMaps.map : null;
  }
  
  // Control AO map
  if (material._originalMaps.aoMap !== undefined) {
    material.aoMap = debugMat.aoMapEnabled ? material._originalMaps.aoMap : null;
  }
  
  // Control roughness map
  if (material._originalMaps.roughnessMap !== undefined) {
    material.roughnessMap = debugMat.roughnessMapEnabled ? material._originalMaps.roughnessMap : null;
  }
  
  // Control normal map
  if (material._originalMaps.normalMap !== undefined) {
    material.normalMap = debugMat.normalMapEnabled ? material._originalMaps.normalMap : null;
  }
  
  material.needsUpdate = true;
}

function readMaterialParamsFromScene() {
  // Find the first material in the scene to read its parameters
  let foundMaterial = null;
  
  scene.traverse((object) => {
    if (object.material && !foundMaterial) {
      if (Array.isArray(object.material)) {
        foundMaterial = object.material[0];
      } else {
        foundMaterial = object.material;
      }
    }
  });
  
  if (foundMaterial) {
    console.log('Reading material parameters from:', foundMaterial);
    
    // Read diffuse color
    if (foundMaterial.color !== undefined) {
      ui_state['debugMaterial'].diffuseColor.r = foundMaterial.color.r;
      ui_state['debugMaterial'].diffuseColor.g = foundMaterial.color.g;
      ui_state['debugMaterial'].diffuseColor.b = foundMaterial.color.b;
      
      // Update GUI parameters
      params.diffuseR = foundMaterial.color.r;
      params.diffuseG = foundMaterial.color.g;
      params.diffuseB = foundMaterial.color.b;
    }
    
    // Read roughness
    if (foundMaterial.roughness !== undefined) {
      ui_state['debugMaterial'].roughness = foundMaterial.roughness;
      params.roughness = foundMaterial.roughness;
    }
    
    // Read clearcoat
    if (foundMaterial.isMeshPhysicalMaterial && foundMaterial.clearcoat !== undefined) {
      ui_state['debugMaterial'].clearcoat = foundMaterial.clearcoat;
      params.clearcoat = foundMaterial.clearcoat;
    }
    
    // Read clearcoat roughness
    if (foundMaterial.isMeshPhysicalMaterial && foundMaterial.clearcoatRoughness !== undefined) {
      ui_state['debugMaterial'].clearcoatRoughness = foundMaterial.clearcoatRoughness;
      params.clearcoatRoughness = foundMaterial.clearcoatRoughness;
    }
    
    // Read map states
    ui_state['debugMaterial'].diffuseMapEnabled = foundMaterial.map !== null;
    params.diffuseMapEnabled = ui_state['debugMaterial'].diffuseMapEnabled;
    
    ui_state['debugMaterial'].aoMapEnabled = foundMaterial.aoMap !== null;
    params.aoMapEnabled = ui_state['debugMaterial'].aoMapEnabled;
    
    ui_state['debugMaterial'].roughnessMapEnabled = foundMaterial.roughnessMap !== null;
    params.roughnessMapEnabled = ui_state['debugMaterial'].roughnessMapEnabled;
    
    ui_state['debugMaterial'].normalMapEnabled = foundMaterial.normalMap !== null;
    params.normalMapEnabled = ui_state['debugMaterial'].normalMapEnabled;
    
    // Update the GUI display
    updateGUIDisplay();
    
    console.log('Material parameters read:', {
      color: { r: params.diffuseR, g: params.diffuseG, b: params.diffuseB },
      roughness: params.roughness,
      clearcoat: params.clearcoat,
      clearcoatRoughness: params.clearcoatRoughness,
      maps: {
        diffuse: params.diffuseMapEnabled,
        ao: params.aoMapEnabled,
        roughness: params.roughnessMapEnabled,
        normal: params.normalMapEnabled
      }
    });
  } else {
    console.log('No materials found in scene');
  }
}

function updateGUIDisplay() {
  // Force GUI to update its display values
  for (let i in gui.__controllers) {
    gui.__controllers[i].updateDisplay();
  }
  
  // Also update material folder controllers
  if (materialFolder) {
    for (let i in materialFolder.__controllers) {
      materialFolder.__controllers[i].updateDisplay();
    }
  }
}

function createSelectionBox() {
  const selectionBox = document.createElement('div');
  selectionBox.className = 'selection-box';
  selectionBox.style.display = 'none';
  document.body.appendChild(selectionBox);
  
  // Also create status indicator
  const statusIndicator = document.createElement('div');
  statusIndicator.className = 'region-selection-status';
  statusIndicator.textContent = 'Region Selection Mode - Drag to select multiple objects (Press Q to toggle)';
  document.body.appendChild(statusIndicator);
  ui_state['statusIndicator'] = statusIndicator;
  
  return selectionBox;
}

function updateSelectionBox() {
  const selectionBox = ui_state['selectionBox'];
  if (!selectionBox) return;
  
  const start = ui_state['selectionStart'];
  const end = ui_state['selectionEnd'];
  
  const left = Math.min(start.x, end.x);
  const top = Math.min(start.y, end.y);
  const width = Math.abs(end.x - start.x);
  const height = Math.abs(end.y - start.y);
  
  selectionBox.style.left = left + 'px';
  selectionBox.style.top = top + 'px';
  selectionBox.style.width = width + 'px';
  selectionBox.style.height = height + 'px';
  selectionBox.style.display = 'block';
}

function hideSelectionBox() {
  const selectionBox = ui_state['selectionBox'];
  if (selectionBox) {
    selectionBox.style.display = 'none';
  }
}

function getObjectsInSelectionBox() {
  const camera = ui_state['camera'];
  const start = ui_state['selectionStart'];
  const end = ui_state['selectionEnd'];
  
  // Convert screen coordinates to normalized device coordinates
  const left = Math.min(start.x, end.x);
  const top = Math.min(start.y, end.y);
  const right = Math.max(start.x, end.x);
  const bottom = Math.max(start.y, end.y);
  
  const leftNDC = (left / window.innerWidth) * 2 - 1;
  const rightNDC = (right / window.innerWidth) * 2 - 1;
  const topNDC = -(top / window.innerHeight) * 2 + 1;
  const bottomNDC = -(bottom / window.innerHeight) * 2 + 1;
  
  const selectedObjects = [];
  const selectableObjects = [];
  
  // Get all selectable objects (USD root nodes)
  scene.traverse((object) => {
    if (object.name === 'USD_Asset_Wrapper' && object.parent === scene) {
      const usdRootNode = object.children[0];
      if (usdRootNode) {
        selectableObjects.push(usdRootNode);
      }
    }
  });
  
  // Check if each object's center is within the selection box
  for (const obj of selectableObjects) {
    // Get world position of the object
    const worldPosition = new THREE.Vector3();
    obj.getWorldPosition(worldPosition);
    
    // Project to screen coordinates
    const screenPosition = worldPosition.clone();
    screenPosition.project(camera);
    
    // Convert to screen pixel coordinates
    const screenX = (screenPosition.x + 1) / 2 * window.innerWidth;
    const screenY = -(screenPosition.y - 1) / 2 * window.innerHeight;
    
    // Check if within selection box
    if (screenX >= left && screenX <= right && screenY >= top && screenY <= bottom) {
      selectedObjects.push(obj);
    }
  }
  
  return selectedObjects;
}

function clearSelection() {
  const transformControls = ui_state['transformControls'];
  if (transformControls) {
    transformControls.detach();
  }
  
  // Clean up multi-selection group if it exists
  const selectedObject = ui_state['selectedObject'];
  if (selectedObject && selectedObject.userData.isMultiSelectionGroup) {
    // Clean up userData from selected objects
    const selectedObjects = ui_state['selectedObjects'];
    for (const obj of selectedObjects) {
      delete obj.userData.multiSelectionGroup;
      delete obj.userData.originalWorldPosition;
      delete obj.userData.originalLocalPosition;
      delete obj.userData.originalLocalRotation;
      delete obj.userData.originalLocalScale;
      delete obj.userData.selectionIndex;
    }
    
    // Remove the group from scene
    scene.remove(selectedObject);
    console.log('Multi-selection group removed');
  }
  
  ui_state['selectedObject'] = null;
  ui_state['selectedObjects'] = [];
  
  console.log('Selection cleared');
}

function selectObjects(objects) {
  clearSelection();
  
  if (objects.length === 0) {
    console.log('No objects selected');
    return;
  }
  
  ui_state['selectedObjects'] = objects;
  
  if (objects.length === 1) {
    // Single object selection - attach gizmo to the object
    const transformControls = ui_state['transformControls'];
    if (transformControls) {
      transformControls.attach(objects[0]);
      ui_state['selectedObject'] = objects[0];
    }
    console.log('Single object selected:', objects[0]);
  } else {
    // Multiple object selection - create a group for transformation
    const group = new THREE.Group();
    group.name = 'MultiSelectionGroup';
    
    // Calculate the center of all selected objects in world coordinates
    const center = new THREE.Vector3();
    const objectPositions = [];
    
    for (const obj of objects) {
      const worldPos = new THREE.Vector3();
      obj.getWorldPosition(worldPos);
      objectPositions.push(worldPos.clone());
      center.add(worldPos);
    }
    center.divideScalar(objects.length);
    
    group.position.copy(center);
    scene.add(group);
    
    // Store references to selected objects and their original states
    group.userData.isMultiSelectionGroup = true;
    group.userData.selectedObjects = objects;
    group.userData.originalPositions = objectPositions;
    group.userData.originalCenter = center.clone();
    
    // Store original transforms for each selected object
    for (let i = 0; i < objects.length; i++) {
      const obj = objects[i];
      obj.userData.multiSelectionGroup = group;
      obj.userData.originalWorldPosition = objectPositions[i].clone();
      obj.userData.originalLocalPosition = obj.position.clone();
      obj.userData.originalLocalRotation = obj.rotation.clone();
      obj.userData.originalLocalScale = obj.scale.clone();
      obj.userData.selectionIndex = i;
    }
    
    const transformControls = ui_state['transformControls'];
    if (transformControls) {
      transformControls.attach(group);
      ui_state['selectedObject'] = group;
    }
    
    console.log('Multiple objects selected:', objects.length, 'Group created at:', center);
  }
}

function onMouseDown(event) {
  // Prevent default to avoid text selection
  event.preventDefault();
  
  if (ui_state['regionSelectionEnabled']) {
    // Start region selection
    ui_state['isSelecting'] = true;
    ui_state['selectionStart'].set(event.clientX, event.clientY);
    ui_state['selectionEnd'].set(event.clientX, event.clientY);
    
    const selectionBox = ui_state['selectionBox'];
    if (selectionBox) {
      updateSelectionBox();
    }
    
    console.log('Region selection started');
  }
}

function onMouseMove(event) {
  if (ui_state['regionSelectionEnabled'] && ui_state['isSelecting']) {
    // Update selection box
    ui_state['selectionEnd'].set(event.clientX, event.clientY);
    updateSelectionBox();
  }
}

function onMouseUp(event) {
  if (ui_state['regionSelectionEnabled'] && ui_state['isSelecting']) {
    // End region selection
    ui_state['isSelecting'] = false;
    hideSelectionBox();
    
    // Get objects within selection box
    const selectedObjects = getObjectsInSelectionBox();
    selectObjects(selectedObjects);
    
    console.log('Region selection completed, objects selected:', selectedObjects.length);
  }
}

function onMouseClick(event) {
  if (ui_state['regionSelectionEnabled']) {
    // In region selection mode, clicks are handled by mouse down/up
    return;
  }
  
  if (!ui_state['gizmoEnabled']) return;
  
  const transformControls = ui_state['transformControls'];
  if (transformControls && transformControls.dragging) return; // Don't select while dragging gizmo
  
  // Calculate mouse position in normalized device coordinates (-1 to +1) for both components
  ui_state['mouse'].x = (event.clientX / window.innerWidth) * 2 - 1;
  ui_state['mouse'].y = -(event.clientY / window.innerHeight) * 2 + 1;
  
  // Update the picking ray with the camera and mouse position
  ui_state['raycaster'].setFromCamera(ui_state['mouse'], ui_state['camera']);
  
  // Calculate objects intersecting the picking ray
  const intersectableObjects = [];
  scene.traverse((object) => {
    if (object.isMesh && object.visible) {
      intersectableObjects.push(object);
    }
  });
  
  const intersects = ui_state['raycaster'].intersectObjects(intersectableObjects, false);
  
  if (intersects.length > 0) {
    const selectedObject = intersects[0].object;
    
    // Find the wrapper group (USD_Asset_Wrapper) by traversing up
    let wrapperNode = selectedObject;
    while (wrapperNode.parent && wrapperNode.parent !== scene) {
      wrapperNode = wrapperNode.parent;
    }
    
    // If we found a wrapper node that's directly under the scene, get the USD root node inside it
    if (wrapperNode && wrapperNode.parent === scene && wrapperNode.name === 'USD_Asset_Wrapper') {
      // Get the actual USD root node (first child of the wrapper)
      const usdRootNode = wrapperNode.children[0];
      
      console.log('Selected object:', selectedObject);
      console.log('Wrapper node:', wrapperNode);
      console.log('USD root node:', usdRootNode);
      
      // Ensure the USD root node has valid transform values before attaching gizmo
      if (usdRootNode && isFinite(usdRootNode.position.x) && isFinite(usdRootNode.position.y) && isFinite(usdRootNode.position.z)) {
        // Attach transform controls to the USD root node instead of the wrapper
        if (transformControls) {
          transformControls.attach(usdRootNode);
          ui_state['selectedObject'] = usdRootNode;
          ui_state['selectedObjects'] = [usdRootNode]; // Update multiple selection array too
          console.log('Transform controls attached to USD root node:', usdRootNode);
          console.log('USD root position:', usdRootNode.position);
          console.log('USD root rotation:', usdRootNode.rotation);
          console.log('USD root scale:', usdRootNode.scale);
        }
      } else {
        console.warn('USD root node has invalid transform values, cannot attach gizmo');
      }
    }
  } else {
    // Clicked on empty space, deselect
    clearSelection();
    console.log('Object deselected');
  }
}

function onKeyDown(event) {
  const transformControls = ui_state['transformControls'];
  if (!transformControls || !ui_state['gizmoEnabled']) return;
  
  switch (event.code) {
    case 'KeyT':
      transformControls.setMode('translate');
      ui_state['gizmoMode'] = 'translate';
      params.gizmoMode = 'translate';
      break;
    case 'KeyR':
      transformControls.setMode('rotate');
      ui_state['gizmoMode'] = 'rotate';
      params.gizmoMode = 'rotate';
      break;
    case 'KeyS':
      transformControls.setMode('scale');
      ui_state['gizmoMode'] = 'scale';
      params.gizmoMode = 'scale';
      break;
    case 'KeyX':
      // Toggle between local and world space
      const newSpace = ui_state['gizmoSpace'] === 'local' ? 'world' : 'local';
      transformControls.setSpace(newSpace);
      ui_state['gizmoSpace'] = newSpace;
      params.gizmoSpace = newSpace;
      console.log('Gizmo space switched to:', newSpace);
      break;
    case 'Escape':
      clearSelection();
      break;
    case 'KeyQ':
      // Toggle region selection mode with Q key
      ui_state['regionSelectionEnabled'] = !ui_state['regionSelectionEnabled'];
      params.regionSelectionEnabled = ui_state['regionSelectionEnabled'];
      
      const controls = ui_state['controls'];
      const statusIndicator = ui_state['statusIndicator'];
      if (ui_state['regionSelectionEnabled']) {
        if (controls) controls.enabled = false;
        if (statusIndicator) statusIndicator.style.display = 'block';
        console.log('Region selection mode enabled (Q key) - OrbitControls disabled');
      } else {
        if (controls) controls.enabled = true;
        if (statusIndicator) statusIndicator.style.display = 'none';
        console.log('Region selection mode disabled (Q key) - OrbitControls enabled');
      }
      clearSelection();
      updateGUIDisplay();
      break;
  }
  
  // Update GUI display to reflect mode change
  updateGUIDisplay();
}

function resetCamera() {
  const camera = ui_state['camera'];
  const controls = ui_state['controls'];
  
  if (camera && controls) {
    // Reset camera to default angled position (30 degrees horizontal, 15 degrees vertical)
    const horizontalAngle = 30 * (Math.PI / 180);
    const verticalAngle = 15 * (Math.PI / 180);
    const distance = 10;
    camera.position.set(
      Math.sin(horizontalAngle) * Math.cos(verticalAngle) * distance,
      Math.sin(verticalAngle) * distance,
      Math.cos(horizontalAngle) * Math.cos(verticalAngle) * distance
    );
    controls.target.set(0, 0, 0);
    controls.update();
    console.log('Camera reset to default angled position');
  }
}

function fitToScene() {
  const camera = ui_state['camera'];
  const controls = ui_state['controls'];
  
  if (!camera || !controls) {
    console.error('Camera or controls not available');
    return;
  }

  // Compute bounding box of all objects in the scene
  const box = new THREE.Box3();
  const objectsToFit = [];
  
  scene.traverse((object) => {
    // Include mesh objects but exclude lights, cameras, and helpers
    if (object.isMesh && object.visible) {
      objectsToFit.push(object);
    }
  });

  if (objectsToFit.length === 0) {
    console.log('No objects to fit to');
    return;
  }

  // Expand bounding box to include all objects
  objectsToFit.forEach((object) => {
    const objectBox = new THREE.Box3().setFromObject(object);
    box.union(objectBox);
  });

  if (box.isEmpty()) {
    console.log('Bounding box is empty');
    return;
  }

  // Get the center and size of the bounding box
  const center = box.getCenter(new THREE.Vector3());
  const size = box.getSize(new THREE.Vector3());
  
  // Calculate the maximum dimension to determine camera distance
  const maxDim = Math.max(size.x, size.y, size.z);
  
  // Calculate distance based on camera's field of view
  const fov = camera.fov * (Math.PI / 180); // Convert to radians
  const distance = maxDim / (2 * Math.tan(fov / 2));
  
  // Add some padding (make camera a bit further back)
  const paddedDistance = distance * 1.5;
  
  // Set camera position and look-at target
  // Position camera along the current view direction but at the calculated distance
  const direction = new THREE.Vector3();
  camera.getWorldDirection(direction);
  direction.normalize();
  
  // Position camera at the calculated distance from the center
  const newPosition = center.clone().add(direction.multiplyScalar(-paddedDistance));
  
  camera.position.copy(newPosition);
  controls.target.copy(center);
  controls.update();
  
  console.log(`Fitted to scene - Center: (${center.x.toFixed(2)}, ${center.y.toFixed(2)}, ${center.z.toFixed(2)}), Distance: ${paddedDistance.toFixed(2)}`);

}

function takeScreenshot() {

  const renderer = ui_state['renderer'];
  const quality = 0.8; // JPEG quality, if you want to use JPEG format

  // strip mime prefix
  const img = renderer.domElement.toDataURL('image/jpeg', quality);
  console.log('Screenshot taken:', img);

  ui_state['screenshot'] = img;

  return img;
}

function sendScreenshotToMCP() {

  const screenshot = ui_state['screenshot'];
  if (!screenshot) {
    console.error('No screenshot available to send to MCP');
    return;
  }

  const client = ui_state['mcpClient'];
  if (!client) {
    console.error('MCP client is not connected');
    return;
  }

  // strip mime prefix
  const img_base64 = screenshot.replace(/^.*,/, '');
  client.callTool({
    name: 'save_screenshot',
    arguments: {
      data: img_base64,
      mimeType: 'image/jpeg',
      name: 'screenshot'  // FIXME. Assign unique name.
    }
  }).then((response) => {
    console.log('Screenshot sent to MCP:', response);
  }).catch((error) => {
    console.error('Error sending screenshot to MCP:', error);
  });

}

async function readSelectedAssets() {

  const client = ui_state['mcpClient'];
  if (!client) {
    console.error('MCP client is not connected');
    return;
  }

  client.callTool({
    name: 'get_selected_assets',
    arguments: {
    }
  }).then((response) => {
    const names = [];
    for (const item of response.content) {
      names.push(item.text);
    }
    console.log('Selected assets:', names);

    reloadScenes(ui_state['usdLoader'], ui_state['renderer'], names);
  }).catch((error) => {
    console.error('Error getting selected assets:', error);
  });


}

async function connectMCPServer() {
  const mcpServerUrl = ui_state['mcpServer'];
  console.log('Connecting to MCP server:', mcpServerUrl);

  // Check if the URL is valid
  if (!mcpServerUrl || !mcpServerUrl.startsWith('http')) {
    console.error('Invalid MCP server URL:', mcpServerUrl);
    return;
  }

  var client = ui_state['mcpClient'];
  if (client) {
    client.close();
  }

  const baseUrl = new URL(mcpServerUrl);
  try {
    client = new Client({
      name: 'streamable-http-client',
      version: '1.0.0'
    });
    const transport = new StreamableHTTPClientTransport(baseUrl);
    await client.connect(transport);
    console.log("Connected using Streamable HTTP transport");

  } catch (error) {
    // If that fails with a 4xx error, try the older SSE transport
    console.log("Streamable HTTP connection failed, falling back to SSE transport");
    client = new Client({
      name: 'sse-client',
      version: '1.0.0'
    });
    const sseTransport = new SSEClientTransport(baseUrl);
    await client.connect(sseTransport);
    console.log("Connected using SSE transport");
  }

  if (!client) {
    ui_state['mcpServerConnected'] = "Failed to connect to MCP server";
    params.mcpServerConnected = ui_state['mcpServerConnected']; // Update GUI parameter

    return;
  }

  const tools = await client.listTools();
  console.log(tools);

  console.log('MCP server connected:', mcpServerUrl);

  ui_state['mcpClient'] = client;
  ui_state['mcpServerConnected'] = "Connected: " + mcpServerUrl;
  params.mcpServerConnected = ui_state['mcpServerConnected']; // Update GUI parameter
}

async function getAsset(asset_info) {
  const client = ui_state['mcpClient'];
  if (!client) {
    console.error('MCP client is not connected');
    return;
  }

  let args = {};
  args.name = asset_info.name;
  if (asset_info.instance_id) {
    args.instance_id = asset_info.instance_id;
  } 
  console.log('args:', args);

  try {
    const response = await client.callTool({
      name: 'read_asset',
      arguments: args
    });
    console.log('Asset retrieved:', response);
    
    // Parse the JSON response
    const assetInfo = JSON.parse(response.content[0].text);
    console.log('Parsed asset info:', assetInfo);
    
    // Create data URI from base64 data
    const dataUri = "data:application/octet-stream;base64," + assetInfo.data;
    
    // Return object with data URI and transform information
    return {
      dataUri: dataUri,
      name: assetInfo.name,
      description: assetInfo.description,
      uuid: assetInfo.uuid,
      position: assetInfo.position || [0, 0, 0],
      scale: assetInfo.scale || [1, 1, 1],
      rotation: assetInfo.rotation || [0, 0, 0] // XYZ angles in degrees
      ,
    };
  } catch (error) {
    console.error('Error retrieving asset:', error);
    return null;
  }
}

async function loadScenes() {

  const loader = new TinyUSDZLoader();

  // it is recommended to call init() before loadAsync()
  // (wait loading/compiling wasm module in the early stage))
  await loader.init();
  TinyUSDZLoaderUtils.setTinyUSDZ(loader.native_);

  ui_state['usdLoader'] = loader; // Store loader in ui_state

  // Use zstd compressed tinyusdz.wasm to save the bandwidth.
  //await loader.init({useZstdCompressedWasm: true});

  const suzanne_filename = "./assets/suzanne-pbr.usda";
  const texcat_filename = "./assets/texture-cat-plane.usdz";
  const cookie_filename = "./assets/UsdCookie.usdz";
  //const usd_filename = "./assets/suzanne-pbr.usda";
  const usd_filename = "./assets/black-rock.usdz";
  //const usd_filename = "./assets/brown-rock.usdz";
  //const usd_filename = "./assets/rock-surface.usdz";

  var threeScenes = []

  const usd_scenes = await Promise.all([
    //loader.loadAsync(suzanne_filename),
    //loader.loadAsync(usd_filename),
    //loader.loadAsync(suzanne_filename),
  ]);

  const defaultMtl = ui_state['defaultMtl'];

  const options = {
    overrideMaterial: false, // override USD material with defaultMtl(default 'false')
    envMap: defaultMtl.envMap, // reuse envmap from defaultMtl
    envMapIntensity: ui_state['envMapIntensity'], // default envmap intensity
  }

  var offset = -(usd_scenes.length - 1) * 1.5;
  for (const usd_scene of usd_scenes) {

    const usdRootNode = usd_scene.getDefaultRootNode();

    const threeNode = await TinyUSDZLoaderUtils.buildThreeNode(usdRootNode, defaultMtl, usd_scene, options);

    if (usd_scene.getURI().includes('UsdCookie')) {
      // Add exra scaling
      threeNode.scale.x *= 2.5;
      threeNode.scale.y *= 2.5;
      threeNode.scale.z *= 2.5;
    }

    threeNode.position.x += offset;
    offset += 3.0;

    threeScenes.push(threeNode);
  }

  return threeScenes;

}

function clearScene() {
  // Remove all objects from the scene except lights and environment
  const objectsToRemove = [];

  scene.traverse((object) => {
    // Keep lights, cameras, and the scene itself
    if (object !== scene &&
      !object.isLight &&
      !object.isCamera &&
      object.parent === scene) {
      objectsToRemove.push(object);
    }
  });

  // Remove objects
  objectsToRemove.forEach((object) => {
    scene.remove(object);

    // Dispose of geometries and materials to free memory
    if (object.geometry) {
      object.geometry.dispose();
    }

    if (object.material) {
      if (Array.isArray(object.material)) {
        object.material.forEach((material) => {
          if (material.map) material.map.dispose();
          if (material.normalMap) material.normalMap.dispose();
          if (material.roughnessMap) material.roughnessMap.dispose();
          if (material.metalnessMap) material.metalnessMap.dispose();
          material.dispose();
        });
      } else {
        if (object.material.map) object.material.map.dispose();
        if (object.material.normalMap) object.material.normalMap.dispose();
        if (object.material.roughnessMap) object.material.roughnessMap.dispose();
        if (object.material.metalnessMap) object.material.metalnessMap.dispose();
        object.material.dispose();
      }
    }
  });

  console.log('Scene cleared');
}

async function reloadScenes(loader, renderer, asset_names) {

  // Clear existing scenes first
  clearScene();

  var threeScenes = []
  var assetTransforms = [] // Store transform info for each asset

  var usd_scenes = [];
  for (const asset_jsoninfo of asset_names) {
    console.log('Loading asset:', asset_jsoninfo);

    const assetInfo = await getAsset(JSON.parse(asset_jsoninfo));
    if (!assetInfo) {
      console.error('Failed to load asset:', asset_jsoninfo);
      continue;
    }

    console.log('Asset info for', asset_jsoninfo, ':', assetInfo);

    const usd_scene = await loader.loadAsync(assetInfo.dataUri);
    console.log('Loaded USD scene:', usd_scene);

    usd_scenes.push(usd_scene);
    assetTransforms.push({
      position: assetInfo.position,
      scale: assetInfo.scale,
      rotation: assetInfo.rotation
    });
  }

  const defaultMtl = ui_state['defaultMtl'];

  const options = {
    overrideMaterial: false, // override USD material with defaultMtl(default 'false')
    envMap: defaultMtl.envMap, // reuse envmap from defaultMtl
    envMapIntensity: ui_state['envMapIntensity'], // default envmap intensity
  }

  var offset = -(usd_scenes.length - 1) * 1.5;
  for (let i = 0; i < usd_scenes.length; i++) {
    const usd_scene = usd_scenes[i];
    const transform = assetTransforms[i];
    const asset_name = asset_names[i]; // Get asset name for logging

    const usdRootNode = usd_scene.getDefaultRootNode();

    const threeNode = await TinyUSDZLoaderUtils.buildThreeNode(usdRootNode, defaultMtl, usd_scene, options);

    // Apply transform information from MCP
    console.log('Applying transform to', asset_name, ':', transform);
    
    // Apply position (add to offset for spacing)
    threeNode.position.set(
      transform.position[0],
      transform.position[1], 
      transform.position[2]
    );
    
    // Apply scale (multiply with existing scale)
    threeNode.scale.multiply(new THREE.Vector3(
      transform.scale[0],
      transform.scale[1], 
      transform.scale[2]
    ));
    
    // Apply rotation (convert degrees to radians and add to existing rotation)
    threeNode.rotation.x += transform.rotation[0] * (Math.PI / 180);
    threeNode.rotation.y += transform.rotation[1] * (Math.PI / 180);
    threeNode.rotation.z += transform.rotation[2] * (Math.PI / 180);
    
    console.log('Final transform applied - Position:', threeNode.position, 'Scale:', threeNode.scale, 'Rotation (rad):', threeNode.rotation);

    //offset += 3.0;

    threeScenes.push(threeNode);
  }

  ui_state['threeNodes'] = [];

  for (const rootNode of threeScenes) {
    // Create a wrapper group to handle the Y-up axis conversion
    const wrapperGroup = new THREE.Group();
    wrapperGroup.name = 'USD_Asset_Wrapper';
    
    // Apply the Y-up axis rotation to the wrapper instead of the root node
    wrapperGroup.rotation.x = -Math.PI / 2; // Rotate to match Y-up axis
    //wrapperGroup.rotation.y = -Math.PI; // Rotate to match Y-up axis
    //wrapperGroup.rotation.z = Math.PI / 2; // Rotate to match Y-up axis
    
    // Add the USD root node to the wrapper (keeping its original transform)
    wrapperGroup.add(rootNode);
    
    // Add the wrapper to the scene instead of the root node directly
    scene.add(wrapperGroup);

    ui_state['threeNodes'].push(wrapperGroup); // Store wrapper instead of rootNode
  }

  // Apply specularColor = 0 to all MeshPhysicalMaterials in newly loaded scenes
  scene.traverse((object) => {
    if (object.material) {
      if (Array.isArray(object.material)) {
        object.material.forEach((material) => {
          if (material.isMeshPhysicalMaterial && material.specularColor !== undefined) {
            material.specularColor.setRGB(0, 0, 0);
            material.needsUpdate = true;
          }
          // Apply debug material parameters
          if (ui_state['debugMaterial'].enabled) {
            applyDebugMaterialParams(material);
          }
        });
      } else {
        if (object.material.isMeshPhysicalMaterial && object.material.specularColor !== undefined) {
          object.material.specularColor.setRGB(0, 0, 0);
          object.material.needsUpdate = true;
        }
        // Apply debug material parameters
        if (ui_state['debugMaterial'].enabled) {
          applyDebugMaterialParams(object.material);
        }
      }
    }
  });

  // Read material parameters from the newly loaded scene
  readMaterialParamsFromScene();

  fitToScene();

  //scene.updateMatrix();
  console.log(ui_state['camera']);
  const transformControls = createTransformControlsHelper(ui_state['camera'], renderer);

  // re-add transform controls helper
  scene.add(transformControls.getHelper());
}



const scene = new THREE.Scene();

function createTransformControlsHelper(camera, renderer) {

  // Initialize TransformControls
  const transformControls = new TransformControls(camera, renderer.domElement);
  //transformControls.setMode(ui_state['gizmoMode']);
  transformControls.visible = ui_state['gizmoEnabled'];
  //transformControls.setSpace(ui_state['gizmoSpace']); // Use the space setting from ui_state
  
  // Check if transformControls is a valid THREE.Object3D before adding
  console.log('TransformControls type:', transformControls);
  console.log('Is Object3D:', transformControls instanceof THREE.Object3D);
  console.log('TransformControls constructor:', TransformControls);
  
  
  ui_state['transformControls'] = transformControls;
  
  const controls = ui_state['controls'];

  // Add event listeners for transform controls
  transformControls.addEventListener('dragging-changed', (event) => {
    // Only enable/disable orbit controls if not in region selection mode
    if (!ui_state['regionSelectionEnabled']) {
      controls.enabled = !event.value; // Disable orbit controls while dragging gizmo
    }
    // In region selection mode, keep OrbitControls disabled
  });
  
  transformControls.addEventListener('change', () => {
    // Add callback for when transform changes with NaN protection
    const selectedObject = ui_state['selectedObject'];
    if (selectedObject) {
      // Check for NaN values and reset if found
      if (!isFinite(selectedObject.position.x) || !isFinite(selectedObject.position.y) || !isFinite(selectedObject.position.z)) {
        console.warn('NaN detected in position, resetting to origin');
        selectedObject.position.set(0, 0, 0);
      }
      if (!isFinite(selectedObject.rotation.x) || !isFinite(selectedObject.rotation.y) || !isFinite(selectedObject.rotation.z)) {
        console.warn('NaN detected in rotation, resetting to zero');
        selectedObject.rotation.set(0, 0, 0);
      }
      if (!isFinite(selectedObject.scale.x) || !isFinite(selectedObject.scale.y) || !isFinite(selectedObject.scale.z)) {
        console.warn('NaN detected in scale, resetting to one');
        selectedObject.scale.set(1, 1, 1);
      }
      
      // Handle multi-selection group transformation
      if (selectedObject.userData.isMultiSelectionGroup) {
        const selectedObjects = ui_state['selectedObjects'];
        const originalCenter = selectedObject.userData.originalCenter;
        
        // Ensure we still have valid selected objects
        if (!selectedObjects || selectedObjects.length === 0) {
          console.warn('Multi-selection group lost its selected objects');
          return;
        }
        
        // Calculate transformation deltas
        const groupPosition = selectedObject.position;
        const groupRotation = selectedObject.rotation;
        const groupScale = selectedObject.scale;
        
        // Calculate position delta
        const positionDelta = new THREE.Vector3();
        positionDelta.subVectors(groupPosition, originalCenter);
        
        // Apply transformations to each selected object
        for (let i = 0; i < selectedObjects.length; i++) {
          const obj = selectedObjects[i];
          
          // Ensure object still has required userData
          if (!obj.userData.originalWorldPosition || !obj.userData.originalLocalPosition) {
            console.warn('Object missing original transform data, skipping:', obj);
            continue;
          }
          
          const originalWorldPos = obj.userData.originalWorldPosition;
          const originalLocalPos = obj.userData.originalLocalPosition;
          const originalLocalRot = obj.userData.originalLocalRotation;
          const originalLocalScale = obj.userData.originalLocalScale;
          
          // Calculate relative position from original center
          const relativePos = new THREE.Vector3();
          relativePos.subVectors(originalWorldPos, originalCenter);
          
          // Apply rotation to relative position
          const rotatedRelativePos = relativePos.clone();
          
          // Create rotation matrix from group rotation
          const rotationMatrix = new THREE.Matrix4();
          rotationMatrix.makeRotationFromEuler(groupRotation);
          
          // Apply rotation to the relative position
          rotatedRelativePos.applyMatrix4(rotationMatrix);
          
          // Apply scale to the relative position
          rotatedRelativePos.multiply(groupScale);
          
          // Calculate new world position
          const newWorldPos = originalCenter.clone();
          newWorldPos.add(rotatedRelativePos);
          newWorldPos.add(positionDelta);
          
          // Convert world position back to local position in the wrapper coordinate system
          // The objects are children of wrapper groups with -90 degree X rotation
          const wrapper = obj.parent;
          if (wrapper && wrapper.name === 'USD_Asset_Wrapper') {
            // Convert from world space to wrapper's local space
            const localPos = newWorldPos.clone();
            wrapper.worldToLocal(localPos);
            obj.position.copy(localPos);
          } else {
            // Fallback: set world position directly
            obj.position.copy(newWorldPos);
          }
          
          // Apply rotation (additive to original)
          obj.rotation.copy(originalLocalRot);
          obj.rotation.x += groupRotation.x;
          obj.rotation.y += groupRotation.y;
          obj.rotation.z += groupRotation.z;
          
          // Apply scale (multiplicative with original)
          obj.scale.copy(originalLocalScale);
          obj.scale.multiply(groupScale);
        }
        
        console.log('Multi-selection transform applied to', selectedObjects.length, 'objects');
      }
      
      console.log('Transform changed - Position:', selectedObject.position, 'Rotation:', selectedObject.rotation, 'Scale:', selectedObject.scale);
    }
  });
  
  transformControls.addEventListener('objectChange', () => {
    // This fires when the attached object changes
    const selectedObjects = ui_state['selectedObjects'];
    if (selectedObjects.length > 1) {
      console.log('Multi-selection transform updated, objects count:', selectedObjects.length);
    }
    
    // Check if the gizmo got detached unexpectedly in region selection mode
    if (ui_state['regionSelectionEnabled'] && selectedObjects.length > 0 && !transformControls.object) {
      console.warn('TransformControls detached unexpectedly in region selection mode, re-attaching...');
      
      // Re-attach to the current selected object
      const selectedObject = ui_state['selectedObject'];
      if (selectedObject) {
        transformControls.attach(selectedObject);
        console.log('TransformControls re-attached to:', selectedObject);
      }
    }
  });

  return transformControls;
}

async function initScene() {

  // Load initial environment map (Pisa HDR)
  await switchEnvironmentMap(ui_state['envMapType']);

  // Assign envmap to material
  // Otherwise some material parameters like clarcoat will not work properly.
  ui_state['defaultMtl'].envMap = scene.environment;

  const camera = new THREE.PerspectiveCamera(45, window.innerWidth / window.innerHeight, 0.1, 1000);
  // Set initial camera position rotated 30 degrees around Y-axis (up axis) and 45 degrees vertical
  const horizontalAngle = 30 * (Math.PI / 180); // Convert 30 degrees to radians
  const verticalAngle = 15 * (Math.PI / 180);   // Convert 45 degrees to radians
  const distance = 10;
  camera.position.set(
    Math.sin(horizontalAngle) * Math.cos(verticalAngle) * distance, // X position
    Math.sin(verticalAngle) * distance,                             // Y position (elevated)
    Math.cos(horizontalAngle) * Math.cos(verticalAngle) * distance  // Z position
  );
  ui_state['camera'] = camera;

  const renderer = new THREE.WebGLRenderer({
    preserveDrawingBuffer: true, // for screenshot
    alpha: true, // Enable transparency
    antialias: true
  });
  renderer.setSize(window.innerWidth, window.innerHeight);
  ui_state['renderer'] = renderer; // Store renderer in ui_state
  document.body.appendChild(renderer.domElement);

  // Initialize OrbitControls
  const controls = new OrbitControls(camera, renderer.domElement);
  controls.target.set(0, 0, 0);
  controls.enableDamping = true;
  controls.dampingFactor = 0.05;
  
  // Configure mouse controls
  // Left mouse button: rotate
  controls.mouseButtons = {
    LEFT: THREE.MOUSE.ROTATE,
    MIDDLE: THREE.MOUSE.DOLLY,
    RIGHT: THREE.MOUSE.PAN
  };
  
  controls.update();
  ui_state['controls'] = controls;


  const transformControls = createTransformControlsHelper(camera, renderer);

  // Create selection box for region selection
  ui_state['selectionBox'] = createSelectionBox();

  // Add mouse event listeners for both single click selection and region selection
  renderer.domElement.addEventListener('mousedown', onMouseDown);
  renderer.domElement.addEventListener('mousemove', onMouseMove);
  renderer.domElement.addEventListener('mouseup', onMouseUp);
  renderer.domElement.addEventListener('click', onMouseClick);
  
  // Add keyboard event listeners for gizmo mode switching
  window.addEventListener('keydown', onKeyDown);

  const rootNodes = await loadScenes();

  ui_state['threeNodes'] = []

  for (const rootNode of rootNodes) {
    // Create a wrapper group to handle the Y-up axis conversion
    const wrapperGroup = new THREE.Group();
    wrapperGroup.name = 'USD_Asset_Wrapper';
    
    // Apply the Y-up axis rotation to the wrapper instead of the root node
    wrapperGroup.rotation.x = -Math.PI / 2; // Rotate to match Y-up axis
    
    // Add the USD root node to the wrapper (keeping its original transform)
    wrapperGroup.add(rootNode);
    
    // Add the wrapper to the scene instead of the root node directly
    scene.add(wrapperGroup);

    ui_state['threeNodes'].push(wrapperGroup); // Store wrapper instead of rootNode
  }

  // Apply specularColor = 0 to all MeshPhysicalMaterials in the initial scene
  scene.traverse((object) => {
    if (object.material) {
      if (Array.isArray(object.material)) {
        object.material.forEach((material) => {
          if (material.isMeshPhysicalMaterial && material.specularColor !== undefined) {
            material.specularColor.setRGB(0, 0, 0);
            material.needsUpdate = true;
          }
          // Apply debug material parameters
          if (ui_state['debugMaterial'].enabled) {
            applyDebugMaterialParams(material);
          }
        });
      } else {
        if (object.material.isMeshPhysicalMaterial && object.material.specularColor !== undefined) {
          object.material.specularColor.setRGB(0, 0, 0);
          object.material.needsUpdate = true;
        }
        // Apply debug material parameters
        if (ui_state['debugMaterial'].enabled) {
          applyDebugMaterialParams(object.material);
        }
      }
    }
  });


  // Update the scene and ensure all transforms are applied before fitting
  //scene.updateMatrixWorld(true);

  // Add to scene - TransformControls extends Object3D so this should work
  //try {
  //} catch (error) {
  //  console.error('Error adding TransformControls to scene:', error);
  //}
  
  // Read material parameters from the loaded scene
  readMaterialParamsFromScene();
  
  // Use requestAnimationFrame to ensure the scene is fully rendered before fitting
  requestAnimationFrame(() => {

    fitToScene();

    scene.add(transformControls.getHelper());

    if (ui_state['threeNodes'].length > 0) {
      //transformControls.attach(ui_state['threeNodes'][0]); // Attach to the first root node by default
    }
    console.log(transformControls.getHelper());
    //scene.add(transformControls.getHelper());
    console.log('TransformControls successfully added to scene');

  });

  function animate() {

    const threeNodes = ui_state['threeNodes'];
    if (ui_state['enable_rotation']) {
      for (const rootNode of threeNodes) {
        rootNode.rotation.z += 0.01 * ui_state['rot_scale'];
        //rootNode.rotation.x += 0.02 * ui_state['rot_scale'];
      }
    }

    // Update camera controls
    const controls = ui_state['controls'];
    if (controls) {
      controls.update();
    }
    
    // Ensure TransformControls stays attached in region selection mode
    if (ui_state['regionSelectionEnabled'] && ui_state['selectedObjects'].length > 0) {
      const transformControls = ui_state['transformControls'];
      const selectedObject = ui_state['selectedObject'];
      
      if (transformControls && selectedObject && !transformControls.object) {
        console.log('Re-attaching TransformControls in region selection mode');
        transformControls.attach(selectedObject);
      }
    }

    if (ui_state['needsMtlUpdate']) {

      // TODO: Cache materials in the scene.
      scene.traverse((object) => {
        if (object.material) {
          if (Array.isArray(object.material)) {
            object.material.forEach((material) => {
              if (Object.prototype.hasOwnProperty.call(material, 'envMapIntensity')) {
                material.envMapIntensity = ui_state['envMapIntensity'];
                material.needsUpdate = true;
              }
              // Set specularColor to 0 for MeshPhysicalMaterial
              if (material.isMeshPhysicalMaterial && material.specularColor !== undefined) {
                material.specularColor.setRGB(0, 0, 0);
                material.needsUpdate = true;
              }
              // Apply debug material parameters
              if (ui_state['debugMaterial'].enabled) {
                applyDebugMaterialParams(material);
              }
            });
          } else {
            if (Object.prototype.hasOwnProperty.call(object.material, 'envMapIntensity')) {
              object.material.envMapIntensity = ui_state['envMapIntensity'];
              object.material.needsUpdate = true;
            }
            // Set specularColor to 0 for MeshPhysicalMaterial
            if (object.material.isMeshPhysicalMaterial && object.material.specularColor !== undefined) {
              object.material.specularColor.setRGB(0, 0, 0);
              object.material.needsUpdate = true;
            }
            // Apply debug material parameters
            if (ui_state['debugMaterial'].enabled) {
              applyDebugMaterialParams(object.material);
            }
          }
        }
      });

      ui_state['needsMtlUpdate'] = false;
    }

    renderer.render(scene, camera);

  }

  renderer.setAnimationLoop(animate);
}

// Handle window resize
function onWindowResize() {
  const camera = ui_state['camera'];
  const renderer = ui_state['renderer'];
  
  if (camera && renderer) {
    camera.aspect = window.innerWidth / window.innerHeight;
    camera.updateProjectionMatrix();
    renderer.setSize(window.innerWidth, window.innerHeight);
    
    // Update transform controls if they exist
    const transformControls = ui_state['transformControls'];
    if (transformControls) {
      transformControls.handleResize();
    }
  }
}

window.addEventListener('resize', onWindowResize);

// Cleanup function for when the page is unloaded
window.addEventListener('beforeunload', () => {
  const renderer = ui_state['renderer'];
  if (renderer && renderer.domElement) {
    renderer.domElement.removeEventListener('mousedown', onMouseDown);
    renderer.domElement.removeEventListener('mousemove', onMouseMove);
    renderer.domElement.removeEventListener('mouseup', onMouseUp);
    renderer.domElement.removeEventListener('click', onMouseClick);
  }
  window.removeEventListener('keydown', onKeyDown);
  
  // Remove selection box
  const selectionBox = ui_state['selectionBox'];
  if (selectionBox && selectionBox.parentNode) {
    selectionBox.parentNode.removeChild(selectionBox);
  }
  
  // Remove status indicator
  const statusIndicator = ui_state['statusIndicator'];
  if (statusIndicator && statusIndicator.parentNode) {
    statusIndicator.parentNode.removeChild(statusIndicator);
  }
});

initScene();
