# Simple Blender Bridge Script - Run directly in Blender
# Event-driven parameter sync without polling (except viewport camera)
#
# Usage (in Blender Python console or via MCP):
#   exec(open('/path/to/bridge_simple.py').read())
#   bridge_connect()  # Connect to server
#   bridge_status()   # Check status
#   bridge_stop()     # Disconnect

import bpy
import sys

# Register as persistent module so state survives between MCP calls
_MODULE_NAME = 'tinyusdz_bridge'
if _MODULE_NAME not in sys.modules:
    import types
    sys.modules[_MODULE_NAME] = types.ModuleType(_MODULE_NAME)
_module = sys.modules[_MODULE_NAME]
import json
import struct
import socket
import threading
import queue
from bpy.app.handlers import persistent

# ============================================================================
# Configuration
# ============================================================================

BRIDGE_SERVER = "localhost"
BRIDGE_PORT = 8090
VIEWPORT_CAMERA_INTERVAL = 0.1  # 100ms - only thing that uses polling

# ============================================================================
# Global State
# ============================================================================

_bridge_state = {
    'socket': None,
    'session_id': None,
    'connected': False,
    'send_queue': queue.Queue(),
    'send_thread': None,
    'recv_thread': None,
    'msgbus_owners': [],
    'depsgraph_handler': None,
    'camera_timer_running': False,
    'prev_camera_state': None,
    'pending_updates': {},
    'batch_scheduled': False,
}

# ============================================================================
# WebSocket-like Communication (using raw TCP for simplicity in Blender)
# ============================================================================

def _create_websocket_handshake(host, port, path):
    """Create WebSocket upgrade request"""
    import base64
    import hashlib
    import os

    key = base64.b64encode(os.urandom(16)).decode()

    request = (
        f"GET {path} HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        f"Upgrade: websocket\r\n"
        f"Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        f"Sec-WebSocket-Version: 13\r\n"
        f"\r\n"
    )
    return request, key

def _send_websocket_frame(sock, data, opcode=0x02):
    """Send WebSocket binary frame"""
    if isinstance(data, str):
        data = data.encode('utf-8')
        opcode = 0x01  # text

    length = len(data)
    frame = bytearray()

    # FIN + opcode
    frame.append(0x80 | opcode)

    # Mask bit + length
    if length < 126:
        frame.append(0x80 | length)
    elif length < 65536:
        frame.append(0x80 | 126)
        frame.extend(struct.pack('>H', length))
    else:
        frame.append(0x80 | 127)
        frame.extend(struct.pack('>Q', length))

    # Masking key
    import os
    mask = os.urandom(4)
    frame.extend(mask)

    # Masked data
    masked = bytearray(data)
    for i in range(len(masked)):
        masked[i] ^= mask[i % 4]
    frame.extend(masked)

    sock.sendall(bytes(frame))

def _recv_websocket_frame(sock):
    """Receive WebSocket frame"""
    # Read first 2 bytes
    header = sock.recv(2)
    if len(header) < 2:
        return None

    fin = (header[0] & 0x80) != 0
    opcode = header[0] & 0x0F
    masked = (header[1] & 0x80) != 0
    length = header[1] & 0x7F

    if length == 126:
        length = struct.unpack('>H', sock.recv(2))[0]
    elif length == 127:
        length = struct.unpack('>Q', sock.recv(8))[0]

    if masked:
        mask = sock.recv(4)

    data = sock.recv(length)

    if masked:
        data = bytearray(data)
        for i in range(len(data)):
            data[i] ^= mask[i % 4]
        data = bytes(data)

    return {'opcode': opcode, 'data': data}

# ============================================================================
# Connection Management
# ============================================================================

