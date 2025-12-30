# Blender Bridge Setup Guide

Step-by-step instructions to set up and run the Blender Bridge for streaming USD scenes to a browser viewer.

## Prerequisites

- Node.js v18+
- Blender 4.0+ with USD export support
- TinyUSDZ WASM build (in `web/js/src/tinyusdz/`)

## Directory Structure

```
web/blender-bridge/
├── package.json
├── server.js              # WebSocket server (port 8090)
├── test-client.js         # Test client for debugging
├── vite.config.js         # Vite dev server config
├── lib/
│   ├── connection-manager.js
│   ├── message-protocol.js
│   └── scene-state.js
├── client/
│   ├── bridge-client.js
│   └── parameter-sync.js
└── viewer/
    ├── index.html
    ├── viewer.js
    ├── viewer.css
    ├── tinyusdz/          # Copied from web/js/src/tinyusdz/
    └── client/            # Copied from ../client/
```

## Setup Steps

### 1. Install Dependencies

```bash
cd web/blender-bridge
npm install
```

### 2. Copy TinyUSDZ WASM Files

Copy the WASM files to the viewer directory (symlinks don't work in browsers):

```bash
cd viewer
mkdir -p tinyusdz
cp ../../js/src/tinyusdz/*.js tinyusdz/
cp ../../js/src/tinyusdz/*.wasm tinyusdz/
```

### 3. Copy Client Files

```bash
mkdir -p client
cp ../client/*.js client/
```

### 4. Start the WebSocket Server

```bash
cd web/blender-bridge
node server.js
```

Server runs at:
- HTTP: http://localhost:8090
- WebSocket: ws://localhost:8090
- Status endpoint: http://localhost:8090/status

### 5. Start the Viewer Dev Server

In a new terminal:

```bash
cd web/blender-bridge
npx vite viewer --port 5173
```

Viewer available at: http://localhost:5173

### 6. Export Scene from Blender

Using Blender MCP or Python console:

```python
import bpy
import os
import tempfile

# Export USDZ with MaterialX
export_path = os.path.join(tempfile.gettempdir(), 'blender_bridge_test.usdz')
bpy.ops.wm.usd_export(
    filepath=export_path,
    export_materials=True,
    generate_materialx_network=True
)
print(f"Exported to: {export_path}")
```

### 7. Send Scene via Test Client

```bash
cd web/blender-bridge
node test-client.js
```

Output will show:
```
========================================
SESSION ID: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
========================================
```

### 8. Connect Browser Viewer

1. Open http://localhost:5173
2. Enter the Session ID from step 7
3. Click "Connect"

The scene should load and display in the viewer.

## Verification

Check server status:
```bash
curl http://localhost:8090/status
```

Check active sessions:
```bash
curl http://localhost:8090/sessions
```

Expected output when connected:
```json
{
  "sessions": [{
    "sessionId": "...",
    "hasBlender": true,
    "browserCount": 1,
    "hasScene": true,
    "sceneName": "BlenderScene"
  }]
}
```

## Troubleshooting

### WASM MIME Type Error
If you see "Response has unsupported MIME type '' expected 'application/wasm'":
- Ensure WASM files are copied (not symlinked) to `viewer/tinyusdz/`
- The vite config includes the `wasmMimePlugin()`

### Port Already in Use
```bash
fuser -k 5173/tcp  # Kill process on port 5173
fuser -k 8090/tcp  # Kill process on port 8090
```

### Session Not Found
- Ensure the WebSocket server is running (`node server.js`)
- Ensure the test client is connected before browser connects
- Check session ID matches exactly

## Architecture

```
┌─────────────┐     WebSocket      ┌─────────────┐     WebSocket     ┌─────────────┐
│   Blender   │ ──────────────────>│   Server    │<─────────────────>│   Browser   │
│ (test-client│    Scene Upload    │  (8090)     │   Scene Data      │   Viewer    │
│  or MCP)    │    Param Updates   │             │   Param Updates   │   (5173)    │
└─────────────┘                    └─────────────┘                   └─────────────┘
                                         │
                                         v
                                   Scene State
                                   (in memory)
```

## Headless Chrome Testing (via Chrome DevTools MCP)

You can automate browser interaction using Chrome DevTools MCP:

```javascript
// 1. Open viewer page
mcp__chrome-devtools__new_page({ url: "http://localhost:5173" })

// 2. Fill session ID
mcp__chrome-devtools__fill({ uid: "<session-id-input-uid>", value: "<SESSION_ID>" })

// 3. Click Connect
mcp__chrome-devtools__click({ uid: "<connect-button-uid>" })

// 4. Take screenshot to verify
mcp__chrome-devtools__take_screenshot()
```

## Camera Sync

Send Blender's viewport camera to the browser viewer:

### Get Camera from Blender (via MCP or Python console)

```python
import bpy
import json

for area in bpy.context.screen.areas:
    if area.type == 'VIEW_3D':
        space = area.spaces.active
        region_3d = space.region_3d
        view_matrix = region_3d.view_matrix.inverted()
        cam_pos = view_matrix.translation
        target = region_3d.view_location

        camera_data = {
            "position": [cam_pos.x, cam_pos.y, cam_pos.z],
            "target": [target.x, target.y, target.z],
            "lens": space.lens
        }
        print(json.dumps(camera_data))
        break
```

### Send Camera to Browser

```bash
node send-camera.js "<SESSION_ID>" '{"position": [15.0, -6.7, 8.2], "target": [0, 0, 0], "lens": 50}'
```

The browser viewer will:
1. Convert Blender Z-up coordinates to Three.js Y-up
2. Apply position, target, and FOV to the camera
3. Update OrbitControls

### Fit to Scene Button

The browser UI includes a "Fit to Scene" button that:
- Calculates the scene bounding box
- Positions camera to view the entire scene
- Works independently of Blender camera sync

## Message Flow

1. **Blender connects** → Server creates session → Returns session ID
2. **Blender uploads scene** → Server stores scene → Broadcasts to browsers
3. **Browser connects** → Server syncs current scene → Browser renders
4. **Blender updates params** → Server broadcasts → Browser applies changes
5. **Camera sync** → send-camera.js joins session → Sends camera update → Browser applies
