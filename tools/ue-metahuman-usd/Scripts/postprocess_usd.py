#!/usr/bin/env python3
"""Build a portable hero-bust layer around UE's geometry-only USD export.

UE's native USD plugin has no groom exporter.  When the project groom bridge
has emitted its editable HairDescription, this layer references those authored
UsdGeomBasisCurves and retains camera-independent cards as a raster fallback.
The deterministic curves remain only as a fallback when no groom is installed.
"""

import argparse
import json
import math
import os
import random


def tuple3(value):
    return "(" + ", ".join(f"{v:.6g}" for v in value) + ")"


def usd_string(value):
    return '"' + str(value).replace('\\', '\\\\').replace('"', '\\"') + '"'


def write_extension_manifests(output_dir):
    """Preserve engine-specific deformation and ragdoll data as custom USD.

    UsdSkel already owns the skeleton, joint weights and ordinary blend shapes.
    MetaHuman DNA/RigLogic and UE PhysicsAsset topology do not have a lossless
    standard USD representation, so use namespaced custom properties rather
    than mislabeling them as UsdSkel or UsdPhysics data.
    """
    inventory_path = os.path.join(output_dir, "inventory.json")
    inventory = {}
    if os.path.exists(inventory_path):
        with open(inventory_path, "r", encoding="utf-8") as stream:
            inventory = json.load(stream)
    dna = inventory.get("dna", {})
    dna_files = [os.path.basename(path) for path in dna.get("files", [])]
    missing = dna.get("missing", [])
    deformer = os.path.join(output_dir, "MetaHuman_Deformers.usda")
    with open(deformer, "w", encoding="utf-8") as stream:
        stream.write('''#usda 1.0
(
    defaultPrim = "MetaHumanDeformers"
    metersPerUnit = 0.01
    upAxis = "Z"
)

def Scope "MetaHumanDeformers"
{
    custom string unreal:deformerFormat = "MetaHuman DNA / RigLogic"
    custom string unreal:usdSkelCoverage = "skeleton, bind/rest transforms, joint indices and joint weights are in the companion USDC layers"
    custom string[] unreal:deformerTypes = ["DNA blend-shape targets", "GUI controls", "PSD controls", "joint groups", "animated maps"]
''')
        if dna_files:
            stream.write("    custom asset[] unreal:dnaFiles = [" +
                         ", ".join("@" + filename + "@" for filename in dna_files) + "]\n")
        for entry in missing:
            stream.write("    custom string unreal:missingRig:" + entry.get("rig", "unknown") +
                         " = " + usd_string(entry.get("reason", "unavailable")) + "\n")
        stream.write("}\n")

    physics = os.path.join(output_dir, "MetaHuman_Physics.usda")
    with open(physics, "w", encoding="utf-8") as stream:
        stream.write('''#usda 1.0
(
    defaultPrim = "MetaHumanPhysics"
    metersPerUnit = 0.01
    upAxis = "Z"
)

def Scope "MetaHumanPhysics"
{
    custom string unreal:representation = "Unreal PhysicsAsset bodies and constraints bound to a UsdSkel skeleton"
    custom string unreal:usdPhysicsStatus = "custom metadata; no lossless PhysicsAsset-to-UsdPhysics mapping is assumed"
''')
        for entry in inventory.get("physics", []):
            label = entry.get("skeletal_mesh", "Mesh").rsplit("/", 1)[-1]
            stream.write("    def Scope " + usd_string(label) + "\n    {\n")
            stream.write("        custom string unreal:skeletalMesh = " + usd_string(entry.get("skeletal_mesh", "")) + "\n")
            stream.write("        custom string unreal:physicsAsset = " + usd_string(entry.get("physics_asset", "")) + "\n")
            stream.write("        custom string unreal:status = " + usd_string(entry.get("status", "unassigned")) + "\n")
            stream.write("        custom int unreal:bodyCount = " + str(entry.get("body_count", 0)) + "\n")
            stream.write("        custom int unreal:constraintCount = " + str(entry.get("constraint_count", 0)) + "\n")
            stream.write("    }\n")
        stream.write("}\n")
    return deformer, physics


def make_strands(count, points_per_strand):
    rng = random.Random(5802)
    points = []
    widths = []
    colors = []
    counts = []
    for index in range(count):
        u = (index + 0.5) / count
        azimuth = index * 2.39996323
        radius = 12.0 * math.sqrt(u)
        x0 = math.cos(azimuth) * radius
        y0 = math.sin(azimuth) * radius - 0.7
        dome = math.sqrt(max(0.0, 1.0 - (radius / 15.5) ** 2))
        z0 = 162.0 + 11.0 * dome
        length = 7.0 + 18.0 * u + rng.uniform(-1.5, 1.5)
        direction = (0.22 * math.sin(azimuth), 0.16 * math.cos(azimuth), -1.0)
        for step in range(points_per_strand):
            t = step / (points_per_strand - 1)
            curl = 0.55 * math.sin(t * 8.0 + azimuth)
            points.append((x0 + direction[0] * length * t + curl * math.cos(azimuth),
                           y0 + direction[1] * length * t + curl * math.sin(azimuth),
                           z0 + direction[2] * length * t - 2.0 * t * t))
            widths.append(0.095 * (1.0 - 0.82 * t))
            shade = 0.025 + 0.018 * rng.random()
            colors.append((shade * 1.35, shade * 0.72, shade * 0.34))
        counts.append(points_per_strand)
    return counts, points, widths, colors


