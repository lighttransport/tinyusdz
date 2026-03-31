# Blender Bridge

WebSocket bridge for streaming Blender scenes to a browser viewer with real-time parameter synchronization.

## Architecture

```
Blender (MCP) ──WebSocket──> Node.js Server ──WebSocket──> Browser Viewer
                (8090)                         (TinyUSDZ WASM + Three.js)
```

## Quick Start

### 1. Install Dependencies

```bash
cd web/blender-bridge
npm install
```

### 2. Start the WebSocket Server

```bash
npm run server
# Server runs at ws://localhost:8090
```

### 3. Start the Browser Viewer

```bash
npm run dev
# Opens http://localhost:5174
```

### 4. Connect from Blender

Using Blender MCP, execute:

```python
import bpy
import websocket
import os
import tempfile

# Export scene to USDZ
export_path = os.path.join(tempfile.gettempdir(), 'bridge_scene.usdz')
bpy.ops.wm.usd_export(
    filepath=export_path,
    export_materials=True,
    generate_materialx_network=True
)

# Read binary data
with open(export_path, 'rb') as f:
    usdz_data = f.read()

# Connect and send
ws = websocket.create_connection('ws://localhost:8090?type=blender')
# Session ID is returned in response
# Use session ID to connect browser viewers
```

## Message Protocol

### Scene Upload (Blender → Browser)

```json
{
  "type": "scene_upload",
  "messageId": "uuid",
  "scene": {
    "name": "SceneName",
    "format": "usdz",
    "byteLength": 1048576
  }
}
// + Binary payload: USDZ data
```

### Parameter Update (Blender → Browser)

```json
{
  "type": "parameter_update",
  "target": {
    "type": "material",  // or "light", "camera", "transform"
    "path": "/World/Sphere/Material"
  },
  "changes": {
    "base_color": [0.9, 0.7, 0.3],
    "base_metalness": 0.85
  }
}
```

### Acknowledgment (Browser → Blender)

```json
{
  "type": "ack",
  "refMessageId": "uuid",
  "status": "success"
}
```

## HTTP Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/status` | GET | Server status and stats |
| `/sessions` | GET | List active sessions |
| `/upload/:sessionId` | POST | Upload scene via HTTP (base64) |

## Supported Parameters

### Materials (OpenPBR / USD Preview Surface)

| Parameter | Three.js Mapping |
|-----------|-----------------|
| `base_color` | `material.color` |
| `base_metalness` | `material.metalness` |
| `specular_roughness` | `material.roughness` |
| `specular_ior` | `material.ior` |
| `coat_weight` | `material.clearcoat` |
| `transmission_weight` | `material.transmission` |
| `emission_color` | `material.emissive` |

### Lights

| Parameter | Three.js Mapping |
|-----------|-----------------|
| `color` | `light.color` |
| `intensity` | `light.intensity` |
| `position` | `light.position` |
| `angle` | `light.angle` (SpotLight) |
| `width/height` | `light.width/height` (RectAreaLight) |

### Camera

| Parameter | Three.js Mapping |
|-----------|-----------------|
| `position` | `camera.position` |
| `target` | `controls.target` |
| `fov` | `camera.fov` |

### Transform

| Parameter | Three.js Mapping |
|-----------|-----------------|
| `position` | `object.position` |
| `rotation` | `object.rotation` |
| `scale` | `object.scale` |
| `matrix` | `object.matrix` |

## File Structure

```
blender-bridge/
├── package.json
├── server.js                 # WebSocket server
├── vite.config.js            # Viewer dev config
├── lib/
│   ├── connection-manager.js # Session management
│   ├── message-protocol.js   # Message encode/decode
│   └── scene-state.js        # Server-side state
├── client/
│   ├── bridge-client.js      # Browser WebSocket client
│   └── parameter-sync.js     # Parameter mapping
└── viewer/
    ├── index.html
    ├── viewer.js
    └── viewer.css
```

## Development

### Testing Server

```bash
# Check server status
curl http://localhost:8090/status

# List sessions
curl http://localhost:8090/sessions
```

### Debugging

Enable verbose logging by checking the browser console and the message log panel in the viewer UI.
