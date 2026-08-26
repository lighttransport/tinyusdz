#!/usr/bin/env python3
"""Fail when the host, CUDA/HIP, and Vulkan material layouts drift apart."""

from pathlib import Path
import re
import sys


def require(text: str, pattern: str, label: str) -> None:
    if re.search(pattern, text, re.MULTILINE) is None:
        raise SystemExit(f"GPU material ABI mismatch: {label}")


root = Path(sys.argv[1]).resolve()
abi = (root / "examples/tusdview/gpu_material_abi.h").read_text()
bridge = (root / "examples/tusdview/lightrt_mtlx_bridge.hh").read_text()
kernel = (root / "examples/tusdview/raytracer_kernel_src.txt").read_text()
cpu = (root / "examples/tusdview/cpu/cpu_raytracer.cc").read_text()
shader = (root / "src/external/lightrt/vk/shaders/trace_materialx_path.comp").read_text()
openpbr = (root / "src/tydra/openpbr-params.hh").read_text()
vk_header = (root / "src/external/lightrt/lightrt_c_vk.h").read_text()
vk_source = (root / "src/external/lightrt/lightrt_c_vk.c").read_text()

values = {
    "TUSD_GPU_OPENPBR_FLOATS": 80,
    "TUSD_GPU_MATERIAL_TEX_PARAM_FLOATS": 155,
    "TUSD_GPU_MATERIAL_TEX_SLOTS": 12,
    "TUSD_GPU_GRAPH_OUTPUTS": 48,
    "TUSD_GPU_GRAPH_HEADER_FLOATS": 50,
    "TUSD_GPU_GRAPH_NODE_FLOATS": 21,
    "TUSD_GPU_GRAPH_MAX_NODES": 64,
    "TUSD_GPU_GRAPH_FLOATS": 1394,
    "TUSD_GPU_LIGHTRT_LIGHT_FLOATS": 16,
    "TUSD_GPU_LIGHTRT_TEXTURE_DESC_INTS": 8,
}

for name, value in values.items():
    require(abi, rf"^#define\s+{name}\s+{value}\b", f"canonical {name}")

for name in (
    "TUSD_GPU_OPENPBR_FLOATS",
    "TUSD_GPU_MATERIAL_TEX_PARAM_FLOATS",
    "TUSD_GPU_MATERIAL_TEX_SLOTS",
    "TUSD_GPU_GRAPH_FLOATS",
    "TUSD_GPU_GRAPH_HEADER_FLOATS",
    "TUSD_GPU_GRAPH_NODE_FLOATS",
    "TUSD_GPU_GRAPH_MAX_NODES",
):
    require(kernel, rf"^#define\s+{name}\s+{values[name]}\b", f"CUDA/HIP {name}")

for name, value in {
    "MAT_STRIDE": 80,
    "GRAPH_OUTPUTS": 48,
    "GRAPH_HEADER": 50,
    "GRAPH_NODE": 21,
    "GRAPH_NODES": 64,
    "TEX_DESC_STRIDE": 8,
    "LIGHT_STRIDE": 16,
}.items():
    require(shader, rf"^const uint\s+{name}\s*=\s*{value}u;", f"Vulkan {name}")

require(openpbr, r"kLightRtOpenPBRVec4s\s*=\s*20", "Tydra OpenPBR vec4 count")
require(bridge, r"kLightRtOpenPBRFloats\s*==\s*TUSD_GPU_OPENPBR_FLOATS", "bridge OpenPBR guard")
require(kernel, r"base\s*\+\s*TUSD_GPU_GRAPH_HEADER_FLOATS\s*\+\s*i\s*\*\s*TUSD_GPU_GRAPH_NODE_FLOATS", "CUDA/HIP graph record offset")
require(shader, r"r\s*<\s*GRAPH_OUTPUTS", "Vulkan graph output routing")
require(shader, r"op\s*==\s*21\)\s*value\[i\]\s*=\s*vec4\(uv,\s*0,\s*1\)",
        "Vulkan texcoord uses hit UV")
if re.search(r"base\s*\+\s*46\s*\+\s*i\s*\*\s*21", kernel):
    raise SystemExit("GPU material ABI mismatch: stale CUDA/HIP graph header")
if ("(int)(graph[base + 1 + route] + 0.5f)" in kernel or
        "(int)(graph[p+1]+0.5f)" in kernel or
        "(int)(graph[p+16]+0.5f)" in kernel):
    raise SystemExit("GPU material ABI mismatch: CUDA/HIP truncates -1 graph sentinels")
require(kernel, r"floorf\(graph\[base \+ 1 \+ route\] \+ 0\.5f\)",
        "CUDA/HIP graph output sentinel decode")
require(kernel, r"floorf\(graph\[p\+1\]\+0\.5f\)",
        "CUDA/HIP graph input sentinel decode")
require(kernel, r"floorf\(graph\[p\+16\]\+0\.5f\)",
        "CUDA/HIP graph texture sentinel decode")
for offset, label in (("p \\+ 1", "input"), ("p \\+ 16", "texture")):
    require(cpu, rf"floor\(\s*scene\.matGraph\[{offset}\] \+ 0\.5f\)",
            f"CPU graph {label} sentinel decode")
for operation in ("Arcsine", "Arccosine", "Arctangent", "Contrast", "Screen",
                  "Overlay", "Burn", "Dodge", "RampLR", "RampTB", "SplitLR",
                  "SplitTB", "Saturate", "IfGreater", "IfGreaterEqual",
                  "IfEqual", "RgbToHsv", "HsvToRgb", "Rotate2D", "Distance",
                  "Reflect", "Refract", "Premult", "Unpremult", "MinComponent",
                  "MaxComponent", "LogicalAnd", "LogicalOr", "LogicalXor",
                  "LogicalNot", "Inside", "Outside", "GeomColor", "Bitangent",
                  "Difference", "In", "Mask", "Matte", "Out", "Over",
                  "DisjointOver", "SetAlpha", "CellNoise2D", "CellNoise3D",
                  "Fractal2D", "WorleyNoise2D", "WorleyNoise3D", "Fractal3D",
                  "Swizzle"):
    require(cpu, rf"MaterialXGraphOpCPU::{operation}\b",
            f"CPU graph {operation} parity")
if "r<44" in shader:
    raise SystemExit("GPU material ABI mismatch: stale Vulkan graph route count")
require(vk_header, r"LRT_VK_MATERIAL_PATH_LIGHT_FLOATS\s+16u", "LightRT host light stride")
require(vk_source, r"nlights\s*\*\s*LRT_VK_MATERIAL_PATH_LIGHT_FLOATS\s*\*\s*sizeof\(float\)", "LightRT light upload")

print("GPU material ABI: host, CUDA/HIP, and Vulkan layouts agree")
