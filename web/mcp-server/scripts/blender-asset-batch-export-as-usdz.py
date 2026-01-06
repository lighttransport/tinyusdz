import bpy
import os
from pathlib import Path
import bmesh
from mathutils import Vector
import json

def asset_object_filter(obj):
    print(obj)
    if obj.asset_data:
        return True
    
    return False


def get_mesh_bounding_box(obj):
    """
    Compute the bounding box of a mesh object in world coordinates.
    
    Args:
        obj: Blender mesh object
        
    Returns:
        tuple: (min_coords, max_coords, center, dimensions)
    """
    # Method 1: Using object's bound_box (fastest, world space)
    bbox_corners = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
    
    # Find min/max coordinates
    min_x = min(corner.x for corner in bbox_corners)
    max_x = max(corner.x for corner in bbox_corners)
    min_y = min(corner.y for corner in bbox_corners)
    max_y = max(corner.y for corner in bbox_corners)
    min_z = min(corner.z for corner in bbox_corners)
    max_z = max(corner.z for corner in bbox_corners)
    
    min_coords = Vector((min_x, min_y, min_z))
    max_coords = Vector((max_x, max_y, max_z))
    center = (min_coords + max_coords) / 2
    dimensions = max_coords - min_coords
    
    return min_coords, max_coords, center, dimensions


def export_selected_as_usdz(filepath, **kwargs):
    """Export selected objects as USDZ file"""
    
    # Ensure we have selected objects
    selected_objects = bpy.context.selected_objects
    if not selected_objects:
        print("No objects selected for export")
        return False
    
    # Get all bounding box corners
    all_corners = []
    for obj in selected_objects:
        bbox_corners = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
        all_corners.extend(bbox_corners)
    
    # Find min/max coordinates
    min_x = min(corner.x for corner in all_corners)
    max_x = max(corner.x for corner in all_corners)
    min_y = min(corner.y for corner in all_corners)
    max_y = max(corner.y for corner in all_corners)
    min_z = min(corner.z for corner in all_corners)
    max_z = max(corner.z for corner in all_corners)
    
    min_coords = Vector((min_x, min_y, min_z))
    max_coords = Vector((max_x, max_y, max_z))
    
    # FIXME: pivot
    pivot = [0.0, 0.0, 0.0]
    
    meta_jsonpath = os.path.splitext(filepath)[0] + "-meta.json"
    
    meta = {}
    meta["pivot_position"] = pivot
    meta["bmin"] = [min_x, min_y, min_z]
    meta["bmax"] = [max_x, max_y, max_z]
    
    
    # Ensure filepath has .usdz extension
    filepath = Path(filepath)
    if filepath.suffix.lower() != '.usdz':
        filepath = filepath.with_suffix('.usdz')
    
    # Create directory if it doesn't exist
    filepath.parent.mkdir(parents=True, exist_ok=True)


    # ouput meta
    with open(meta_jsonpath, "w") as meta_f:
        meta_f.write(json.dumps(meta))
        
    
    print(f"Exporting {len(selected_objects)} selected objects to: {filepath}")
    
    # Default export settings
    export_settings = {
        'filepath': str(filepath),
        'check_existing': False,
        'selected_objects_only': True,
        'visible_objects_only': False,
        'export_animation': False,
        'export_hair': False,
        'export_uvmaps': True,
        'export_normals': True,
        'export_materials': True,
        'use_instancing': True,
        'evaluation_mode': 'RENDER',
        'generate_preview_surface': True,
        'export_textures': True,
        'overwrite_textures': True,
        'relative_paths': True,
    }
    
    # Update with any custom settings
    export_settings.update(kwargs)
    
    try:
        # Export using USD exporter
        bpy.ops.wm.usd_export(**export_settings)
        print(f"Successfully exported USDZ to: {filepath}")
        return True
        
    except Exception as e:
        print(f"Error exporting USDZ: {e}")
        return False

def export_objects_by_name(object_names, filepath, **kwargs):
    """Export specific objects by name as USDZ"""
    
    # Clear current selection
    bpy.ops.object.select_all(action='DESELECT')
    
    # Select objects by name
    selected_count = 0
    for obj_name in object_names:
        obj = bpy.data.objects.get(obj_name)
        if obj:
            obj.select_set(True)
            selected_count += 1
            print(f"Selected object: {obj_name}")
        else:
            print(f"Object not found: {obj_name}")
    
    if selected_count == 0:
        print("No valid objects found to export")
        return False
    
    # Export selected objects
    return export_selected_as_usdz(filepath, **kwargs)

