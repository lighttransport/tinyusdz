# Blender Bridge Addon - Event-Driven Parameter Sync
# Uses msgbus for UI changes, depsgraph for transforms, timer only for viewport camera

bl_info = {
    "name": "TinyUSDZ Bridge",
    "author": "TinyUSDZ",
    "version": (1, 0, 0),
    "blender": (4, 0, 0),
    "location": "View3D > Sidebar > TinyUSDZ",
    "description": "Real-time sync with browser viewer via WebSocket",
    "category": "Import-Export",
}

import bpy
import json
import threading
import queue
from bpy.app.handlers import persistent
from mathutils import Matrix

# Optional WebSocket import (installed separately)
try:
    import websocket
    HAS_WEBSOCKET = True
except ImportError:
    HAS_WEBSOCKET = False
    print("Warning: websocket-client not installed. Run: pip install websocket-client")


# ============================================================================
# WebSocket Client
# ============================================================================

class BridgeWebSocket:
    """Thread-safe WebSocket client for bridge communication"""

    def __init__(self):
        self.ws = None
        self.session_id = None
        self.connected = False
        self.send_queue = queue.Queue()
        self.send_thread = None
        self._lock = threading.Lock()

    def connect(self, url="ws://localhost:8090"):
        """Connect to bridge server"""
        if not HAS_WEBSOCKET:
            return False

        try:
            self.ws = websocket.create_connection(
                f"{url}?type=blender",
                timeout=5
            )
            self.connected = True

            # Receive session ID
            response = self.ws.recv()
            msg = self._decode_message(response)
            if msg and msg.get('type') == 'session_created':
                self.session_id = msg.get('sessionId')
                print(f"Bridge connected. Session: {self.session_id}")

            # Start send thread
            self.send_thread = threading.Thread(target=self._send_loop, daemon=True)
            self.send_thread.start()

            return True
        except Exception as e:
            print(f"Bridge connection failed: {e}")
            self.connected = False
            return False

    def disconnect(self):
        """Disconnect from bridge server"""
        self.connected = False
        if self.ws:
            try:
                self.ws.close()
            except:
                pass
            self.ws = None
        self.session_id = None

    def send(self, message):
        """Queue message for sending (thread-safe)"""
        if self.connected:
            self.send_queue.put(message)

    def _send_loop(self):
        """Background thread for sending messages"""
        while self.connected:
            try:
                message = self.send_queue.get(timeout=0.1)
                if self.ws and self.connected:
                    encoded = self._encode_message(message)
                    self.ws.send(encoded, opcode=websocket.ABNF.OPCODE_BINARY)
            except queue.Empty:
                continue
            except Exception as e:
                print(f"Send error: {e}")
                self.connected = False
                break

    def _encode_message(self, header, payload=None):
        """Encode message with header length prefix"""
        import struct
        header_json = json.dumps(header)
        header_bytes = header_json.encode('utf-8')

        if payload:
            result = struct.pack('<I', len(header_bytes)) + header_bytes + payload
        else:
            result = struct.pack('<I', len(header_bytes)) + header_bytes

        return result

    def _decode_message(self, data):
        """Decode message from binary"""
        import struct
        if isinstance(data, str):
            # Plain JSON for initial handshake
            try:
                return json.loads(data)
            except:
                pass

        if len(data) < 4:
            return None

        header_len = struct.unpack('<I', data[:4])[0]
        header_json = data[4:4+header_len].decode('utf-8')
        return json.loads(header_json)


# ============================================================================
# Event Monitor - msgbus subscriptions
# ============================================================================

