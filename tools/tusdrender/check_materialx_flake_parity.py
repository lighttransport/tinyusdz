#!/usr/bin/env python3
"""Focused headless GPU regression for MaterialX flake multi-outputs."""

from __future__ import annotations

import pathlib
import os
import re
import subprocess
import sys
import tempfile

BACKENDS = {
    "vkr": ("-vkr", r"backend: LightRT VK \(ray_query"),
    "cuda": ("-cuda", r"backend: shared CUDA RT"),
    "hip": ("-hip", r"backend: shared HIP RT"),
}

SCENE = '''#usda 1.0
(defaultPrim = "World" upAxis = "Y")
def Xform "World" {
  def Mesh "Card" {
    point3f[] points = [(-1,-1,0),(1,-1,0),(1,1,0),(-1,1,0)]
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0,1,2,3]
    texCoord2f[] primvars:st = [(0,0),(1,0),(1,1),(0,1)] (interpolation = "vertex")
    rel material:binding = </World/M>
  }
  def Material "M" (prepend apiSchemas = ["MaterialXConfigAPI"]) {
    token outputs:surface.connect = </World/M/P.outputs:surface>
    token outputs:mtlx:surface.connect = </World/M/P.outputs:surface>
    def Shader "P" {
      uniform token info:id = "ND_open_pbr_surface_surfaceshader"
      color3f inputs:base_color.connect = </World/M/NG.outputs:base>
      token outputs:surface
    }
    def NodeGraph "NG" {
      color3f outputs:base.connect = </World/M/NG/Tint.outputs:out>
      def Shader "Flakes" {
        uniform token info:id = "ND_flake2d"
        float inputs:size = 0.08
        float inputs:roughness = 0.25
        float inputs:coverage = 0.8
        int outputs:id
        float outputs:rand
        float outputs:presence
        float3 outputs:flakenormal
      }
      def Shader "Matrix" {
        uniform token info:id = "ND_creatematrix_vector3_matrix33"
        float3 inputs:in1 = (2,0,0)
        float3 inputs:in2 = (0,3,0)
        float3 inputs:in3 = (0,0,4)
        matrix3d outputs:out
      }
      def Shader "Transform" {
        uniform token info:id = "ND_transformmatrix_vector3"
        float3 inputs:in = (0.35,0.2,0.1)
        matrix3d inputs:mat.connect = </World/M/NG/Matrix.outputs:out>
        float3 outputs:out
      }
      def Shader "Tint" {
        uniform token info:id = "ND_multiply_vector3FA"
        float3 inputs:in1.connect = </World/M/NG/Transform.outputs:out>
        float inputs:in2.connect = </World/M/NG/Flakes.outputs:presence>
        float3 outputs:out
      }
    }
  }
  def DistantLight "Key" { float inputs:intensity = 1 }
}
'''


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} TUSDRENDER", file=sys.stderr)
        return 2
    binary = pathlib.Path(sys.argv[1]).resolve()
    ran = 0
    # Snapshot compute-device exposure before Vulkan initialization. Some
    # PRIME loaders transiently create NVIDIA control nodes even when no CUDA
    # compute device is usable in this container.
    cuda_available = (os.environ.get("TINYUSDZ_MTLX_FLAKE_CUDA") == "1" and
                      pathlib.Path("/dev/nvidia0").exists() and
                      pathlib.Path("/dev/nvidia-uvm").exists())
    hip_available = (os.environ.get("TINYUSDZ_MTLX_FLAKE_HIP") == "1" and
                     pathlib.Path("/dev/kfd").exists())
    with tempfile.TemporaryDirectory(prefix="tusdrender-mtlx-flake-") as tmp:
        asset = pathlib.Path(tmp) / "flake.usda"
        asset.write_text(SCENE, encoding="utf-8")
        for backend, (flag, success) in BACKENDS.items():
            if backend == "cuda" and not cuda_available:
                continue
            if backend == "hip" and not hip_available:
                continue
            output = pathlib.Path(tmp) / f"flake-{backend}.png"
            try:
                result = subprocess.run(
                    [str(binary), str(asset), str(output), flag, "-stats",
                     "--path-trace", "--pt-samples", "1", "-w", "64",
                     "-height", "64", "-autoframe"], stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT, text=True, timeout=30, check=False)
            except subprocess.TimeoutExpired:
                print(f"{backend}: timed out", file=sys.stderr)
                return 1
            log = result.stdout
            if not re.search(success, log):
                if any(word in log for word in
                       ("unavailable", "no CUDA", "no HIP", "no Vulkan",
                        "renderer init failed", "failed to create")):
                    continue
                print(log, file=sys.stderr)
                return 1
            ran += 1
            if result.returncode or not re.search(
                    r"graphMaterials=[1-9][0-9]* graphNodes=[1-9][0-9]*", log):
                print(log, file=sys.stderr)
                return 1
            if not output.is_file() or output.stat().st_size < 500:
                print(f"{backend}: missing/trivial flake render", file=sys.stderr)
                return 1
    if not ran:
        print("MaterialX flake GPU backends unavailable")
        return 77
    print(f"MaterialX flake GPU parity: {ran} backend(s) passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