def export_collection_as_usdz(collection_name, filepath, **kwargs):
    """Export all objects in a collection as USDZ"""
    
    collection = bpy.data.collections.get(collection_name)
    if not collection:
        print(f"Collection '{collection_name}' not found")
        return False
    
    # Clear selection and select all objects in collection
    bpy.ops.object.select_all(action='DESELECT')
    
    selected_count = 0
    for obj in collection.objects:
        if obj.type in ['MESH', 'CURVE', 'SURFACE', 'META', 'FONT']:
            obj.select_set(True)
            selected_count += 1
    
    print(f"Selected {selected_count} objects from collection '{collection_name}'")
    
    if selected_count == 0:
        print("No exportable objects in collection")
        return False
    
    return export_selected_as_usdz(filepath, **kwargs)

def export_usdz_with_custom_settings(filepath, 
                                   export_animation=False,
                                   export_materials=True,
                                   export_textures=True,
                                   generate_preview=True,
                                   use_instancing=True):
    """Export USDZ with custom settings"""
    
    custom_settings = {
        'export_animation': export_animation,
        'export_materials': export_materials,
        'export_textures': export_textures,
        'generate_preview_surface': generate_preview,
        'use_instancing': use_instancing,
    }
    
    return export_selected_as_usdz(filepath, **custom_settings)

def batch_export_objects_as_usdz(output_directory, object_filter=None):
    """Export multiple objects as individual USDZ files"""
    
    output_dir = Path(output_directory)
    output_dir.mkdir(parents=True, exist_ok=True)
    
    # Get objects to export
    objects_to_export = []
    for obj in bpy.context.scene.objects:
        if obj.type in ['MESH', 'CURVE', 'SURFACE', 'META', 'FONT']:
            if object_filter is None or object_filter(obj):
                objects_to_export.append(obj)
    
    print(f"Batch exporting {len(objects_to_export)} objects...")
    
    success_count = 0
    
    for obj in objects_to_export:
        # Select only this object
        bpy.ops.object.select_all(action='DESELECT')
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        
        # Create filename from object name
        safe_name = "".join(c for c in obj.name if c.isalnum() or c in (' ', '-', '_')).rstrip()
        filepath = output_dir / f"{safe_name}.usdz"
        
        # Export
        if export_selected_as_usdz(str(filepath)):
            success_count += 1
        else:
            print(f"Failed to export: {obj.name}")
    
    print(f"Successfully exported {success_count}/{len(objects_to_export)} objects")
    return success_count

def prepare_object_for_usdz_export(obj):
    """Prepare an object for optimal USDZ export"""
    
    print(f"Preparing object '{obj.name}' for USDZ export...")
    
    # Select the object
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    
    # Apply transformations
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    print(f"  Applied transformations")
    
    # If it's a mesh, ensure it has proper normals
    if obj.type == 'MESH':
        bpy.ops.object.mode_set(mode='EDIT')
        bpy.ops.mesh.select_all(action='SELECT')
        bpy.ops.mesh.normals_make_consistent(inside=False)
        bpy.ops.object.mode_set(mode='OBJECT')
        print(f"  Fixed mesh normals")
    
    # Ensure UV mapping exists
    if obj.type == 'MESH' and not obj.data.uv_layers:
        bpy.ops.object.mode_set(mode='EDIT')
        bpy.ops.mesh.select_all(action='SELECT')
        bpy.ops.uv.smart_project()
        bpy.ops.object.mode_set(mode='OBJECT')
        print(f"  Generated UV mapping")
    
    print(f"  Object '{obj.name}' prepared for export")