def bridge_connect(server=BRIDGE_SERVER, port=BRIDGE_PORT):
    """Connect to bridge server"""
    global _bridge_state

    if _bridge_state['connected']:
        print("Already connected")
        return True

    try:
        # Create socket
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5)
        sock.connect((server, port))

        # WebSocket handshake
        path = "/?type=blender"
        request, key = _create_websocket_handshake(server, port, path)
        sock.sendall(request.encode())

        # Read response
        response = b""
        while b"\r\n\r\n" not in response:
            response += sock.recv(1024)

        if b"101" not in response:
            print("WebSocket handshake failed")
            sock.close()
            return False

        sock.settimeout(0.1)
        _bridge_state['socket'] = sock
        _bridge_state['connected'] = True

        # Receive session ID
        try:
            frame = _recv_websocket_frame(sock)
            if frame:
                msg = _decode_bridge_message(frame['data'])
                if msg and msg.get('type') == 'session_created':
                    _bridge_state['session_id'] = msg.get('sessionId')
                    print(f"Connected! Session: {_bridge_state['session_id']}")
        except socket.timeout:
            pass

        # Start send thread
        _bridge_state['send_thread'] = threading.Thread(target=_send_loop, daemon=True)
        _bridge_state['send_thread'].start()

        # Start receive thread (for browser requests)
        _bridge_state['recv_thread'] = threading.Thread(target=_recv_loop, daemon=True)
        _bridge_state['recv_thread'].start()

        # Start monitors
        _setup_msgbus_subscriptions()
        _setup_depsgraph_handler()
        _start_camera_timer()

        return True

    except Exception as e:
        print(f"Connection failed: {e}")
        return False

def bridge_stop():
    """Disconnect from bridge"""
    global _bridge_state

    _bridge_state['connected'] = False

    # Stop camera timer
    _stop_camera_timer()

    # Clear msgbus subscriptions
    for owner in _bridge_state['msgbus_owners']:
        try:
            bpy.msgbus.clear_by_owner(owner)
        except:
            pass
    _bridge_state['msgbus_owners'] = []

    # Remove depsgraph handler
    handler = _bridge_state['depsgraph_handler']
    if handler and handler in bpy.app.handlers.depsgraph_update_post:
        bpy.app.handlers.depsgraph_update_post.remove(handler)
    _bridge_state['depsgraph_handler'] = None

    # Close socket
    if _bridge_state['socket']:
        try:
            _bridge_state['socket'].close()
        except:
            pass
        _bridge_state['socket'] = None

    _bridge_state['session_id'] = None
    print("Disconnected")

def bridge_status():
    """Print bridge status"""
    if _bridge_state['connected']:
        print(f"Connected - Session: {_bridge_state['session_id']}")
    else:
        print("Disconnected")

# ============================================================================
# Message Encoding/Decoding
# ============================================================================

def _encode_bridge_message(header, payload=None):
    """Encode message with header length prefix"""
    header_json = json.dumps(header)
    header_bytes = header_json.encode('utf-8')

    if payload:
        result = struct.pack('<I', len(header_bytes)) + header_bytes + payload
    else:
        result = struct.pack('<I', len(header_bytes)) + header_bytes

    return result

def _decode_bridge_message(data):
    """Decode bridge message"""
    if len(data) < 4:
        return None

    header_len = struct.unpack('<I', data[:4])[0]
    header_json = data[4:4+header_len].decode('utf-8')
    return json.loads(header_json)

# ============================================================================
# Send Thread
# ============================================================================

def _send_loop():
    """Background thread for sending messages"""
    while _bridge_state['connected']:
        try:
            message = _bridge_state['send_queue'].get(timeout=0.1)
            sock = _bridge_state['socket']
            if sock and _bridge_state['connected']:
                encoded = _encode_bridge_message(message)
                _send_websocket_frame(sock, encoded)
        except queue.Empty:
            continue
        except Exception as e:
            print(f"Send error: {e}")
            _bridge_state['connected'] = False
            break

def _recv_loop():
    """Background thread for receiving messages from server"""
    while _bridge_state['connected']:
        try:
            sock = _bridge_state['socket']
            if not sock:
                break

            frame = _recv_websocket_frame(sock)
            if frame and frame['opcode'] in [0x01, 0x02]:  # text or binary
                msg = _decode_bridge_message(frame['data'])
                if msg:
                    _handle_incoming_message(msg)
        except socket.timeout:
            continue
        except Exception as e:
            if _bridge_state['connected']:
                print(f"Recv error: {e}")
            break

def _handle_incoming_message(msg):
    """Handle incoming message from server (browser request)"""
    msg_type = msg.get('type')
    msg_id = msg.get('messageId')

    if msg_type == 'export_request':
        # Browser is requesting a scene export
        print("Export requested by browser")
        options = msg.get('exportOptions', {})
        # Schedule export on main thread
        bpy.app.timers.register(
            lambda: _do_export_for_browser(msg_id, options),
            first_interval=0.01
        )

    elif msg_type == 'execute_code':
        # Browser is requesting code execution
        code = msg.get('code', '')
        print(f"Code execution requested: {code[:50]}...")
        bpy.app.timers.register(
            lambda: _do_execute_code(msg_id, code),
            first_interval=0.01
        )

