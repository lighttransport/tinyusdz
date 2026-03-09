# SPDX-License-Identifier: MIT
# TinyUSDZ Animation Clips Exporter — Blender Addon
#
# Exports multiple Blender Actions as USD animation clips using two approaches:
#   1. Option C (USDHook): Concatenated timeline + custom property metadata
#   2. Separate files (Operator): One USD file per Action (HumanFemale pattern)
#
# Metadata format (v1):
#   Scalar attributes under userProperties:tinyusdz:animClips: namespace.
#   Multi-value fields use "|" (pipe) as separator — never comma, because
#   action names may contain commas.
#   On Blender re-import with property_import_mode='USER', the
#   "userProperties:" prefix is stripped, leaving "tinyusdz:animClips:*".

bl_info = {
    "name": "TinyUSDZ Animation Clips Exporter",
    "author": "TinyUSDZ Contributors",
    "version": (1, 1, 0),
    "blender": (4, 0, 0),
    "location": "View3D > Sidebar > TinyUSDZ",
    "description": "Export multiple Blender Actions as USD animation clips",
    "category": "Import-Export",
}

_METADATA_VERSION = 1
_SEP = "|"

import bpy
import os
import logging

log = logging.getLogger(__name__)
log.setLevel(logging.DEBUG)

# USD Python modules — available inside Blender's USD export hook
try:
    from pxr import Usd, UsdSkel, Sdf
    _HAS_PXR = True
except ImportError:
    _HAS_PXR = False


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

# Map Blender object type to a short tag used in metadata
_OBJ_TYPE_TAG = {
    'ARMATURE': 'Armature',
    'CAMERA': 'Camera',
    'LIGHT': 'Light',
    'MESH': 'Mesh',
    'EMPTY': 'Empty',
    'CURVE': 'Curve',
    'SURFACE': 'Surface',
    'SPEAKER': 'Speaker',
}

# Map object type tags to Blender panel icons
_OBJ_TYPE_ICON = {
    'ARMATURE': 'ARMATURE_DATA',
    'CAMERA': 'CAMERA_DATA',
    'LIGHT': 'LIGHT_DATA',
    'MESH': 'MESH_DATA',
    'EMPTY': 'EMPTY_DATA',
    'CURVE': 'CURVE_DATA',
    'SURFACE': 'SURFACE_DATA',
    'SPEAKER': 'SPEAKER',
}


def _source_type_for(obj):
    """Return the animation source type tag for a Blender object."""
    if obj.type == 'ARMATURE':
        return "SkelAnimation"
    return "XformOp"


def _sanitize_filename(name):
    """Replace characters that are unsafe in filenames."""
    for ch in r' /\:*?"<>|':
        name = name.replace(ch, '_')
    return name


# ---------------------------------------------------------------------------
# Core — gather exportable animation clips
# ---------------------------------------------------------------------------

def gather_animation_clips(context):
    """Return a list of clip descriptors from all objects in the scene.

    Each entry::

        {
            'action':       bpy.types.Action,
            'object':       bpy.types.Object,
            'obj_type':     str,          # e.g. 'ARMATURE', 'CAMERA', ...
            'source_type':  str,          # 'SkelAnimation' or 'XformOp'
            'frame_start':  int,
            'frame_end':    int,
        }
    """
    clips = []

    for obj in bpy.data.objects:
        anim_data = obj.animation_data
        if not anim_data:
            continue

        seen_actions = set()

        # Active action
        if anim_data.action:
            action = anim_data.action
            seen_actions.add(action)
            clips.append({
                'action': action,
                'object': obj,
                'obj_type': obj.type,
                'source_type': _source_type_for(obj),
                'frame_start': int(action.frame_range[0]),
                'frame_end': int(action.frame_range[1]),
            })

        # NLA track actions (deduplicated per object)
        for track in anim_data.nla_tracks:
            for strip in track.strips:
                if strip.action and strip.action not in seen_actions:
                    seen_actions.add(strip.action)
                    clips.append({
                        'action': strip.action,
                        'object': obj,
                        'obj_type': obj.type,
                        'source_type': _source_type_for(obj),
                        'frame_start': int(strip.action.frame_range[0]),
                        'frame_end': int(strip.action.frame_range[1]),
                    })

    return clips


