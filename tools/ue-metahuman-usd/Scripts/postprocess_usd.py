#!/usr/bin/env python3
"""Build a portable hero-bust layer around UE's geometry-only USD export.

The groom bridge is deliberately deterministic and dependency-free: UE groom
assets are proprietary cooked data and UE exposes no native USD groom exporter.
This step authors both UsdGeomBasisCurves and camera-independent hair cards so
the two real-time representations can be evaluated by tusdview.
"""

import argparse
import math
import os
import random


def tuple3(value):
    return "(" + ", ".join(f"{v:.6g}" for v in value) + ")"


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


def build_layer(head_name, body_name, strand_count):
    counts, strand_points, widths, colors = make_strands(strand_count, 7)
    card_points, card_indices, card_colors = make_cards(36)
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
                color3f inputs:base_color = (0.035, 0.016, 0.007)
                float inputs:base_roughness = 0.52
                float inputs:specular_weight = 0.35
                float inputs:specular_roughness = 0.28
                float inputs:specular_anisotropy = 0.88
                float inputs:coat_weight = 0.18
                token outputs:out
            }}
        }}
    }}

    def BasisCurves "HairStrands"
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

    def Mesh "HairCards"
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
}}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--strands", type=int, default=1200)
    args = parser.parse_args()
    output_dir = os.path.abspath(args.output_dir)
    layer = build_layer("TinyUSDZ_DefaultHuman_Head.usdc",
                        "TinyUSDZ_DefaultHuman_Body.usdc", args.strands)
    destination = os.path.join(output_dir, "MetaHuman_Hero.usda")
    with open(destination, "w", encoding="utf-8") as stream:
        stream.write(layer)
    print(destination)


if __name__ == "__main__":
    main()