def _do_export_for_browser(msg_id, options):
    """Execute export on main thread and send result"""
    import tempfile
    import os
    import uuid

    try:
        # Notify export started
        _queue_send({
            'type': 'export_started',
            'refMessageId': msg_id,
            'timestamp': 0
        })

        # Export to temp file
        filepath = os.path.join(tempfile.gettempdir(), 'bridge_export.usdz')

        export_options = {
            'filepath': filepath,
            'export_materials': True,
            'generate_materialx_network': options.get('materialx', True),
            'export_animation': options.get('animation', False),
        }

        # Handle Blender version differences
        try:
            # Blender 5.0+
            export_options['export_textures_mode'] = 'NEW'
        except:
            pass

        bpy.ops.wm.usd_export(**export_options)

        # Read the file
        with open(filepath, 'rb') as f:
            scene_data = f.read()

        # Send scene upload message
        header = {
            'type': 'scene_upload',
            'messageId': str(uuid.uuid4()),
            'refMessageId': msg_id,
            'timestamp': 0,
            'scene': {
                'name': bpy.context.scene.name,
                'format': 'usdz',
                'byteLength': len(scene_data),
                'hasAnimation': options.get('animation', False),
                'exportOptions': {
                    'materialx': options.get('materialx', True)
                }
            },
            'metadata': {
                'blenderVersion': bpy.app.version_string,
                'upAxis': 'Z'
            }
        }

        # Send directly (too large for queue)
        encoded = _encode_bridge_message(header, scene_data)
        _send_websocket_frame(_bridge_state['socket'], encoded)

        print(f"Scene exported and sent ({len(scene_data)} bytes)")

    except Exception as e:
        print(f"Export failed: {e}")
        _queue_send({
            'type': 'error',
            'refMessageId': msg_id,
            'error': {'code': 'EXPORT_FAILED', 'message': str(e)}
        })

    return None  # Don't repeat timer

def _do_execute_code(msg_id, code):
    """Execute Python code and send result"""
    import io
    import sys

    try:
        # Capture stdout
        old_stdout = sys.stdout
        sys.stdout = captured = io.StringIO()

        # Execute code
        exec(code, {'bpy': bpy, '__name__': '__main__'})

        output = captured.getvalue()
        sys.stdout = old_stdout

        _queue_send({
            'type': 'code_result',
            'refMessageId': msg_id,
            'success': True,
            'output': output
        })

    except Exception as e:
        sys.stdout = old_stdout
        _queue_send({
            'type': 'code_result',
            'refMessageId': msg_id,
            'success': False,
            'error': str(e)
        })

    return None  # Don't repeat timer

def _queue_send(message):
    """Queue message for sending"""
    if _bridge_state['connected']:
        _bridge_state['send_queue'].put(message)

# ============================================================================
# Update Batching
# ============================================================================

def _queue_update(target_type, path, changes):
    """Queue an update for batched sending"""
    key = f"{target_type}:{path}"

    if key not in _bridge_state['pending_updates']:
        _bridge_state['pending_updates'][key] = {
            'target': {'type': target_type, 'path': path},
            'changes': {}
        }
    _bridge_state['pending_updates'][key]['changes'].update(changes)

    # Schedule batch send
    if not _bridge_state['batch_scheduled']:
        _bridge_state['batch_scheduled'] = True
        bpy.app.timers.register(_send_batched_updates, first_interval=0.016)

def _send_batched_updates():
    """Send all pending updates"""
    import uuid

    updates = _bridge_state['pending_updates']
    _bridge_state['pending_updates'] = {}
    _bridge_state['batch_scheduled'] = False

    for key, update in updates.items():
        message = {
            'type': 'parameter_update',
            'messageId': str(uuid.uuid4()),
            'timestamp': 0,
            'target': update['target'],
            'changes': update['changes']
        }
        _queue_send(message)

    return None  # Don't repeat

# ============================================================================
# msgbus Subscriptions (Event-driven - NO polling)
# ============================================================================

def _setup_msgbus_subscriptions():
    """Subscribe to material and light property changes"""

    # Subscribe to materials
    for mat in bpy.data.materials:
        _subscribe_material(mat)

    # Subscribe to lights
    for obj in bpy.data.objects:
        if obj.type == 'LIGHT':
            _subscribe_light(obj)

    print(f"Subscribed to {len(bpy.data.materials)} materials, "
          f"{sum(1 for o in bpy.data.objects if o.type == 'LIGHT')} lights")

