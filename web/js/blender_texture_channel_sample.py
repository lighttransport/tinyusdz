#!/usr/bin/env python3
"""
Blender Python script to create texture channel extraction sample.
Creates synthetic textures and materials demonstrating channel separation/combination.

Usage:
    blender --background --python blender_texture_channel_sample.py

Output:
    assets/texture_channel_sample.usdz
    assets/textures/channel_test_rgb.png (synthetic RGB texture)
"""

import bpy
import os
import numpy as np
from pathlib import Path

# Output paths
SCRIPT_DIR = Path(__file__).parent
ASSETS_DIR = SCRIPT_DIR / "assets"
TEXTURES_DIR = ASSETS_DIR / "textures"
OUTPUT_FILE = ASSETS_DIR / "texture_channel_sample.usdz"

def ensure_dirs():
    """Create output directories if they don't exist."""
    ASSETS_DIR.mkdir(exist_ok=True)
    TEXTURES_DIR.mkdir(exist_ok=True)

def clear_scene():
    """Clear all objects and materials from the scene."""
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)

    for mat in bpy.data.materials:
        bpy.data.materials.remove(mat)

    for img in bpy.data.images:
        bpy.data.images.remove(img)

def create_synthetic_rgb_texture(width=256, height=256):
    """
    Create a synthetic RGB texture where each channel has distinct patterns:
    - R channel: Horizontal gradient
    - G channel: Vertical gradient
    - B channel: Circular gradient from center
    """
    # Create numpy array
    pixels = np.zeros((height, width, 4), dtype=np.float32)

    for y in range(height):
        for x in range(width):
            # Normalize coordinates
            u = x / (width - 1)
            v = y / (height - 1)

            # R: Horizontal gradient (left=0, right=1)
            r = u

            # G: Vertical gradient (bottom=0, top=1)
            g = v

            # B: Radial gradient from center
            cx, cy = 0.5, 0.5
            dist = np.sqrt((u - cx)**2 + (v - cy)**2)
            b = 1.0 - min(dist * 2, 1.0)  # Bright in center, dark at edges

            pixels[y, x] = [r, g, b, 1.0]

    # Create Blender image
    img = bpy.data.images.new("ChannelTestRGB", width=width, height=height, alpha=True)
    img.pixels = pixels.flatten()
    img.pack()

    # Also save to disk
    img.filepath_raw = str(TEXTURES_DIR / "channel_test_rgb.png")
    img.file_format = 'PNG'
    img.save()

    return img