def compute_concatenated_offsets(clips):
    """Compute non-overlapping frame offsets for a list of clips.

    Returns a list of (global_start, global_end) tuples and total frame count.
    """
    offsets = []
    current_frame = 0
    for clip in clips:
        duration = clip['frame_end'] - clip['frame_start']
        offsets.append((current_frame, current_frame + duration))
        current_frame += duration + 1  # +1 frame gap between clips
    return offsets, current_frame


# ---------------------------------------------------------------------------
# Metadata writer (used by hook and auto-bake)
# ---------------------------------------------------------------------------

def _find_metadata_prim(stage):
    """Find the best prim to attach clip metadata to.

    Blender's generated root Xform (customData.Blender.generated=1) is
    stripped on re-import, so we skip it and use the first real child.
    """
    default_prim = stage.GetDefaultPrim()
    if default_prim and default_prim.IsValid():
        custom_data = default_prim.GetCustomData()
        blender_data = custom_data.get("Blender", {})
        if blender_data.get("generated", False):
            for child in default_prim.GetChildren():
                return child
        else:
            return default_prim

    # Fallback: first SkelRoot
    for prim in stage.Traverse():
        if prim.IsA(UsdSkel.Root):
            return prim

    # Last resort: first prim
    for prim in stage.Traverse():
        return prim

    return None


def _write_clip_metadata(stage, clips, fps, concat_offsets=None):
    """Write clip metadata attributes to the USD stage.

    Args:
        stage: Usd.Stage
        clips: list from gather_animation_clips()
        fps: float — scene frames per second
        concat_offsets: optional list of (start, end) tuples for auto-bake
    """
    target = _find_metadata_prim(stage)
    if not target or not target.IsValid():
        log.error("[TinyUSDZ] Cannot find any prim to attach metadata")
        return False

    _NS = "userProperties:tinyusdz:animClips:"

    names = [c['action'].name for c in clips]
    start_frames = [str(c['frame_start']) for c in clips]
    end_frames = [str(c['frame_end']) for c in clips]
    source_types = [c['source_type'] for c in clips]
    object_names = [c['object'].name for c in clips]
    object_types = [
        _OBJ_TYPE_TAG.get(c['obj_type'], c['obj_type']) for c in clips
    ]

    def _set_str(name, value):
        target.CreateAttribute(_NS + name, Sdf.ValueTypeNames.String).Set(value)

    def _set_int(name, value):
        target.CreateAttribute(_NS + name, Sdf.ValueTypeNames.Int).Set(value)

    def _set_float(name, value):
        target.CreateAttribute(_NS + name, Sdf.ValueTypeNames.Float).Set(value)

    _set_int("version", _METADATA_VERSION)
    _set_int("clipCount", len(clips))
    _set_float("fps", float(fps))
    _set_str("names", _SEP.join(names))
    _set_str("startFrames", _SEP.join(start_frames))
    _set_str("endFrames", _SEP.join(end_frames))
    _set_str("sourceTypes", _SEP.join(source_types))
    _set_str("objectNames", _SEP.join(object_names))
    _set_str("objectTypes", _SEP.join(object_types))

    if concat_offsets:
        _set_str("concatStartFrames",
                 _SEP.join(str(o[0]) for o in concat_offsets))
        _set_str("concatEndFrames",
                 _SEP.join(str(o[1]) for o in concat_offsets))

    # Console output
    print(f"[TinyUSDZ] Wrote {len(clips)} clip(s) to {target.GetPath()}:")
    for i, c in enumerate(clips):
        print(f"  [{i}] {names[i]} ({source_types[i]}, "
              f"{object_types[i]}:{object_names[i]}) "
              f"frames {start_frames[i]}-{end_frames[i]}")

    return True


# ---------------------------------------------------------------------------
# Addon preferences
# ---------------------------------------------------------------------------

class TinyUSDZAnimClipsPreferences(bpy.types.AddonPreferences):
    bl_idname = __name__

    bake_mode: bpy.props.EnumProperty(
        name="Bake Mode",
        description="How clip data is written during standard USD export",
        items=[
            ('METADATA_ONLY', "Metadata Only",
             "Write clip boundary metadata to the exported stage. "
             "Assumes the user arranged NLA strips with correct frame ranges"),
            ('AUTO_BAKE', "Auto-Bake (Concatenate)",
             "Use the separate-file operator workflow via the panel. "
             "The hook still writes metadata for single-file exports"),
        ],
        default='METADATA_ONLY',
    )

    def draw(self, context):
        layout = self.layout
        layout.prop(self, "bake_mode")