def create_ar_optimized_usdz(filepath, scale_factor=1.0, center_object=True):
    """Create USDZ optimized for AR viewing"""
    
    selected_objects = bpy.context.selected_objects
    if not selected_objects:
        print("No objects selected")
        return False
    
    print("Creating AR-optimized USDZ...")
    
    # Store original transforms
    original_transforms = {}
    for obj in selected_objects:
        original_transforms[obj.name] = {
            'location': obj.location.copy(),
            'rotation': obj.rotation_euler.copy(),
            'scale': obj.scale.copy()
        }
    
    try:
        # Prepare objects
        for obj in selected_objects:
            prepare_object_for_usdz_export(obj)
        
        # Center objects if requested
        if center_object and selected_objects:
            # Calculate bounding box center
            bbox_center = [0, 0, 0]
            obj_count = 0
            
            for obj in selected_objects:
                if obj.type == 'MESH':
                    world_bbox = [obj.matrix_world @ mathutils.Vector(corner) for corner in obj.bound_box]
                    for corner in world_bbox:
                        bbox_center[0] += corner[0]
                        bbox_center[1] += corner[1]
                        bbox_center[2] += corner[2]
                    obj_count += len(world_bbox)
            
            if obj_count > 0:
                bbox_center = [c / obj_count for c in bbox_center]
                
                # Move objects to center
                for obj in selected_objects:
                    obj.location[0] -= bbox_center[0]
                    obj.location[1] -= bbox_center[1]
                    obj.location[2] -= bbox_center[2]
        
        # Apply scale factor
        if scale_factor != 1.0:
            for obj in selected_objects:
                obj.scale *= scale_factor
        
        # Export with AR-optimized settings
        ar_settings = {
            'export_materials': True,
            'export_textures': True,
            'generate_preview_surface': True,
            'use_instancing': True,
            'export_uvmaps': True,
            'export_normals': True,
            'relative_paths': True,
        }
        
        success = export_selected_as_usdz(filepath, **ar_settings)
        
        return success
        
    finally:
        # Restore original transforms
        for obj in selected_objects:
            if obj.name in original_transforms:
                transform = original_transforms[obj.name]
                obj.location = transform['location']
                obj.rotation_euler = transform['rotation']
                obj.scale = transform['scale']

def export_with_material_baking(filepath, texture_size=1024):
    """Export USDZ with baked materials for better compatibility"""
    
    selected_objects = bpy.context.selected_objects
    if not selected_objects:
        print("No objects selected")
        return False
    
    # This is a simplified version - full material baking is complex
    print(f"Exporting USDZ with material considerations...")
    
    # Ensure materials are properly set up
    for obj in selected_objects:
        if obj.type == 'MESH' and obj.data.materials:
            for mat in obj.data.materials:
                if mat and mat.use_nodes:
                    # Ensure material has proper output
                    output_node = None
                    for node in mat.node_tree.nodes:
                        if node.type == 'OUTPUT_MATERIAL':
                            output_node = node
                            break
                    
                    if output_node:
                        print(f"  Material '{mat.name}' has proper node setup")
    
    # Export with material settings
    material_settings = {
        'export_materials': True,
        'export_textures': True,
        'overwrite_textures': True,
        'generate_preview_surface': True,
    }
    
    return export_selected_as_usdz(filepath, **material_settings)

def get_usdz_export_info():
    """Display information about USDZ export capabilities"""
    print("=" * 60)
    print("BLENDER USDZ EXPORT INFORMATION")
    print("=" * 60)
    
    print("\nUSDZ Export Features:")
    print("• Geometry: Meshes, curves, surfaces")
    print("• Materials: PBR materials with textures")
    print("• UV Mapping: Required for proper texturing")
    print("• Instancing: Efficient for repeated objects")
    print("• Animation: Basic animation support")
    
    print("\nAR/iOS Compatibility:")
    print("• Optimized for Apple AR Quick Look")
    print("• Supports PBR materials")
    print("• Automatic LOD generation")
    print("• Texture compression")
    
    print("\nBest Practices:")
    print("• Apply transformations before export")
    print("• Ensure proper UV mapping")
    print("• Use PBR materials when possible")
    print("• Keep geometry reasonably low-poly for AR")
    print("• Test on target devices")

# Example usage functions
def example_exports():
    """Example of different USDZ export scenarios"""
    
    print("EXAMPLE: USDZ Export Scenarios")
    print("-" * 40)
    
    # Example 1: Export selected objects
    #if bpy.context.selected_objects:
    #    export_selected_as_usdz("N:/data/tinyusdz/tmp/selected_objects.usdz")
    
    # Example 2: Export specific objects by name
    # export_objects_by_name(["Cube", "Sphere"], "/tmp/specific_objects.usdz")
    
    # Example 3: Export collection
    # export_collection_as_usdz("Collection", "/tmp/collection.usdz")
    
    # Example 4: Batch export
    # batch_export_objects_as_usdz("/tmp/batch_export/")
    
    # Example 5: AR-optimized export
    # create_ar_optimized_usdz("/tmp/ar_model.usdz", scale_factor=0.1)

get_usdz_export_info()

batch_export_objects_as_usdz("N:/data/tinyusdz/tmp/", object_filter=asset_object_filter)