def _subscribe_material(material):
    """Subscribe to material property changes"""
    mat_path = f"/Materials/{material.name}"
    owner = object()
    _bridge_state['msgbus_owners'].append(owner)

    # Direct material properties
    for prop in ['diffuse_color', 'metallic', 'roughness', 'specular_intensity']:
        if hasattr(material, prop):
            try:
                bpy.msgbus.subscribe_rna(
                    key=(material, prop),
                    owner=owner,
                    args=(material.name, prop, mat_path),
                    notify=_on_material_property_change,
                )
            except:
                pass

    # Node-based materials (Principled BSDF)
    if material.use_nodes and material.node_tree:
        for node in material.node_tree.nodes:
            if node.type == 'BSDF_PRINCIPLED':
                for inp in node.inputs:
                    if inp.name in ['Base Color', 'Metallic', 'Roughness', 'IOR',
                                   'Alpha', 'Emission Color', 'Emission Strength']:
                        try:
                            bpy.msgbus.subscribe_rna(
                                key=(inp, 'default_value'),
                                owner=owner,
                                args=(material.name, inp.name, mat_path),
                                notify=_on_node_input_change,
                            )
                        except:
                            pass

def _on_material_property_change(mat_name, prop_name, mat_path):
    """Callback when material property changes (event-driven)"""
    mat = bpy.data.materials.get(mat_name)
    if not mat:
        return

    value = getattr(mat, prop_name, None)
    if value is None:
        return

    if hasattr(value, '__iter__'):
        value = [round(v, 4) for v in value]

    _queue_update('material', mat_path, {prop_name: value})

def _on_node_input_change(mat_name, input_name, mat_path):
    """Callback when node input changes (event-driven)"""
    mat = bpy.data.materials.get(mat_name)
    if not mat or not mat.use_nodes:
        return

    for node in mat.node_tree.nodes:
        if node.type == 'BSDF_PRINCIPLED':
            inp = node.inputs.get(input_name)
            if inp:
                value = inp.default_value
                if hasattr(value, '__iter__'):
                    value = [round(v, 4) for v in value]
                else:
                    value = round(value, 4)

                _queue_update('material', mat_path, {input_name: value})
                break

def _subscribe_light(light_obj):
    """Subscribe to light property changes"""
    light = light_obj.data
    light_path = f"/Lights/{light_obj.name}"
    owner = object()
    _bridge_state['msgbus_owners'].append(owner)

    for prop in ['energy', 'color', 'shadow_soft_size', 'spot_size']:
        if hasattr(light, prop):
            try:
                bpy.msgbus.subscribe_rna(
                    key=(light, prop),
                    owner=owner,
                    args=(light_obj.name, prop, light_path),
                    notify=_on_light_property_change,
                )
            except:
                pass

def _on_light_property_change(light_name, prop_name, light_path):
    """Callback when light property changes (event-driven)"""
    obj = bpy.data.objects.get(light_name)
    if not obj or obj.type != 'LIGHT':
        return

    light = obj.data
    value = getattr(light, prop_name, None)
    if value is None:
        return

    if hasattr(value, '__iter__'):
        value = [round(v, 4) for v in value]
    else:
        value = round(value, 4)

    _queue_update('light', light_path, {prop_name: value})

# ============================================================================
# Depsgraph Handler (Event-driven for transforms)
# ============================================================================

def _setup_depsgraph_handler():
    """Setup depsgraph update handler for transform changes"""

    prev_transforms = {}

    @persistent
    def on_depsgraph_update(scene, depsgraph):
        if not _bridge_state['connected']:
            return

        for update in depsgraph.updates:
            if not isinstance(update.id, bpy.types.Object):
                continue

            obj_name = update.id.name

            if update.is_updated_transform:
                obj = bpy.data.objects.get(obj_name)
                if obj:
                    loc = [round(v, 4) for v in obj.location]
                    rot = [round(v, 4) for v in obj.rotation_euler]
                    scale = [round(v, 4) for v in obj.scale]

                    current = {'location': loc, 'rotation': rot, 'scale': scale}
                    prev = prev_transforms.get(obj_name)

                    if current != prev:
                        _queue_update('transform', f"/Objects/{obj_name}", current)
                        prev_transforms[obj_name] = current

    bpy.app.handlers.depsgraph_update_post.append(on_depsgraph_update)
    _bridge_state['depsgraph_handler'] = on_depsgraph_update
    print("Depsgraph handler registered (event-driven transforms)")