# ---------------------------------------------------------------------------
# USD Hook — Option C (metadata on exported stage)
# ---------------------------------------------------------------------------

class TinyUSDZAnimClipsHook(bpy.types.USDHook):
    """USD export hook that writes multi-clip metadata to the exported stage."""
    bl_idname = "tinyusdz_anim_clips"
    bl_label = "TinyUSDZ Anim Clips"
    bl_description = "Write multi-clip animation metadata to USD"

    @staticmethod
    def on_export(export_context):
        if not _HAS_PXR:
            log.warning("[TinyUSDZ] pxr modules not available — skipping hook")
            return True

        stage = export_context.get_stage()
        if not stage:
            log.warning("[TinyUSDZ] No USD stage in export context")
            return False

        clips = gather_animation_clips(bpy.context)
        if len(clips) <= 1:
            if clips:
                log.info("[TinyUSDZ] Only 1 clip — no multi-clip metadata needed")
            return True

        fps = bpy.context.scene.render.fps

        # Auto-bake offsets if mode is set
        concat_offsets = None
        prefs = bpy.context.preferences.addons.get(__name__)
        if prefs and prefs.preferences.bake_mode == 'AUTO_BAKE':
            concat_offsets, _ = compute_concatenated_offsets(clips)

        ok = _write_clip_metadata(stage, clips, fps, concat_offsets)
        return ok if ok else False


# ---------------------------------------------------------------------------
# Operator — Separate file export (HumanFemale pattern)
# ---------------------------------------------------------------------------

class EXPORT_OT_tinyusdz_anim_clips(bpy.types.Operator):
    """Export each Blender Action as a separate USD file.

    Follows the Pixar HumanFemale pattern:
      - <base_name>.usd          — scene + skeleton (no animation)
      - <base_name>.<action>.usd — animation-only, references the base
    """
    bl_idname = "export.tinyusdz_anim_clips"
    bl_label = "Export USD Animation Clips"
    bl_description = "Export each Blender Action as a separate USD file"
    bl_options = {'REGISTER', 'UNDO'}

    directory: bpy.props.StringProperty(
        name="Output Directory",
        subtype='DIR_PATH',
    )
    base_name: bpy.props.StringProperty(
        name="Base Name",
        default="Scene",
        description="Base filename for exported USD files",
    )
    file_format: bpy.props.EnumProperty(
        name="Format",
        items=[
            ('USDA', ".usda (ASCII)", "Human-readable ASCII format"),
            ('USDC', ".usdc (Binary)", "Compact binary Crate format"),
        ],
        default='USDA',
    )

    def invoke(self, context, event):
        blend_name = os.path.splitext(
            os.path.basename(bpy.data.filepath or "Scene")
        )[0]
        self.base_name = blend_name
        context.window_manager.fileselect_add(self)
        return {'RUNNING_MODAL'}

    def draw(self, context):
        layout = self.layout
        layout.prop(self, "base_name")
        layout.prop(self, "file_format")

    def execute(self, context):
        if not self.directory:
            self.report({'ERROR'}, "No output directory selected")
            return {'CANCELLED'}

        clips = gather_animation_clips(context)
        if not clips:
            self.report({'WARNING'}, "No animation clips found")
            return {'CANCELLED'}

        ext = ".usda" if self.file_format == 'USDA' else ".usdc"
        base_path = os.path.join(self.directory, self.base_name + ext)

        # Save current state
        scene = context.scene
        orig_frame_start = scene.frame_start
        orig_frame_end = scene.frame_end
        orig_frame_current = scene.frame_current

        # Map object -> original active action
        orig_actions = {}
        for clip in clips:
            obj = clip['object']
            if obj not in orig_actions and obj.animation_data:
                orig_actions[obj] = obj.animation_data.action

        try:
            # --- Pass 1: Export base scene (no animation) ---
            self.report({'INFO'}, f"Exporting base scene: {base_path}")
            bpy.ops.wm.usd_export(
                filepath=base_path,
                export_animation=False,
                export_hair=True,
                export_uvmaps=True,
                export_normals=True,
                export_materials=True,
            )

            # --- Pass 2: Export each action as a separate file ---
            for clip in clips:
                action = clip['action']
                obj = clip['object']
                action_name = _sanitize_filename(action.name)
                clip_path = os.path.join(
                    self.directory,
                    f"{self.base_name}.{action_name}{ext}"
                )

                self.report(
                    {'INFO'},
                    f"Exporting clip: {action.name} -> {clip_path}"
                )

                # Set active action and frame range
                if obj.animation_data:
                    obj.animation_data.action = action
                scene.frame_start = clip['frame_start']
                scene.frame_end = clip['frame_end']
                scene.frame_set(clip['frame_start'])

                bpy.ops.wm.usd_export(
                    filepath=clip_path,
                    export_animation=True,
                    export_hair=False,
                    export_uvmaps=False,
                    export_normals=False,
                    export_materials=False,
                )

                if _HAS_PXR:
                    _add_base_reference(clip_path, base_path)

            self.report(
                {'INFO'},
                f"Exported {len(clips)} animation clips to {self.directory}"
            )

        except Exception as e:
            self.report({'ERROR'}, f"Export failed: {e}")
            log.exception("[TinyUSDZ] Separate file export failed")
            return {'CANCELLED'}

        finally:
            scene.frame_start = orig_frame_start
            scene.frame_end = orig_frame_end
            scene.frame_set(orig_frame_current)
            for obj, action in orig_actions.items():
                if obj.animation_data:
                    obj.animation_data.action = action

        return {'FINISHED'}