class MaterialMonitor:
    """Monitor material property changes via msgbus"""

    def __init__(self, bridge):
        self.bridge = bridge
        self.subscriptions = {}  # owner objects for cleanup

    def subscribe_all(self):
        """Subscribe to all materials in scene"""
        self.clear()

        for mat in bpy.data.materials:
            self._subscribe_material(mat)

    def _subscribe_material(self, material):
        """Subscribe to a single material's properties"""
        mat_path = f"/Materials/{material.name}"
        owner = object()
        self.subscriptions[material.name] = owner

        # Subscribe to common material properties
        props_to_watch = [
            'diffuse_color',
            'metallic',
            'roughness',
            'specular_intensity',
        ]

        for prop in props_to_watch:
            if hasattr(material, prop):
                try:
                    bpy.msgbus.subscribe_rna(
                        key=(material, prop),
                        owner=owner,
                        args=(material, prop, mat_path),
                        notify=self._on_property_change,
                    )
                except:
                    pass

        # Subscribe to node tree if using nodes
        if material.use_nodes and material.node_tree:
            self._subscribe_node_tree(material, owner, mat_path)

    def _subscribe_node_tree(self, material, owner, mat_path):
        """Subscribe to shader node changes"""
        for node in material.node_tree.nodes:
            if node.type == 'BSDF_PRINCIPLED':
                # Watch principled BSDF inputs
                for inp in node.inputs:
                    if inp.type == 'RGBA' or inp.type == 'VALUE':
                        try:
                            bpy.msgbus.subscribe_rna(
                                key=(inp, 'default_value'),
                                owner=owner,
                                args=(material, inp.name, mat_path),
                                notify=self._on_node_input_change,
                            )
                        except:
                            pass

    def _on_property_change(self, material, prop_name, mat_path):
        """Callback when material property changes"""
        value = getattr(material, prop_name, None)
        if value is None:
            return

        # Convert to serializable format
        if hasattr(value, '__iter__'):
            value = list(value)

        self.bridge.queue_update('material', mat_path, {prop_name: value})

    def _on_node_input_change(self, material, input_name, mat_path):
        """Callback when node input changes"""
        if not material.use_nodes:
            return

        for node in material.node_tree.nodes:
            if node.type == 'BSDF_PRINCIPLED':
                inp = node.inputs.get(input_name)
                if inp:
                    value = inp.default_value
                    if hasattr(value, '__iter__'):
                        value = list(value)

                    self.bridge.queue_update('material', mat_path, {input_name: value})
                    break

    def clear(self):
        """Clear all subscriptions"""
        for owner in self.subscriptions.values():
            bpy.msgbus.clear_by_owner(owner)
        self.subscriptions.clear()


class LightMonitor:
    """Monitor light property changes via msgbus"""

    def __init__(self, bridge):
        self.bridge = bridge
        self.subscriptions = {}

    def subscribe_all(self):
        """Subscribe to all lights in scene"""
        self.clear()

        for obj in bpy.data.objects:
            if obj.type == 'LIGHT':
                self._subscribe_light(obj)

    def _subscribe_light(self, light_obj):
        """Subscribe to light properties"""
        light = light_obj.data
        light_path = f"/Lights/{light_obj.name}"
        owner = object()
        self.subscriptions[light_obj.name] = owner

        # Light data properties
        light_props = ['energy', 'color', 'shadow_soft_size', 'spot_size', 'spot_blend']
        for prop in light_props:
            if hasattr(light, prop):
                try:
                    bpy.msgbus.subscribe_rna(
                        key=(light, prop),
                        owner=owner,
                        args=(light_obj, prop),
                        notify=self._on_light_change,
                    )
                except:
                    pass

    def _on_light_change(self, light_obj, prop_name):
        """Callback when light property changes"""
        light = light_obj.data
        light_path = f"/Lights/{light_obj.name}"

        value = getattr(light, prop_name, None)
        if value is None:
            return

        if hasattr(value, '__iter__'):
            value = list(value)

        self.bridge.queue_update('light', light_path, {prop_name: value})

    def clear(self):
        """Clear all subscriptions"""
        for owner in self.subscriptions.values():
            bpy.msgbus.clear_by_owner(owner)
        self.subscriptions.clear()


# ============================================================================
# Depsgraph Handler - Transform changes
# ============================================================================

class DepsgraphMonitor:
    """Monitor transform/geometry changes via depsgraph handler"""

    def __init__(self, bridge):
        self.bridge = bridge
        self.prev_transforms = {}
        self._handler = None

    def start(self):
        """Register depsgraph handler"""
        if self._handler is not None:
            return

        @persistent
        def on_depsgraph_update(scene, depsgraph):
            self._process_updates(scene, depsgraph)

        self._handler = on_depsgraph_update
        bpy.app.handlers.depsgraph_update_post.append(self._handler)

    def stop(self):
        """Unregister depsgraph handler"""
        if self._handler and self._handler in bpy.app.handlers.depsgraph_update_post:
            bpy.app.handlers.depsgraph_update_post.remove(self._handler)
        self._handler = None
        self.prev_transforms.clear()

    def _process_updates(self, scene, depsgraph):
        """Process depsgraph updates"""
        for update in depsgraph.updates:
            obj_id = update.id

            # Skip non-object updates
            if not isinstance(obj_id, bpy.types.Object):
                continue

            obj_name = obj_id.name
            obj_path = f"/Objects/{obj_name}"

            if update.is_updated_transform:
                # Get current transform
                obj = bpy.data.objects.get(obj_name)
                if obj:
                    loc = list(obj.location)
                    rot = list(obj.rotation_euler)
                    scale = list(obj.scale)

                    current = {'location': loc, 'rotation': rot, 'scale': scale}
                    prev = self.prev_transforms.get(obj_name)

                    # Only send if changed
                    if current != prev:
                        self.bridge.queue_update('transform', obj_path, current)
                        self.prev_transforms[obj_name] = current

            if update.is_updated_shading:
                # Shading update - material assignment may have changed
                self.bridge.queue_update('shading', obj_path, {'updated': True})