def create_synthetic_mask_texture(width=256, height=256):
    """
    Create a mask texture with checkerboard pattern in R,
    stripes in G, and noise in B.
    """
    pixels = np.zeros((height, width, 4), dtype=np.float32)

    np.random.seed(42)  # Reproducible noise

    for y in range(height):
        for x in range(width):
            # R: Checkerboard (32x32 cells)
            cell_size = 32
            checker = ((x // cell_size) + (y // cell_size)) % 2
            r = float(checker)

            # G: Horizontal stripes
            stripe_size = 16
            g = float((y // stripe_size) % 2)

            # B: Smooth noise (using simple pattern for reproducibility)
            b = (np.sin(x * 0.1) * np.cos(y * 0.1) + 1) * 0.5

            pixels[y, x] = [r, g, b, 1.0]

    img = bpy.data.images.new("ChannelMask", width=width, height=height, alpha=True)
    img.pixels = pixels.flatten()
    img.pack()

    img.filepath_raw = str(TEXTURES_DIR / "channel_mask.png")
    img.file_format = 'PNG'
    img.save()

    return img

def create_channel_extraction_material(name, texture_img):
    """
    Create a material that demonstrates channel extraction:
    - Separates RGB channels
    - Uses R channel for roughness
    - Uses G channel for metalness
    - Uses B channel mixed with base color
    """
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    tree = mat.node_tree
    nodes = tree.nodes
    links = tree.links

    # Clear default nodes
    nodes.clear()

    # Create output node
    output = nodes.new('ShaderNodeOutputMaterial')
    output.location = (600, 0)

    # Create Principled BSDF
    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    bsdf.location = (300, 0)
    links.new(bsdf.outputs['BSDF'], output.inputs['Surface'])

    # Create texture coordinate
    tex_coord = nodes.new('ShaderNodeTexCoord')
    tex_coord.location = (-600, 0)

    # Create image texture node
    tex_image = nodes.new('ShaderNodeTexImage')
    tex_image.location = (-400, 0)
    tex_image.image = texture_img
    links.new(tex_coord.outputs['UV'], tex_image.inputs['Vector'])

    # Create Separate Color node (RGB separation)
    separate = nodes.new('ShaderNodeSeparateColor')
    separate.location = (-150, 0)
    links.new(tex_image.outputs['Color'], separate.inputs['Color'])

    # Use R channel for Roughness
    links.new(separate.outputs['Red'], bsdf.inputs['Roughness'])

    # Use G channel for Metallic
    links.new(separate.outputs['Green'], bsdf.inputs['Metallic'])

    # Create a color for base and mix with B channel
    base_color = nodes.new('ShaderNodeRGB')
    base_color.location = (-150, -200)
    base_color.outputs[0].default_value = (0.8, 0.4, 0.2, 1.0)  # Orange-ish

    mix_color = nodes.new('ShaderNodeMixRGB')
    mix_color.location = (50, -100)
    mix_color.blend_type = 'MIX'
    links.new(separate.outputs['Blue'], mix_color.inputs['Fac'])
    links.new(base_color.outputs[0], mix_color.inputs['Color1'])

    # Second color for mix
    second_color = nodes.new('ShaderNodeRGB')
    second_color.location = (-150, -350)
    second_color.outputs[0].default_value = (0.2, 0.5, 0.9, 1.0)  # Blue-ish
    links.new(second_color.outputs[0], mix_color.inputs['Color2'])

    links.new(mix_color.outputs['Color'], bsdf.inputs['Base Color'])

    return mat

def create_swizzle_material(name, texture_img):
    """
    Create a material that swizzles channels (RGB -> BGR).
    """
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    tree = mat.node_tree
    nodes = tree.nodes
    links = tree.links

    nodes.clear()

    output = nodes.new('ShaderNodeOutputMaterial')
    output.location = (600, 0)

    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    bsdf.location = (300, 0)
    links.new(bsdf.outputs['BSDF'], output.inputs['Surface'])

    tex_coord = nodes.new('ShaderNodeTexCoord')
    tex_coord.location = (-600, 0)

    tex_image = nodes.new('ShaderNodeTexImage')
    tex_image.location = (-400, 0)
    tex_image.image = texture_img
    links.new(tex_coord.outputs['UV'], tex_image.inputs['Vector'])

    # Separate
    separate = nodes.new('ShaderNodeSeparateColor')
    separate.location = (-150, 0)
    links.new(tex_image.outputs['Color'], separate.inputs['Color'])

    # Combine with swizzled channels (BGR instead of RGB)
    combine = nodes.new('ShaderNodeCombineColor')
    combine.location = (50, 0)
    links.new(separate.outputs['Blue'], combine.inputs['Red'])    # B -> R
    links.new(separate.outputs['Green'], combine.inputs['Green']) # G -> G
    links.new(separate.outputs['Red'], combine.inputs['Blue'])    # R -> B

    links.new(combine.outputs['Color'], bsdf.inputs['Base Color'])

    # Set roughness
    bsdf.inputs['Roughness'].default_value = 0.4

    return mat

def create_grayscale_material(name, texture_img):
    """
    Create a material that extracts one channel and uses it as grayscale.
    """
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    tree = mat.node_tree
    nodes = tree.nodes
    links = tree.links

    nodes.clear()

    output = nodes.new('ShaderNodeOutputMaterial')
    output.location = (600, 0)

    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    bsdf.location = (300, 0)
    links.new(bsdf.outputs['BSDF'], output.inputs['Surface'])

    tex_coord = nodes.new('ShaderNodeTexCoord')
    tex_coord.location = (-600, 0)

    tex_image = nodes.new('ShaderNodeTexImage')
    tex_image.location = (-400, 0)
    tex_image.image = texture_img
    links.new(tex_coord.outputs['UV'], tex_image.inputs['Vector'])

    # Separate
    separate = nodes.new('ShaderNodeSeparateColor')
    separate.location = (-150, 0)
    links.new(tex_image.outputs['Color'], separate.inputs['Color'])

    # Combine R channel to all (grayscale from R)
    combine = nodes.new('ShaderNodeCombineColor')
    combine.location = (50, 0)
    links.new(separate.outputs['Red'], combine.inputs['Red'])
    links.new(separate.outputs['Red'], combine.inputs['Green'])
    links.new(separate.outputs['Red'], combine.inputs['Blue'])

    links.new(combine.outputs['Color'], bsdf.inputs['Base Color'])

    bsdf.inputs['Roughness'].default_value = 0.5

    return mat

def create_single_channel_mod_material(name, texture_img):
    """
    Create a material that modifies only one channel.
    Extracts RGB, inverts G, recombines.
    """
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    tree = mat.node_tree
    nodes = tree.nodes
    links = tree.links

    nodes.clear()

    output = nodes.new('ShaderNodeOutputMaterial')
    output.location = (700, 0)

    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    bsdf.location = (400, 0)
    links.new(bsdf.outputs['BSDF'], output.inputs['Surface'])

    tex_coord = nodes.new('ShaderNodeTexCoord')
    tex_coord.location = (-600, 0)

    tex_image = nodes.new('ShaderNodeTexImage')
    tex_image.location = (-400, 0)
    tex_image.image = texture_img
    links.new(tex_coord.outputs['UV'], tex_image.inputs['Vector'])

    # Separate
    separate = nodes.new('ShaderNodeSeparateColor')
    separate.location = (-150, 0)
    links.new(tex_image.outputs['Color'], separate.inputs['Color'])

    # Invert just the G channel
    invert_g = nodes.new('ShaderNodeMath')
    invert_g.location = (50, 0)
    invert_g.operation = 'SUBTRACT'
    invert_g.inputs[0].default_value = 1.0
    links.new(separate.outputs['Green'], invert_g.inputs[1])

    # Combine with modified G
    combine = nodes.new('ShaderNodeCombineColor')
    combine.location = (200, 0)
    links.new(separate.outputs['Red'], combine.inputs['Red'])
    links.new(invert_g.outputs['Value'], combine.inputs['Green'])
    links.new(separate.outputs['Blue'], combine.inputs['Blue'])

    links.new(combine.outputs['Color'], bsdf.inputs['Base Color'])

    bsdf.inputs['Roughness'].default_value = 0.3

    return mat

def create_mask_blend_material(name, color_img, mask_img):
    """
    Create a material that uses mask channels to blend colors.
    """
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    tree = mat.node_tree
    nodes = tree.nodes
    links = tree.links

    nodes.clear()

    output = nodes.new('ShaderNodeOutputMaterial')
    output.location = (800, 0)

    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    bsdf.location = (500, 0)
    links.new(bsdf.outputs['BSDF'], output.inputs['Surface'])

    tex_coord = nodes.new('ShaderNodeTexCoord')
    tex_coord.location = (-700, 0)

    # Color texture
    tex_color = nodes.new('ShaderNodeTexImage')
    tex_color.location = (-500, 100)
    tex_color.image = color_img
    tex_color.label = "Color Texture"
    links.new(tex_coord.outputs['UV'], tex_color.inputs['Vector'])

    # Mask texture
    tex_mask = nodes.new('ShaderNodeTexImage')
    tex_mask.location = (-500, -200)
    tex_mask.image = mask_img
    tex_mask.label = "Mask Texture"
    links.new(tex_coord.outputs['UV'], tex_mask.inputs['Vector'])

    # Separate mask channels
    separate_mask = nodes.new('ShaderNodeSeparateColor')
    separate_mask.location = (-250, -200)
    links.new(tex_mask.outputs['Color'], separate_mask.inputs['Color'])

    # Base colors
    color1 = nodes.new('ShaderNodeRGB')
    color1.location = (-250, 300)
    color1.outputs[0].default_value = (1.0, 0.2, 0.2, 1.0)  # Red

    color2 = nodes.new('ShaderNodeRGB')
    color2.location = (-250, 150)
    color2.outputs[0].default_value = (0.2, 1.0, 0.2, 1.0)  # Green

    # Mix using mask R channel (checkerboard)
    mix1 = nodes.new('ShaderNodeMixRGB')
    mix1.location = (0, 200)
    links.new(separate_mask.outputs['Red'], mix1.inputs['Fac'])
    links.new(color1.outputs[0], mix1.inputs['Color1'])
    links.new(color2.outputs[0], mix1.inputs['Color2'])

    # Mix with original texture using mask G channel (stripes)
    mix2 = nodes.new('ShaderNodeMixRGB')
    mix2.location = (200, 100)
    links.new(separate_mask.outputs['Green'], mix2.inputs['Fac'])
    links.new(mix1.outputs['Color'], mix2.inputs['Color1'])
    links.new(tex_color.outputs['Color'], mix2.inputs['Color2'])

    links.new(mix2.outputs['Color'], bsdf.inputs['Base Color'])

    # Use mask B channel for roughness
    links.new(separate_mask.outputs['Blue'], bsdf.inputs['Roughness'])

    return mat

def create_test_object(name, location, material):
    """Create a test sphere with the given material."""
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.8, segments=32, ring_count=16, location=location)
    obj = bpy.context.active_object
    obj.name = name

    if obj.data.materials:
        obj.data.materials[0] = material
    else:
        obj.data.materials.append(material)

    return obj

def export_usdz():
    """Export scene to USDZ with MaterialX."""
    # Select all mesh objects
    bpy.ops.object.select_all(action='DESELECT')
    for obj in bpy.data.objects:
        if obj.type == 'MESH':
            obj.select_set(True)

    # Export options
    export_path = str(OUTPUT_FILE)

    bpy.ops.wm.usd_export(
        filepath=export_path,
        selected_objects_only=True,
        export_materials=True,
        generate_materialx_network=True,
        export_textures=True,
        overwrite_textures=True,
        relative_paths=True
    )

    print(f"Exported to: {export_path}")

def main():
    print("Creating texture channel extraction sample...")

    ensure_dirs()
    clear_scene()

    # Create synthetic textures
    print("Creating synthetic RGB texture...")
    rgb_texture = create_synthetic_rgb_texture(256, 256)

    print("Creating synthetic mask texture...")
    mask_texture = create_synthetic_mask_texture(256, 256)

    # Create materials with different channel operations
    print("Creating materials...")

    # Material 1: Channel extraction (R->roughness, G->metallic, B->color mix)
    mat_extraction = create_channel_extraction_material("ChannelExtraction", rgb_texture)

    # Material 2: Channel swizzle (RGB -> BGR)
    mat_swizzle = create_swizzle_material("ChannelSwizzle", rgb_texture)

    # Material 3: Grayscale from single channel
    mat_grayscale = create_grayscale_material("GrayscaleFromR", rgb_texture)

    # Material 4: Single channel modification
    mat_single_mod = create_single_channel_mod_material("SingleChannelMod", rgb_texture)

    # Material 5: Mask-based blending
    mat_mask_blend = create_mask_blend_material("MaskBlend", rgb_texture, mask_texture)

    # Create test objects
    print("Creating test objects...")
    create_test_object("Sphere_ChannelExtraction", (-3, 0, 0), mat_extraction)
    create_test_object("Sphere_Swizzle", (-1.5, 0, 0), mat_swizzle)
    create_test_object("Sphere_Grayscale", (0, 0, 0), mat_grayscale)
    create_test_object("Sphere_SingleMod", (1.5, 0, 0), mat_single_mod)
    create_test_object("Sphere_MaskBlend", (3, 0, 0), mat_mask_blend)

    # Add a camera
    bpy.ops.object.camera_add(location=(0, -8, 2))
    camera = bpy.context.active_object
    camera.rotation_euler = (1.4, 0, 0)
    bpy.context.scene.camera = camera

    # Add lighting
    bpy.ops.object.light_add(type='SUN', location=(5, -5, 10))
    sun = bpy.context.active_object
    sun.data.energy = 3.0

    # Export
    print("Exporting to USDZ...")
    export_usdz()

    print("Done!")
    print(f"Output files:")
    print(f"  - {OUTPUT_FILE}")
    print(f"  - {TEXTURES_DIR / 'channel_test_rgb.png'}")
    print(f"  - {TEXTURES_DIR / 'channel_mask.png'}")

if __name__ == "__main__":
    main()
