import * as THREE from 'three';
import { HDRCubeTextureLoader } from 'three/addons/loaders/HDRCubeTextureLoader.js';

import { GUI } from 'https://cdn.jsdelivr.net/npm/dat.gui@0.7.9/build/dat.gui.module.js';

import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js'
import { TinyUSDZLoaderUtils } from 'tinyusdz/TinyUSDZLoaderUtils.js'

// MCP
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StreamableHTTPClientTransport } from "@modelcontextprotocol/sdk/client/streamableHttp.js";
import { SSEClientTransport } from "@modelcontextprotocol/sdk/client/sse.js";



const gui = new GUI( {width: 450});

let ui_state = {}
ui_state['rot_scale'] = 1.0;
ui_state['defaultMtl'] = TinyUSDZLoaderUtils.createDefaultMaterial();

ui_state['envMapIntensity'] = 3.14; // pi is good for pisaHDR;
ui_state['ambient'] = 0.4;
let ambientLight = new THREE.AmbientLight(0x404040, ui_state['ambient']);
ui_state['camera_z'] = 3.14; // TODO: Compute best fit from scene's bbox.
ui_state['needsMtlUpdate'] = false;
ui_state['renderer'] = null;
ui_state['mcpServer'] = "http://localhost:8085/mcp"; // MCP server URL
ui_state['mcpServerConnected'] = "Not connected";
ui_state['mcpClient'] = null;

ui_state['screenshot'] = null;


// Create a parameters object
const params = {
  envMapIntensity: ui_state['envMapIntensity'],
  rotationSpeed: ui_state['rot_scale'],
  camera_z: ui_state['camera_z'],
  mcpServer: ui_state['mcpServer'],
  connectMcpServer: connectMCPServer,
  mcpServerConnected: ui_state['mcpServerConnected'],
  take_screenshot: takeScreenshot,
  send_screenshot_to_mcp: sendScreenshotToMCP
};

// Add controls
gui.add(params, 'envMapIntensity', 0, 20, 0.1).name('envMapIntensity').onChange((value) => {
  ui_state['envMapIntensity'] = value;
  ui_state['needsMtlUpdate'] = true;
  
});
gui.add(params, 'camera_z', 0, 20).name('Camera Z').onChange((value) => {
  ui_state['camera_z'] = value;
});
gui.add(params, 'rotationSpeed', 0, 10).name('Rotation Speed').onChange((value) => {
  ui_state['rot_scale'] = value;
});

gui.add(params, 'mcpServer').name('MCP Server URL').onChange((value) => {
  ui_state['mcpServer'] = value;
});
gui.add(params, 'connectMcpServer').name('Connect MCP Server');
gui.add(params, 'mcpServerConnected').name('MCP Server Connected').listen();
gui.add(params, 'take_screenshot').name('Take Screenshot');
gui.add(params, 'send_screenshot_to_mcp').name('Send screenshot to MCP');


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

async function loadScenes() {

  const loader = new TinyUSDZLoader();

  // it is recommended to call init() before loadAsync()
  // (wait loading/compiling wasm module in the early stage))
  await loader.init();

  // Use zstd compressed tinyusdz.wasm to save the bandwidth.
  //await loader.init({useZstdCompressedWasm: true});

  const suzanne_filename = "./assets/suzanne-pbr.usda";
  const texcat_filename = "./assets/texture-cat-plane.usdz";
  const cookie_filename = "./assets/UsdCookie.usdz";
  const usd_filename = "./assets/suzanne-pbr.usda";

  var threeScenes = []

  const usd_scenes = await Promise.all([
    //loader.loadAsync(texcat_filename),
    loader.loadAsync(usd_filename),
    //loader.loadAsync(suzanne_filename),
  ]);

  const defaultMtl = ui_state['defaultMtl'];

  const options = {
    overrideMaterial: false, // override USD material with defaultMtl(default 'false')
    envMap: defaultMtl.envMap, // reuse envmap from defaultMtl
    envMapIntensity: ui_state['envMapIntensity'], // default envmap intensity
  }

  var offset = -(usd_scenes.length-1) * 1.5;
  for (const usd_scene of usd_scenes) {

    const usdRootNode = usd_scene.getDefaultRootNode();

    const threeNode = TinyUSDZLoaderUtils.buildThreeNode(usdRootNode, defaultMtl, usd_scene, options); 

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



const scene = new THREE.Scene();

async function initScene() {

  const envmap = await new HDRCubeTextureLoader()
    .setPath( 'assets/textures/cube/pisaHDR/' )
    .loadAsync( [ 'px.hdr', 'nx.hdr', 'py.hdr', 'ny.hdr', 'pz.hdr', 'nz.hdr' ] )
  scene.background = envmap;
  scene.environment = envmap;

  // Assign envmap to material
  // Otherwise some material parameters like clarcoat will not work properly.
  ui_state['defaultMtl'].envMap = envmap;

  const camera = new THREE.PerspectiveCamera(75, window.innerWidth / window.innerHeight, 0.1, 1000);
  camera.position.z = ui_state['camera_z'];

  const renderer = new THREE.WebGLRenderer({
    preserveDrawingBuffer: true, // for screenshot
    alpha: true, // Enable transparency
    antialias: true
  });
  renderer.setSize(window.innerWidth, window.innerHeight);
  ui_state['renderer'] = renderer; // Store renderer in ui_state
  document.body.appendChild(renderer.domElement);

  const rootNodes = await loadScenes();

  for (const rootNode of rootNodes) {

    // HACK. upAxis
    rootNode.rotation.x = -Math.PI / 2; // Rotate to match Y-up axis
    rootNode.rotation.z = Math.PI/2; // Rotate to match Y-up axis
    scene.add(rootNode);
  }

  function animate() {

    for (const rootNode of rootNodes) {
      rootNode.rotation.z += 0.01 * ui_state['rot_scale'];
      //rootNode.rotation.x += 0.02 * ui_state['rot_scale'];
    }

    camera.position.z = ui_state['camera_z'];

    if (ui_state['needsMtlUpdate']) {

      // TODO: Cache materials in the scene.
      scene.traverse((object) => {
        if (object.material) {
          if (Object.prototype.hasOwnProperty.call(object.material, 'envMapIntensity')) {
            object.material.envMapIntensity = ui_state['envMapIntensity'];
            object.material.needsUpdate = true;
          }
        }
      });

      ui_state['needsMtlUpdate'] = false;
    }

    renderer.render(scene, camera);

  }

  renderer.setAnimationLoop(animate);
}

initScene();