# ============================================================================
# Viewport Camera Timer - Only for viewport camera
# ============================================================================

class ViewportCameraOperator(bpy.types.Operator):
    """Modal operator for viewport camera sync (timer-based)"""
    bl_idname = "tinyusdz.viewport_camera_sync"
    bl_label = "Sync Viewport Camera"
    bl_options = {'INTERNAL'}

    _timer = None
    _prev_state = None

    def execute(self, context):
        wm = context.window_manager
        # 100ms interval - only for viewport camera
        self._timer = wm.event_timer_add(0.1, window=context.window)
        wm.modal_handler_add(self)
        return {'RUNNING_MODAL'}

    def modal(self, context, event):
        bridge = get_bridge()

        if not bridge or not bridge.is_connected():
            return self.cancel(context)

        if event.type == 'TIMER':
            self._sync_viewport_camera(context)

        return {'PASS_THROUGH'}

    def cancel(self, context):
        if self._timer:
            context.window_manager.event_timer_remove(self._timer)
            self._timer = None
        return {'CANCELLED'}

    def _sync_viewport_camera(self, context):
        """Extract and send viewport camera state"""
        for area in context.screen.areas:
            if area.type == 'VIEW_3D':
                space = area.spaces.active
                region_3d = space.region_3d

                # Get camera position and target
                view_matrix = region_3d.view_matrix.inverted()
                position = view_matrix.translation
                target = region_3d.view_location
                lens = space.lens

                current = {
                    'position': [round(position.x, 4), round(position.y, 4), round(position.z, 4)],
                    'target': [round(target.x, 4), round(target.y, 4), round(target.z, 4)],
                    'lens': round(lens, 2)
                }

                # Only send if changed (reduces traffic)
                if current != self._prev_state:
                    bridge = get_bridge()
                    if bridge:
                        bridge.queue_update('camera', '/BlenderViewport', current)
                    self._prev_state = current

                break


# ============================================================================
# Main Bridge Manager
# ============================================================================

class BlenderBridge:
    """Main bridge manager coordinating all monitors"""

    def __init__(self):
        self.ws = BridgeWebSocket()
        self.material_monitor = MaterialMonitor(self)
        self.light_monitor = LightMonitor(self)
        self.depsgraph_monitor = DepsgraphMonitor(self)

        # Update batching
        self.pending_updates = {}
        self.batch_timer = None
        self._batch_lock = threading.Lock()

    def connect(self, url="ws://localhost:8090"):
        """Connect to bridge server and start monitoring"""
        if not self.ws.connect(url):
            return False

        # Start all monitors
        self.material_monitor.subscribe_all()
        self.light_monitor.subscribe_all()
        self.depsgraph_monitor.start()

        # Start viewport camera sync
        bpy.ops.tinyusdz.viewport_camera_sync('INVOKE_DEFAULT')

        return True

    def disconnect(self):
        """Disconnect and stop all monitors"""
        self.material_monitor.clear()
        self.light_monitor.clear()
        self.depsgraph_monitor.stop()
        self.ws.disconnect()

    def is_connected(self):
        """Check if connected"""
        return self.ws.connected

    def get_session_id(self):
        """Get current session ID"""
        return self.ws.session_id

    def queue_update(self, target_type, path, changes):
        """Queue an update for batched sending"""
        with self._batch_lock:
            key = f"{target_type}:{path}"
            if key not in self.pending_updates:
                self.pending_updates[key] = {
                    'target': {'type': target_type, 'path': path},
                    'changes': {}
                }
            self.pending_updates[key]['changes'].update(changes)

        # Schedule batch send (debounce)
        self._schedule_batch_send()

    def _schedule_batch_send(self):
        """Schedule sending batched updates"""
        # Use Blender's timer for thread safety
        if not bpy.app.timers.is_registered(self._send_batched_updates):
            bpy.app.timers.register(self._send_batched_updates, first_interval=0.016)

    def _send_batched_updates(self):
        """Send all pending updates (called by timer)"""
        with self._batch_lock:
            updates = self.pending_updates
            self.pending_updates = {}

        for key, update in updates.items():
            import uuid
            message = {
                'type': 'parameter_update',
                'messageId': str(uuid.uuid4()),
                'timestamp': int(bpy.context.scene.frame_current),
                'target': update['target'],
                'changes': update['changes']
            }
            self.ws.send(message)

        return None  # Don't repeat timer

    def upload_scene(self, filepath):
        """Upload USDZ scene to bridge"""
        if not self.is_connected():
            return False

        try:
            with open(filepath, 'rb') as f:
                scene_data = f.read()

            import uuid
            import os
            message = {
                'type': 'scene_upload',
                'messageId': str(uuid.uuid4()),
                'timestamp': 0,
                'scene': {
                    'name': os.path.basename(filepath),
                    'format': 'usdz',
                    'byteLength': len(scene_data)
                }
            }

            # Send with payload
            self.ws.send((message, scene_data))
            return True
        except Exception as e:
            print(f"Scene upload failed: {e}")
            return False