# ============================================================================
# Viewport Camera Timer (Only polling component - 100ms interval)
# ============================================================================

def _start_camera_timer():
    """Start viewport camera sync timer"""
    if _bridge_state['camera_timer_running']:
        return

    _bridge_state['camera_timer_running'] = True
    bpy.app.timers.register(_camera_timer_callback, first_interval=VIEWPORT_CAMERA_INTERVAL)
    print(f"Viewport camera timer started ({VIEWPORT_CAMERA_INTERVAL*1000:.0f}ms interval)")

def _stop_camera_timer():
    """Stop viewport camera sync timer"""
    _bridge_state['camera_timer_running'] = False

def _camera_timer_callback():
    """Timer callback for viewport camera (only polling component)"""
    if not _bridge_state['connected'] or not _bridge_state['camera_timer_running']:
        return None  # Stop timer

    # Find 3D viewport
    for window in bpy.context.window_manager.windows:
        for area in window.screen.areas:
            if area.type == 'VIEW_3D':
                space = area.spaces.active
                region_3d = space.region_3d

                view_matrix = region_3d.view_matrix.inverted()
                position = view_matrix.translation
                target = region_3d.view_location

                current = {
                    'position': [round(position.x, 4), round(position.y, 4), round(position.z, 4)],
                    'target': [round(target.x, 4), round(target.y, 4), round(target.z, 4)],
                    'lens': round(space.lens, 2)
                }

                if current != _bridge_state['prev_camera_state']:
                    _queue_update('camera', '/BlenderViewport', current)
                    _bridge_state['prev_camera_state'] = current

                break
        break

    return VIEWPORT_CAMERA_INTERVAL  # Repeat

# ============================================================================
# Scene Upload
# ============================================================================

def bridge_upload_scene():
    """Export current scene as USDZ and upload to bridge"""
    import tempfile
    import os
    import uuid

    if not _bridge_state['connected']:
        print("Not connected")
        return False

    # Export to temp file
    filepath = os.path.join(tempfile.gettempdir(), 'bridge_export.usdz')

    bpy.ops.wm.usd_export(
        filepath=filepath,
        export_materials=True,
        generate_materialx_network=True
    )

    # Read and send
    with open(filepath, 'rb') as f:
        scene_data = f.read()

    header = {
        'type': 'scene_upload',
        'messageId': str(uuid.uuid4()),
        'timestamp': 0,
        'scene': {
            'name': 'BlenderScene',
            'format': 'usdz',
            'byteLength': len(scene_data)
        }
    }

    # Send via socket directly (too large for queue)
    encoded = _encode_bridge_message(header, scene_data)
    _send_websocket_frame(_bridge_state['socket'], encoded)

    print(f"Scene uploaded ({len(scene_data)} bytes)")
    return True

# ============================================================================
# Convenience Functions
# ============================================================================

def bridge_refresh_subscriptions():
    """Refresh msgbus subscriptions (after adding new objects)"""
    # Clear existing
    for owner in _bridge_state['msgbus_owners']:
        try:
            bpy.msgbus.clear_by_owner(owner)
        except:
            pass
    _bridge_state['msgbus_owners'] = []

    # Re-subscribe
    _setup_msgbus_subscriptions()
    print("Subscriptions refreshed")

# ============================================================================
# Export to persistent module (survives MCP calls)
# ============================================================================

_module.bridge_connect = bridge_connect
_module.bridge_stop = bridge_stop
_module.bridge_status = bridge_status
_module.bridge_upload_scene = bridge_upload_scene
_module.bridge_refresh_subscriptions = bridge_refresh_subscriptions
_module._bridge_state = _bridge_state

# Also export to builtins for easier access
import builtins
builtins.bridge_connect = bridge_connect
builtins.bridge_stop = bridge_stop
builtins.bridge_status = bridge_status
builtins.bridge_upload_scene = bridge_upload_scene
builtins.bridge_refresh_subscriptions = bridge_refresh_subscriptions

# Print usage
print("""
TinyUSDZ Bridge - Event-Driven (Lightweight)
============================================
Commands:
  bridge_connect()              - Connect to server
  bridge_stop()                 - Disconnect
  bridge_status()               - Check connection status
  bridge_upload_scene()         - Upload current scene
  bridge_refresh_subscriptions() - Refresh after adding objects

Event Monitoring:
  - Materials: msgbus (no polling)
  - Lights: msgbus (no polling)
  - Transforms: depsgraph handler (no polling)
  - Viewport camera: timer (100ms polling - only polling component)
""")
