"""Generate deterministic straight/wavy Blender Hair Curves and export USD.

Run with Blender 5.2+:
  blender -b --python blender-generate-hair.py -- output.usdc output.blend
"""
import math
import os
import random
import sys

import bpy
from mathutils import Vector


def script_args():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if not args:
        raise RuntimeError("expected output.usdc [output.blend]")
    return os.path.abspath(args[0]), os.path.abspath(args[1]) if len(args) > 1 else ""


def make_hair_material(name, color, roughness, radial_roughness):
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    nodes = material.node_tree.nodes
    nodes.clear()
    output = nodes.new("ShaderNodeOutputMaterial")
    hair = nodes.new("ShaderNodeBsdfHairPrincipled")
    hair.parametrization = "COLOR"
    hair.inputs["Color"].default_value = (*color, 1.0)
    hair.inputs["Roughness"].default_value = roughness
    hair.inputs["Radial Roughness"].default_value = radial_roughness
    hair.inputs["IOR"].default_value = 1.55
    hair.inputs["Offset"].default_value = math.radians(3.0)
    hair.inputs["Random Roughness"].default_value = 0.06
    material.node_tree.links.new(hair.outputs["BSDF"], output.inputs["Surface"])
    return material


def scalp_root(strand, strand_count, center):
    # Fibonacci distribution over an ellipsoidal upper cap, excluding the face.
    golden = math.pi * (3.0 - math.sqrt(5.0))
    t = (strand + 0.5) / strand_count
    y = 1.0 - 1.72 * t
    radial = math.sqrt(max(0.0, 1.0 - y * y))
    phi = strand * golden
    normal = Vector((math.cos(phi) * radial, math.sin(phi) * radial, y))
    root = Vector(center) + Vector((normal.x * 0.72, normal.y * 0.78, normal.z * 0.92))
    return root, normal


def strand_points(strand, strand_count, point_count, center, wavy, rng):
    root, normal = scalp_root(strand, strand_count, center)
    phase = rng.random() * math.tau
    length = (1.15 + rng.random() * 0.42) * (0.88 + 0.12 * (1.0 - normal.y))
    side = Vector((-normal.y, normal.x, 0.0))
    if side.length_squared < 1e-8:
        side = Vector((1.0, 0.0, 0.0))
    side.normalize()
    points = []
    for index in range(point_count):
        u = index / (point_count - 1)
        gravity = Vector((0.0, 0.0, -length * u))
        outward = normal * (0.09 * math.sin(math.pi * u))
        settle = Vector((normal.x, normal.y, 0.0)) * (0.19 * u)
        wave = Vector()
        if wavy:
            amplitude = (0.035 + 0.035 * u) * math.sin(math.pi * u)
            wave = side * (amplitude * math.sin(phase + u * math.tau * 4.5))
            wave += normal.cross(side) * (amplitude * 0.48 * math.cos(phase + u * math.tau * 4.5))
        points.append(root + gravity + outward + settle + wave)
    return points


def make_hair_object(name, center, strand_count, point_count, wavy, material, seed):
    rng = random.Random(seed)
    curves = bpy.data.hair_curves.new(name)
    curves.add_curves([point_count] * strand_count)
    coordinates = []
    radii = []
    for strand in range(strand_count):
        for point_index, point in enumerate(
                strand_points(strand, strand_count, point_count, center, wavy, rng)):
            coordinates.extend(point)
            u = point_index / (point_count - 1)
            radii.append(0.0045 * (1.0 - 0.92 * u))
    curves.attributes["position"].data.foreach_set("vector", coordinates)
    radius = curves.attributes.new("radius", "FLOAT", "POINT")
    radius.data.foreach_set("value", radii)
    curves.materials.append(material)
    obj = bpy.data.objects.new(name, curves)
    bpy.context.collection.objects.link(obj)
    return obj


def make_scalp(name, center, material):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=64, ring_count=32, location=center)
    scalp = bpy.context.object
    scalp.name = name
    scalp.scale = (0.70, 0.76, 0.90)
    scalp.data.materials.append(material)
    return scalp


def main():
    output_usd, output_blend = script_args()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.world = bpy.data.worlds.new("HairWorld")
    scene.world.color = (0.025, 0.025, 0.035)
    straight = make_hair_material("Straight_PrincipledHair", (0.16, 0.035, 0.012), 0.22, 0.34)
    wavy = make_hair_material("Wavy_PrincipledHair", (0.42, 0.15, 0.035), 0.29, 0.43)
    scalp_material = bpy.data.materials.new("Scalp")
    scalp_material.diffuse_color = (0.045, 0.018, 0.012, 1.0)
    make_scalp("Straight_Scalp", (-1.15, 0.0, 0.75), scalp_material)
    make_scalp("Wavy_Scalp", (1.15, 0.0, 0.75), scalp_material)
    make_hair_object("wStraight", (-1.15, 0.0, 0.75), 5000, 24, False, straight, 8173)
    make_hair_object("wWavy", (1.15, 0.0, 0.75), 5000, 48, True, wavy, 2917)
    scene["hair_source"] = "Inspired by Cem Yuksel HAIR model geometry"
    scene["hair_source_url"] = "https://www.cemyuksel.com/research/hairmodels/"
    scene["strand_count"] = 10000
    if output_blend:
        os.makedirs(os.path.dirname(output_blend), exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=output_blend, compress=True)
    os.makedirs(os.path.dirname(output_usd), exist_ok=True)
    result = bpy.ops.wm.usd_export(
        filepath=output_usd,
        selected_objects_only=False,
        export_hair=True,
        export_curves=True,
        export_materials=True,
        generate_materialx_network=True,
        convert_world_material=False,
        evaluation_mode="RENDER",
        relative_paths=False,
    )
    if "FINISHED" not in result:
        raise RuntimeError(f"USD export failed: {result}")
    print(f"HAIR_EXPORT usd={output_usd} strands=10000 points=360000")


if __name__ == "__main__":
    main()