def _add_base_reference(clip_path, base_path):
    """Add a reference from the clip file to the base scene file."""
    try:
        stage = Usd.Stage.Open(clip_path)
        if not stage:
            return

        base_rel = os.path.relpath(base_path, os.path.dirname(clip_path))
        if not base_rel.startswith('.'):
            base_rel = './' + base_rel

        default_prim = stage.GetDefaultPrim()
        if default_prim and default_prim.IsValid():
            default_prim.GetReferences().AddReference(Sdf.Reference(base_rel))

        stage.GetRootLayer().Save()
        log.info(f"[TinyUSDZ] Added reference {base_rel} to {clip_path}")

    except Exception as e:
        log.warning(f"[TinyUSDZ] Could not add reference to {clip_path}: {e}")


# ---------------------------------------------------------------------------
# Auto-bake operator (concatenates all actions, then exports)
# ---------------------------------------------------------------------------

class EXPORT_OT_tinyusdz_auto_bake(bpy.types.Operator):
    """Concatenate all Actions end-to-end and export as a single USD file."""
    bl_idname = "export.tinyusdz_auto_bake"
    bl_label = "Export Concatenated USD"
    bl_description = (
        "Concatenate all Actions end-to-end and export as a single USD file"
    )
    bl_options = {'REGISTER', 'UNDO'}

    filepath: bpy.props.StringProperty(
        name="File Path",
        subtype='FILE_PATH',
    )

    def invoke(self, context, event):
        context.window_manager.fileselect_add(self)
        return {'RUNNING_MODAL'}

    def execute(self, context):
        if not self.filepath:
            self.report({'ERROR'}, "No output file selected")
            return {'CANCELLED'}

        clips = gather_animation_clips(context)
        if not clips:
            self.report({'WARNING'}, "No animation clips found")
            return {'CANCELLED'}

        scene = context.scene
        orig_frame_start = scene.frame_start
        orig_frame_end = scene.frame_end
        orig_frame_current = scene.frame_current

        # Save original NLA/action state per object
        orig_states = {}
        for clip in clips:
            obj = clip['object']
            if obj in orig_states:
                continue
            anim_data = obj.animation_data
            if not anim_data:
                continue
            orig_states[obj] = {
                'action': anim_data.action,
                'tracks': [
                    (track, track.mute) for track in anim_data.nla_tracks
                ],
            }

        try:
            # Group clips by object
            clips_by_obj = {}
            for clip in clips:
                clips_by_obj.setdefault(clip['object'], []).append(clip)

            total_frames = 0

            for obj, obj_clips in clips_by_obj.items():
                anim_data = obj.animation_data
                if not anim_data:
                    continue

                for track in anim_data.nla_tracks:
                    track.mute = True

                anim_data.action = None

                temp_track = anim_data.nla_tracks.new()
                temp_track.name = "_tinyusdz_concat_temp"

                current_frame = 0
                for clip in obj_clips:
                    action = clip['action']
                    duration = clip['frame_end'] - clip['frame_start']
                    strip = temp_track.strips.new(
                        action.name, int(current_frame), action
                    )
                    strip.frame_start = current_frame
                    strip.frame_end = current_frame + duration
                    strip.action_frame_start = clip['frame_start']
                    strip.action_frame_end = clip['frame_end']
                    current_frame += duration + 1

                total_frames = max(total_frames, int(current_frame))

            scene.frame_start = 0
            scene.frame_end = total_frames
            scene.frame_set(0)

            bpy.ops.wm.usd_export(
                filepath=self.filepath,
                export_animation=True,
                export_hair=True,
                export_uvmaps=True,
                export_normals=True,
                export_materials=True,
            )

            self.report(
                {'INFO'},
                f"Exported concatenated USD with {len(clips)} clips"
            )

        except Exception as e:
            self.report({'ERROR'}, f"Auto-bake export failed: {e}")
            log.exception("[TinyUSDZ] Auto-bake export failed")
            return {'CANCELLED'}

        finally:
            scene.frame_start = orig_frame_start
            scene.frame_end = orig_frame_end
            scene.frame_set(orig_frame_current)

            for obj, state in orig_states.items():
                anim_data = obj.animation_data
                if not anim_data:
                    continue

                for track in list(anim_data.nla_tracks):
                    if track.name == "_tinyusdz_concat_temp":
                        anim_data.nla_tracks.remove(track)

                for track, was_muted in state['tracks']:
                    try:
                        track.mute = was_muted
                    except ReferenceError:
                        pass

                anim_data.action = state['action']

        return {'FINISHED'}