# Global bridge instance
_bridge = None

def get_bridge():
    global _bridge
    return _bridge


# ============================================================================
# UI Panel
# ============================================================================

class TINYUSDZ_PT_bridge_panel(bpy.types.Panel):
    """TinyUSDZ Bridge Panel"""
    bl_label = "TinyUSDZ Bridge"
    bl_idname = "TINYUSDZ_PT_bridge_panel"
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = 'TinyUSDZ'

    def draw(self, context):
        layout = self.layout
        bridge = get_bridge()

        if not HAS_WEBSOCKET:
            layout.label(text="websocket-client not installed", icon='ERROR')
            layout.label(text="pip install websocket-client")
            return

        if bridge and bridge.is_connected():
            layout.label(text="Connected", icon='CHECKMARK')
            layout.label(text=f"Session: {bridge.get_session_id()[:8]}...")
            layout.operator("tinyusdz.disconnect", text="Disconnect")
            layout.separator()
            layout.operator("tinyusdz.export_and_upload", text="Upload Scene")
        else:
            layout.label(text="Disconnected", icon='X')
            layout.prop(context.scene, "tinyusdz_server_url")
            layout.operator("tinyusdz.connect", text="Connect")


class TINYUSDZ_OT_connect(bpy.types.Operator):
    """Connect to TinyUSDZ Bridge"""
    bl_idname = "tinyusdz.connect"
    bl_label = "Connect to Bridge"

    def execute(self, context):
        global _bridge

        if _bridge is None:
            _bridge = BlenderBridge()

        url = context.scene.tinyusdz_server_url
        if _bridge.connect(url):
            self.report({'INFO'}, f"Connected to {url}")
        else:
            self.report({'ERROR'}, "Connection failed")

        return {'FINISHED'}


class TINYUSDZ_OT_disconnect(bpy.types.Operator):
    """Disconnect from TinyUSDZ Bridge"""
    bl_idname = "tinyusdz.disconnect"
    bl_label = "Disconnect"

    def execute(self, context):
        global _bridge

        if _bridge:
            _bridge.disconnect()
            self.report({'INFO'}, "Disconnected")

        return {'FINISHED'}


class TINYUSDZ_OT_export_and_upload(bpy.types.Operator):
    """Export scene as USDZ and upload to bridge"""
    bl_idname = "tinyusdz.export_and_upload"
    bl_label = "Export and Upload"

    def execute(self, context):
        import tempfile
        import os

        bridge = get_bridge()
        if not bridge or not bridge.is_connected():
            self.report({'ERROR'}, "Not connected")
            return {'CANCELLED'}

        # Export to temp file
        filepath = os.path.join(tempfile.gettempdir(), 'bridge_export.usdz')

        bpy.ops.wm.usd_export(
            filepath=filepath,
            export_materials=True,
            generate_materialx_network=True
        )

        # Upload
        if bridge.upload_scene(filepath):
            self.report({'INFO'}, "Scene uploaded")
        else:
            self.report({'ERROR'}, "Upload failed")

        return {'FINISHED'}


# ============================================================================
# Registration
# ============================================================================

classes = (
    ViewportCameraOperator,
    TINYUSDZ_PT_bridge_panel,
    TINYUSDZ_OT_connect,
    TINYUSDZ_OT_disconnect,
    TINYUSDZ_OT_export_and_upload,
)

def register():
    for cls in classes:
        bpy.utils.register_class(cls)

    bpy.types.Scene.tinyusdz_server_url = bpy.props.StringProperty(
        name="Server URL",
        default="ws://localhost:8090",
        description="TinyUSDZ Bridge server URL"
    )

def unregister():
    global _bridge

    if _bridge:
        _bridge.disconnect()
        _bridge = None

    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)

    del bpy.types.Scene.tinyusdz_server_url

if __name__ == "__main__":
    register()