def make_cards(count):
    points = []
    indices = []
    colors = []
    for index in range(count):
        a = 2.0 * math.pi * index / count
        radius = 11.7
        root = (math.cos(a) * radius, math.sin(a) * radius, 166.0)
        tip = (root[0] * 1.08, root[1] * 1.08, 148.0 + 3.0 * math.sin(a * 3.0))
        side = (-math.sin(a) * 1.45, math.cos(a) * 1.45, 0.0)
        base = len(points)
        points.extend([(root[0] - side[0], root[1] - side[1], root[2]),
                       (root[0] + side[0], root[1] + side[1], root[2]),
                       (tip[0] + side[0] * 0.35, tip[1] + side[1] * 0.35, tip[2]),
                       (tip[0] - side[0] * 0.35, tip[1] - side[1] * 0.35, tip[2])])
        indices.extend([base, base + 1, base + 2, base + 3])
        colors.extend([(0.035, 0.016, 0.007)] * 4)
    return points, indices, colors


def embedded_groom_layer(groom_path):
    """Inline the authored prim because the viewer's curve carrier does not
    yet cross an external reference arc (meshes do)."""
    with open(groom_path, "r", encoding="utf-8") as stream:
        source = stream.read()
    start = source.find('def BasisCurves "HairStrands"')
    if start < 0:
        raise RuntimeError(f"No HairStrands BasisCurves prim in {groom_path}")
    prim = source[start:].strip()
    closing = prim.rfind("\n}")
    if closing < 0:
        raise RuntimeError(f"Unterminated HairStrands prim in {groom_path}")
    prim = (prim[:closing] +
            '\n    rel material:binding = </MetaHuman/Materials/Hair>' +
            prim[closing:])
    return "\n".join("    " + line if line else line for line in prim.splitlines())