# ---------------------------------------------------------------------------
# UI Panel
# ---------------------------------------------------------------------------

class VIEW3D_PT_tinyusdz_anim_clips(bpy.types.Panel):
    bl_label = "TinyUSDZ Anim Clips"
    bl_idname = "VIEW3D_PT_tinyusdz_anim_clips"
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = 'TinyUSDZ'

    def draw(self, context):
        layout = self.layout
        clips = gather_animation_clips(context)

        # Clip list
        box = layout.box()
        box.label(text=f"Found {len(clips)} animation clip(s)", icon='ACTION')
        for clip in clips:
            row = box.row(align=True)
            icon = _OBJ_TYPE_ICON.get(clip['obj_type'], 'OBJECT_DATA')
            row.label(text=clip['action'].name, icon=icon)
            sub = row.row()
            sub.alignment = 'RIGHT'
            sub.label(text=f"{clip['frame_start']}-{clip['frame_end']}")

        if not clips:
            box.label(text="No actions found.", icon='INFO')

        layout.separator()

        # Hook info
        box = layout.box()
        box.label(text="USD Hook (auto on export)", icon='LINKED')
        box.label(text="Writes clip metadata to exported USD.")
        prefs = context.preferences.addons.get(__name__)
        if prefs:
            box.prop(prefs.preferences, "bake_mode")

        layout.separator()

        # Export buttons
        col = layout.column(align=True)
        col.label(text="Separate Files:", icon='EXPORT')
        col.operator(
            "export.tinyusdz_anim_clips",
            text="Export Separate Files",
            icon='FILE_FOLDER',
        )

        layout.separator()

        col = layout.column(align=True)
        col.label(text="Concatenated:", icon='NLA')
        col.operator(
            "export.tinyusdz_auto_bake",
            text="Export Concatenated USD",
            icon='FILE_TICK',
        )


# ---------------------------------------------------------------------------
# Menu integration
# ---------------------------------------------------------------------------

def menu_func_export(self, context):
    self.layout.operator(
        EXPORT_OT_tinyusdz_anim_clips.bl_idname,
        text="USD Animation Clips (.usd)",
    )


# ---------------------------------------------------------------------------
# Registration
# ---------------------------------------------------------------------------

_classes = (
    TinyUSDZAnimClipsPreferences,
    TinyUSDZAnimClipsHook,
    EXPORT_OT_tinyusdz_anim_clips,
    EXPORT_OT_tinyusdz_auto_bake,
    VIEW3D_PT_tinyusdz_anim_clips,
)


def register():
    for cls in _classes:
        bpy.utils.register_class(cls)
    bpy.types.TOPBAR_MT_file_export.append(menu_func_export)
    print(f"[TinyUSDZ] Animation Clips Exporter v{bl_info['version']} registered")


def unregister():
    try:
        bpy.types.TOPBAR_MT_file_export.remove(menu_func_export)
    except ValueError:
        pass
    for cls in reversed(_classes):
        try:
            bpy.utils.unregister_class(cls)
        except RuntimeError:
            pass


if __name__ == "__main__":
    register()
