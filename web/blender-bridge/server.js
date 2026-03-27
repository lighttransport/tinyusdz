// Blender Bridge WebSocket Server
// Streams Blender scenes to browser viewers via TinyUSDZ

import { WebSocketServer } from 'ws';
import express from 'express';
import { createServer } from 'http';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
import { ConnectionManager, ClientType } from './lib/connection-manager.js';
import { SceneState } from './lib/scene-state.js';
import {
  decodeMessage,
  encodeMessage,
  createSceneUploadMessage,
  createParameterUpdateMessage,
  createAckMessage,
  createErrorMessage,
  createPongMessage,
  createExportRequestMessage,
  MessageType,
  TargetType
} from './lib/message-protocol.js';

// Configuration
const PORT = process.env.BLENDER_BRIDGE_PORT || 8090;
const HOST = process.env.BLENDER_BRIDGE_HOST || '0.0.0.0';

// Initialize managers
const connectionManager = new ConnectionManager();
const sceneStates = new Map(); // sessionId -> SceneState

// Express app for HTTP endpoints
const app = express();
app.use(express.json({ limit: '50mb' }));

// CORS headers
app.use((req, res, next) => {
  res.header('Access-Control-Allow-Origin', '*');
  res.header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
  res.header('Access-Control-Allow-Headers', 'Content-Type');
  next();
});

// HTTP endpoint: Get server status
app.get('/status', (req, res) => {
  res.json({
    status: 'running',
    ...connectionManager.getStats()
  });
});

// HTTP endpoint: List sessions
app.get('/sessions', (req, res) => {
  const sessions = connectionManager.getAllSessionIds().map(sessionId => {
    const session = connectionManager.getSession(sessionId);
    const sceneState = sceneStates.get(sessionId);
    return {
      sessionId,
      hasBlender: !!session?.blenderClient,
      browserCount: session?.browserCount || 0,
      hasScene: sceneState?.hasScene() || false,
      sceneName: sceneState?.metadata?.name || null
    };
  });
  res.json({ sessions });
});

// HTTP endpoint: Serve Blender bridge Python script
app.get('/blender/bridge.py', (req, res) => {
  const scriptPath = path.join(__dirname, 'blender', 'bridge_simple.py');
  res.setHeader('Content-Type', 'text/x-python');
  res.setHeader('Content-Disposition', 'inline; filename="bridge_simple.py"');

  if (fs.existsSync(scriptPath)) {
    res.send(fs.readFileSync(scriptPath, 'utf-8'));
  } else {
    res.status(404).send('# Script not found');
  }
});

// HTTP endpoint: Bootstrap script (one-liner for remote Blender)
app.get('/blender/bootstrap', (req, res) => {
  const serverUrl = req.query.server || req.headers.host || 'localhost:8090';
  const [serverHost, serverPort] = serverUrl.includes(':')
    ? serverUrl.split(':')
    : [serverUrl, '8090'];

  res.setHeader('Content-Type', 'text/x-python');

  // Generate minimal bootstrap script that fetches and runs the full script
  const bootstrap = `# TinyUSDZ Bridge Bootstrap - Run this in Blender
# Fetches and executes the bridge script from the server

import urllib.request
import ssl

SERVER = "${serverHost}"
PORT = ${serverPort}

# Fetch the bridge script
ctx = ssl.create_default_context()
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE

url = f"http://{SERVER}:{PORT}/blender/bridge.py"
print(f"Fetching bridge script from {url}...")

try:
    with urllib.request.urlopen(url, timeout=10, context=ctx) as response:
        script = response.read().decode('utf-8')
    exec(script)
    print("Bridge script loaded!")
    bridge_connect(server=SERVER, port=PORT)
except Exception as e:
    print(f"Failed to fetch bridge script: {e}")
`;

  res.send(bootstrap);
});