def build_layer(head_name, body_name, strand_count, native_groom_path):
    counts, strand_points, widths, colors = make_strands(strand_count, 7)
    card_points, card_indices, card_colors = make_cards(36)
    strand_layer = (embedded_groom_layer(native_groom_path)
                    if native_groom_path else f'''    def BasisCurves "HairStrands"
    {{
        float3[] extent = [(-16, -16, 135), (16, 16, 175)]
        uniform token type = "linear"
        uniform token basis = "bezier"
        uniform token wrap = "nonperiodic"
        int[] curveVertexCounts = [{', '.join(map(str, counts))}]
        point3f[] points = [{', '.join(map(tuple3, strand_points))}]
        float[] widths = [{', '.join(f'{v:.6g}' for v in widths)}] (interpolation = "vertex")
        color3f[] primvars:displayColor = [{', '.join(map(tuple3, colors))}] (interpolation = "vertex")
        rel material:binding = </MetaHuman/Materials/Hair>
    }}
''')
    cards_layer = ("" if native_groom_path else f'''    def Mesh "HairCards"
    {{
        float3[] extent = [(-16, -16, 145), (16, 16, 168)]
        uniform bool doubleSided = 1
        int[] faceVertexCounts = [{', '.join(['4'] * 36)}]
        int[] faceVertexIndices = [{', '.join(map(str, card_indices))}]
        point3f[] points = [{', '.join(map(tuple3, card_points))}]
        normal3f[] normals = [{', '.join(['(0, 1, 0)'] * (36 * 4))}] (interpolation = "faceVarying")
        color3f[] primvars:displayColor = [{', '.join(map(tuple3, card_colors))}] (interpolation = "vertex")
        texCoord2f[] primvars:st = [{', '.join(['(0,0)', '(1,0)', '(1,1)', '(0,1)'] * 36)}] (interpolation = "faceVarying")
        rel material:binding = </MetaHuman/Materials/Hair>
    }}
''')
    return f'''#usda 1.0
(
    defaultPrim = "MetaHuman"
    metersPerUnit = 0.01
    upAxis = "Z"
)

def Xform "MetaHuman"
{{
    def Xform "Body" (prepend references = @{body_name}@)
    {{
        over "TinyUSDZ_DefaultHuman_Body"
        {{
            rel material:binding = </MetaHuman/Materials/Skin>
        }}
    }}
    def Xform "Head" (prepend references = @{head_name}@)
    {{
        over "TinyUSDZ_DefaultHuman_Head"
        {{
            rel material:binding = </MetaHuman/Materials/Skin>
            over "Section0" {{ rel material:binding = </MetaHuman/Materials/Skin> }}
            over "Section1" {{ rel material:binding = </MetaHuman/Materials/Skin> }}
            over "Section2" {{ rel material:binding = </MetaHuman/Materials/Skin> }}
            over "Section3" {{ rel material:binding = </MetaHuman/Materials/Skin> }}
            over "Section4" {{ rel material:binding = </MetaHuman/Materials/Skin> }}
            over "Section5" {{ rel material:binding = </MetaHuman/Materials/Skin> }}
            over "Section6" {{ rel material:binding = </MetaHuman/Materials/Skin> }}
            over "Section7" {{ rel material:binding = </MetaHuman/Materials/Skin> }}
            over "Section8" {{ rel material:binding = </MetaHuman/Materials/Skin> }}
        }}
    }}

    def Scope "Materials"
    {{
        def Material "Skin" (prepend apiSchemas = ["MaterialXConfigAPI"])
        {{
            string config:mtlx:version = "1.38"
            token outputs:mtlx:surface.connect = </MetaHuman/Materials/Skin/OpenPBR.outputs:out>
            def Shader "OpenPBR"
            {{
                uniform token info:id = "ND_open_pbr_surface_surfaceshader"
                color3f inputs:base_color = (0.50, 0.205, 0.135)
                float inputs:base_roughness = 0.48
                float inputs:specular_weight = 0.42
                float inputs:specular_roughness = 0.32
                float inputs:subsurface_weight = 0.68
                color3f inputs:subsurface_color = (1.0, 0.24, 0.12)
                float inputs:subsurface_radius = 0.16
                color3f inputs:subsurface_radius_scale = (1.0, 0.35, 0.18)
                float inputs:subsurface_scale = 0.12
                token outputs:out
            }}
        }}
        def Material "Hair" (prepend apiSchemas = ["MaterialXConfigAPI"])
        {{
            string config:mtlx:version = "1.38"
            custom string tinyusdz:referenceBsdf = "chiang_hair_bsdf"
            token outputs:mtlx:surface.connect = </MetaHuman/Materials/Hair/OpenPBR.outputs:out>
            def Shader "OpenPBR"
            {{
                uniform token info:id = "ND_open_pbr_surface_surfaceshader"
                # Keep the authored groom's dark fibers readable in the
                # raster R/TT/TRT approximation; the lobes themselves apply
                # the melanin-like per-strand display color.
                color3f inputs:base_color = (0.16, 0.055, 0.015)
                float inputs:base_roughness = 0.52
                float inputs:specular_weight = 0.35
                float inputs:specular_roughness = 0.28
                float inputs:specular_anisotropy = 0.88
                float inputs:coat_weight = 0.18
                token outputs:out
            }}
        }}
    }}

{strand_layer}

{cards_layer}
}}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--strands", type=int, default=1200)
    args = parser.parse_args()
    output_dir = os.path.abspath(args.output_dir)
    groom_path = os.path.join(output_dir, "MetaHuman_GroomStrands.usda")
    if not os.path.exists(groom_path):
        groom_path = ""
    layer = build_layer("TinyUSDZ_DefaultHuman_Head.usdc",
                        "TinyUSDZ_DefaultHuman_Body.usdc", args.strands, groom_path)
    destination = os.path.join(output_dir, "MetaHuman_Hero.usda")
    with open(destination, "w", encoding="utf-8") as stream:
        stream.write(layer)
    rig_manifest = os.path.join(output_dir, "MetaHuman_Rig.usda")
    head_dna = os.path.exists(os.path.join(output_dir, "TinyUSDZ_DefaultHuman_Head.dna"))
    head_asset = ('custom asset unreal:headDNA = @TinyUSDZ_DefaultHuman_Head.dna@'
                  if head_dna else
                  'custom string unreal:headDNAStatus = "missing: offline preset requires UE cloud auto-rigging"')
    with open(rig_manifest, "w", encoding="utf-8") as stream:
        stream.write(f'''#usda 1.0
(
    defaultPrim = "MetaHumanRig"
    metersPerUnit = 0.01
    upAxis = "Z"
)

def Scope "MetaHumanRig"
{{
    {head_asset}
    custom asset unreal:bodyDNA = @TinyUSDZ_DefaultHuman_Body.dna@
    custom string unreal:rigFormat = "MetaHuman DNA / RigLogic"
    custom string unreal:usdDeformation = "UsdSkel skeleton, bind transforms, joint indices and joint weights"
    custom string unreal:facialDeformation = "DNA blend-shape targets, GUI controls, PSD controls and animated maps"
}}
''')
    write_extension_manifests(output_dir)
    print(destination)


if __name__ == "__main__":
    main()