// HTTP endpoint: Upload scene (alternative to WebSocket for large files)
app.post('/upload/:sessionId', (req, res) => {
  const { sessionId } = req.params;
  const session = connectionManager.getSession(sessionId);

  if (!session) {
    return res.status(404).json({ error: 'Session not found' });
  }

  // Handle base64 encoded USDZ
  const { data, options } = req.body;
  if (!data) {
    return res.status(400).json({ error: 'Missing data field' });
  }

  try {
    const binaryData = Buffer.from(data, 'base64');
    handleSceneUpload(sessionId, { scene: options || {} }, binaryData);
    res.json({ success: true, byteLength: binaryData.length });
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// Create HTTP server
const server = createServer(app);

// Create WebSocket server
const wss = new WebSocketServer({ server });

console.log(`Blender Bridge Server starting on ${HOST}:${PORT}`);

// WebSocket connection handler
wss.on('connection', (ws, req) => {
  const url = new URL(req.url, `http://${req.headers.host}`);
  const clientType = url.searchParams.get('type') || 'browser';
  const sessionId = url.searchParams.get('session');

  let clientId = null;
  let assignedSessionId = null;

  if (clientType === 'blender') {
    // Blender client - join existing session or create new one
    if (sessionId) {
      // Join existing session as Blender client
      const session = connectionManager.getSession(sessionId);
      if (session) {
        assignedSessionId = sessionId;
        clientId = sessionId;
        session.blender = ws;
        console.log(`Blender joined existing session: ${sessionId}`);
      } else {
        // Session doesn't exist, close connection
        ws.close(4002, 'Session not found');
        return;
      }
    } else {
      // Create new session
      assignedSessionId = connectionManager.registerBlender(ws, {
        userAgent: req.headers['user-agent']
      });
      clientId = assignedSessionId;

      // Initialize scene state for this session
      sceneStates.set(assignedSessionId, new SceneState());

      // Send session ID to Blender
      ws.send(encodeMessage({
        type: 'session_created',
        sessionId: assignedSessionId
      }));
    }

    console.log(`Blender connected: session=${assignedSessionId}`);
  } else {
    // Browser client joins existing session
    if (!sessionId) {
      ws.close(4001, 'Missing session parameter');
      return;
    }

    clientId = connectionManager.registerBrowser(ws, sessionId, {
      userAgent: req.headers['user-agent']
    });

    if (!clientId) {
      ws.close(4002, 'Session not found');
      return;
    }

    assignedSessionId = sessionId;
    console.log(`Browser connected: session=${sessionId}, client=${clientId}`);

    // Sync current scene state to new browser
    syncSceneToClient(ws, sessionId);
  }

  // Message handler
  ws.on('message', (data, isBinary) => {
    try {
      const { header, payload } = decodeMessage(data);
      handleMessage(ws, clientId, assignedSessionId, clientType, header, payload);
    } catch (err) {
      console.error(`Message decode error: ${err.message}`);
      ws.send(createErrorMessage(null, 'DECODE_ERROR', err.message));
    }
  });

  // Close handler
  ws.on('close', (code, reason) => {
    console.log(`Client disconnected: ${clientId}, code=${code}`);
    connectionManager.unregister(clientId);

    // Clean up scene state if session is removed
    const session = connectionManager.getSession(assignedSessionId);
    if (!session) {
      sceneStates.delete(assignedSessionId);
    }
  });

  // Error handler
  ws.on('error', (err) => {
    console.error(`WebSocket error for ${clientId}: ${err.message}`);
  });
});

/**
 * Handle incoming message
 */
function handleMessage(ws, clientId, sessionId, clientType, header, payload) {
  const messageType = header.type;

  switch (messageType) {
    case MessageType.SCENE_UPLOAD:
      if (clientType === 'blender') {
        handleSceneUpload(sessionId, header, payload);
        ws.send(createAckMessage(header.messageId, 'success'));
      }
      break;

    case MessageType.PARAMETER_UPDATE:
      if (clientType === 'blender') {
        handleParameterUpdate(sessionId, header);
        ws.send(createAckMessage(header.messageId, 'success'));
      }
      break;

    case MessageType.ACK:
      // Browser acknowledged a message
      console.log(`ACK received from ${clientId}: ref=${header.refMessageId}, status=${header.status}`);
      break;

    case MessageType.STATUS:
      // Browser status update
      console.log(`Status from ${clientId}:`, header.viewer);
      // Forward to Blender if needed
      if (header.forwardToBlender) {
        connectionManager.sendToBlender(sessionId, encodeMessage(header));
      }
      break;

    case MessageType.ERROR:
      console.error(`Error from ${clientId}:`, header.error);
      // Forward to Blender
      connectionManager.sendToBlender(sessionId, encodeMessage(header));
      break;

    case MessageType.PING:
      ws.send(createPongMessage(header.messageId));
      break;

    case MessageType.PONG:
      connectionManager.handlePong(clientId);
      break;

    case MessageType.EXPORT_REQUEST:
      // Browser requesting scene export from Blender
      if (clientType === 'browser') {
        console.log(`Export request from browser: session=${sessionId}`);
        // Forward to Blender
        const exportMsg = encodeMessage({
          type: MessageType.EXPORT_REQUEST,
          messageId: header.messageId,
          exportOptions: header.exportOptions || {}
        });
        const sent = connectionManager.sendToBlender(sessionId, exportMsg);
        if (!sent) {
          ws.send(createErrorMessage(header.messageId, 'NO_BLENDER', 'Blender not connected to this session'));
        }
      }
      break;

    case MessageType.EXECUTE_CODE:
      // Browser requesting code execution in Blender
      if (clientType === 'browser') {
        console.log(`Code execution request from browser: session=${sessionId}`);
        const codeMsg = encodeMessage({
          type: MessageType.EXECUTE_CODE,
          messageId: header.messageId,
          code: header.code
        });
        const sent = connectionManager.sendToBlender(sessionId, codeMsg);
        if (!sent) {
          ws.send(createErrorMessage(header.messageId, 'NO_BLENDER', 'Blender not connected to this session'));
        }
      }
      break;

    case MessageType.EXPORT_STARTED:
    case MessageType.EXPORT_PROGRESS:
    case MessageType.CODE_RESULT:
      // Blender -> Browser responses, forward to browsers
      if (clientType === 'blender') {
        connectionManager.broadcastToBrowsers(sessionId, encodeMessage(header, payload));
      }
      break;

    default:
      console.warn(`Unknown message type: ${messageType}`);
  }
}

/**
 * Handle scene upload from Blender
 */
function handleSceneUpload(sessionId, header, payload) {
  console.log(`Scene upload: session=${sessionId}, size=${payload.length}`);

  // Store scene state
  const sceneState = sceneStates.get(sessionId);
  if (sceneState) {
    sceneState.setScene(header.scene, payload);
  }

  // Create message with binary payload
  const message = createSceneUploadMessage(payload, {
    name: header.scene?.name,
    hasAnimation: header.scene?.hasAnimation,
    materialx: header.scene?.exportOptions?.materialx,
    rootPrimPath: header.scene?.exportOptions?.rootPrimPath,
    blenderVersion: header.metadata?.blenderVersion,
    upAxis: header.metadata?.upAxis
  });

  // Broadcast to all browsers
  connectionManager.broadcastToBrowsers(sessionId, message);
}

/**
 * Handle parameter update from Blender
 */
function handleParameterUpdate(sessionId, header) {
  const { target, changes } = header;
  console.log(`Parameter update: session=${sessionId}, target=${target.path}, type=${target.type}`);

  // Update scene state and compute delta
  const sceneState = sceneStates.get(sessionId);
  let delta = changes;

  if (sceneState) {
    switch (target.type) {
      case TargetType.MATERIAL:
        delta = sceneState.updateMaterial(target.path, changes);
        break;
      case TargetType.LIGHT:
        delta = sceneState.updateLight(target.path, changes);
        break;
      case TargetType.CAMERA:
        delta = sceneState.updateCamera(target.path, changes);
        break;
      case TargetType.TRANSFORM:
        delta = sceneState.updateTransform(target.path, changes);
        break;
    }
  }

  // Only broadcast if there are actual changes
  if (Object.keys(delta).length > 0) {
    const message = createParameterUpdateMessage(target.type, target.path, delta, {
      name: target.name,
      lightType: target.lightType
    });
    connectionManager.broadcastToBrowsers(sessionId, message);
  }
}

/**
 * Sync scene state to a newly connected browser
 */
function syncSceneToClient(ws, sessionId) {
  const sceneState = sceneStates.get(sessionId);
  if (!sceneState || !sceneState.hasScene()) {
    console.log(`No scene to sync for session ${sessionId}`);
    return;
  }

  console.log(`Syncing scene to new browser client: session=${sessionId}`);

  // Send scene
  const { sceneInfo, binaryData } = sceneState.getSceneForSync();
  const sceneMessage = createSceneUploadMessage(binaryData, sceneInfo);
  ws.send(sceneMessage);

  // Send all parameter updates
  const params = sceneState.getAllParametersForSync();

  // Sync materials
  for (const [path, changes] of Object.entries(params.materials)) {
    const msg = createParameterUpdateMessage(TargetType.MATERIAL, path, changes);
    ws.send(msg);
  }

  // Sync lights
  for (const [path, changes] of Object.entries(params.lights)) {
    const msg = createParameterUpdateMessage(TargetType.LIGHT, path, changes);
    ws.send(msg);
  }

  // Sync cameras
  for (const [path, changes] of Object.entries(params.cameras)) {
    const msg = createParameterUpdateMessage(TargetType.CAMERA, path, changes);
    ws.send(msg);
  }

  // Sync transforms
  for (const [path, changes] of Object.entries(params.transforms)) {
    const msg = createParameterUpdateMessage(TargetType.TRANSFORM, path, changes);
    ws.send(msg);
  }
}

// Start server
server.listen(PORT, HOST, () => {
  console.log(`Blender Bridge Server running at http://${HOST}:${PORT}`);
  console.log(`WebSocket endpoint: ws://${HOST}:${PORT}`);
  console.log(`HTTP status: http://${HOST}:${PORT}/status`);

  // Start heartbeat monitoring
  connectionManager.startHeartbeat();
});

// Graceful shutdown
process.on('SIGINT', () => {
  console.log('\nShutting down...');
  connectionManager.stopHeartbeat();
  wss.close(() => {
    server.close(() => {
      console.log('Server closed');
      process.exit(0);
    });
  });
});
