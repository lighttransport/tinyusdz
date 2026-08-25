#!/usr/bin/env python3
"""Headless MaterialX/OpenPBR parity for tusdrender GPU RT backends."""

from __future__ import annotations

import math
import os
import pathlib
import re
import subprocess
import sys
import tempfile


SKIP = 77
BACKENDS = {
    "vkr": ("-vkr", r"backend: LightRT VK \(ray_query, descriptor MaterialX, production path"),
    "cuda": ("-cuda", r"backend: shared CUDA RT \(([^)]+)\), path samples="),
    "hip": ("-hip", r"backend: shared HIP RT \(([^)]+)\), path samples="),
}


def fail(message: str, log: str = "") -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    if log:
        print(log, file=sys.stderr)
    raise RuntimeError(message)


def load_image_reader(repo: pathlib.Path):
    helper_dir = repo / "examples" / "tusdview" / "tests"
    sys.path.insert(0, str(helper_dir))
    try:
        from asset_fingerprint import _read_image  # pylint: disable=import-outside-toplevel
    finally:
        sys.path.pop(0)
    return _read_image


def render(binary: pathlib.Path, asset: pathlib.Path, backend: str,
           output: pathlib.Path, extra_args=()) -> tuple[str, bool]:
    flag, success_pattern = BACKENDS[backend]
    command = [
        str(binary), str(asset), str(output), flag, "-stats", "--path-trace",
        "--pt-samples", "4", "--pt-max-depth", "3", "--pt-rr-depth", "2",
        "-w", "192", "-height", "96", "-autoframe", *extra_args,
    ]
    try:
        run = subprocess.run(
            command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, timeout=90, check=False)
    except subprocess.TimeoutExpired as exc:
        fail(f"{backend} timed out after 90 seconds", exc.stdout or "")
    log = run.stdout
    if not re.search(success_pattern, log):
        unavailable = (
            "unavailable", "no CUDA", "no HIP", "no Vulkan", "failed to create",
            "Failed to create", "renderer init failed", "WARN: No renderable",
        )
        if any(token in log for token in unavailable):
            print(f"  {backend}: unavailable (skipped)")
            return log, False
        fail(f"{backend} did not report its production RT path", log)
    if run.returncode != 0:
        fail(f"{backend} exited with {run.returncode}", log)
    if not output.is_file() or output.stat().st_size < 500:
        fail(f"{backend} produced a blank/trivial image", log)
    return log, True


def pixels(image, read_image):
    got = read_image(str(image))
    if got is None:
        fail(f"cannot decode {image}")
    width, height, rgb = got
    return width, height, [tuple(rgb[i:i + 3]) for i in range(0, len(rgb), 3)]


def normalized_rmse(a, b, read_image) -> float:
    wa, ha, pa = pixels(a, read_image)
    wb, hb, pb = pixels(b, read_image)
    if (wa, ha) != (wb, hb):
        fail(f"image dimensions differ: {(wa, ha)} vs {(wb, hb)}")
    mse = sum((ca - cb) ** 2 for xa, xb in zip(pa, pb)
              for ca, cb in zip(xa, xb)) / (len(pa) * 3)
    return math.sqrt(mse) / 255.0


def panel_means(image, read_image):
    width, height, px = pixels(image, read_image)
    foreground = [(x, y) for y in range(height) for x in range(width)
                  if max(px[y * width + x]) >= 4]
    if len(foreground) < width * height // 20:
        fail(f"{image.name} has too little rendered foreground")
    xmin = min(x for x, _ in foreground)
    xmax = max(x for x, _ in foreground) + 1
    ymin = min(y for _, y in foreground)
    ymax = max(y for _, y in foreground) + 1
    means = []
    for panel in range(3):
        x0 = xmin + (xmax - xmin) * panel // 3 + 3
        x1 = xmin + (xmax - xmin) * (panel + 1) // 3 - 3
        y0 = ymin + 3
        y1 = ymax - 3
        sample = [px[y * width + x] for y in range(y0, y1)
                  for x in range(x0, x1) if max(px[y * width + x]) >= 4]
        if not sample:
            fail(f"{image.name} panel {panel} is blank")
        means.append(tuple(sum(p[c] for p in sample) / len(sample)
                           for c in range(3)))
    return means


def check_nonblank(image, read_image, require_variation=True) -> None:
    width, height, px = pixels(image, read_image)
    foreground = [value for value in px if max(value) >= 1]
    if len(foreground) < 32:
        fail(f"{image.name} has too little rendered foreground")
    values = [channel for value in foreground for channel in value]
    if require_variation and max(values) - min(values) < 4:
        fail(f"{image.name} has no meaningful image variation")


def center_mean(image, read_image):
    width, height, px = pixels(image, read_image)
    # The single-card fixture is autoframed in the center. Sampling a bounded
    # central region avoids treating the renderer's non-black environment as
    # foreground (which can otherwise swamp the material's channel ordering).
    x0, x1 = width * 3 // 8, width * 5 // 8
    y0, y1 = height * 3 // 8, height * 5 // 8
    sample = [px[y * width + x] for y in range(y0, y1)
              for x in range(x0, x1)]
    if len(sample) < 32:
        fail(f"{image.name} has too little rendered foreground")
    return tuple(sum(value[channel] for value in sample) / len(sample)
                 for channel in range(3))


def chroma(rgb):
    total = sum(rgb)
    if total < 1.0:
        return (0.0, 0.0, 0.0)
    return tuple(channel / total for channel in rgb)


def check_semantic_grid(image, backend, read_image):
    means = panel_means(image, read_image)
    left, center, right = means
    if not (left[0] > left[1] > left[2]):
        fail(f"{backend} lost PreviewSurface warm-color ordering: {left}")
    if not (center[2] > center[1] > center[0]):
        fail(f"{backend} lost OpenPBR blue-color ordering: {center}")
    if not (right[0] > right[1] > right[2]):
        fail(f"{backend} lost standard_surface warm-color ordering: {right}")
    if center[2] - center[0] < 8.0:
        fail(f"{backend} flattened the OpenPBR base-color response: {center}")
    print(f"  {backend}: panel means " + " ".join(
        "%.1f,%.1f,%.1f" % value for value in means))
    return means


def require_topology(log: str, backend: str) -> None:
    if backend == "vkr":
        pattern = r"graphMaterials=[1-9][0-9]* graphNodes=[1-9][0-9]*"
    else:
        pattern = r"graphs=[1-9][0-9]* graph_nodes=[1-9][0-9]*"
    if not re.search(pattern, log):
        fail(f"{backend} did not retain executable MaterialX graph topology", log)


def require_hardware_identity(log: str, backend: str) -> None:
    if backend == "cuda" and not re.search(
            r"backend: shared CUDA RT \([^)]*NVIDIA[^)]*\)", log, re.I):
        fail("CUDA did not identify an NVIDIA hardware device", log)
    if backend == "hip" and not re.search(
            r"backend: shared HIP RT \([^)]*(AMD|Radeon)[^)]*\)", log, re.I):
        fail("HIP did not identify an AMD hardware device", log)


def parse_vulkan_devices(value: str):
    devices = []
    for spec in filter(None, value.split(",")):
        fields = spec.split(":", 1)
        if len(fields) != 2 or not fields[0].isdigit() or not fields[1]:
            fail("TUSDR_PARITY_VULKAN_DEVICES must use INDEX:VENDOR entries")
        devices.append((int(fields[0]), fields[1]))
    return devices


def generate_texture_fixtures(repo: pathlib.Path, output: pathlib.Path) -> None:
    env = os.environ.copy()
    env.update({
        "TUSDVIEW": str(repo / "build_ninja" / "tusdview"),
        "TUSDVIEW_TEST_OUT": str(output),
        "TUSDVIEW_SEMANTIC_GENERATE_ONLY": "1",
    })
    run = subprocess.run(
        ["bash", str(repo / "examples" / "tusdview" / "tests" /
                     "run-texture-semantic-aov.sh")],
        env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, timeout=30, check=False)
    if run.returncode != 0:
        fail("failed to generate shared semantic texture fixtures", run.stdout)


def write_lobe_grid(path: pathlib.Path) -> None:
    lobes = [
        ("Base", "color3f inputs:base_color = (0.8,0.12,0.04)"),
        ("Coat", "color3f inputs:base_color = (0.08,0.45,0.12)\n"
                 "      float inputs:coat_weight = 1\n"
                 "      color3f inputs:coat_color = (0.2,1,0.3)\n"
                 "      float inputs:coat_roughness = 0.08"),
        ("Transmission", "color3f inputs:base_color = (0.05,0.12,0.8)\n"
                         "      float inputs:transmission_weight = 0.75\n"
                         "      color3f inputs:transmission_color = (0.1,0.3,1)\n"
                         "      float inputs:geometry_thin_walled = 1"),
        ("Subsurface", "color3f inputs:base_color = (0.8,0.35,0.04)\n"
                       "      float inputs:subsurface_weight = 0.8\n"
                       "      color3f inputs:subsurface_color = (1,0.15,0.04)\n"
                       "      float inputs:subsurface_radius = 0.2\n"
                       "      color3f inputs:subsurface_radius_scale = (1,0.2,0.1)"),
        ("Emission", "color3f inputs:base_color = (0.05,0.02,0.05)\n"
                     "      color3f inputs:emission_color = (1,0.05,0.8)\n"
                     "      float inputs:emission_luminance = 3"),
        ("Opacity", "color3f inputs:base_color = (0.05,0.7,0.7)\n"
                    "      float inputs:geometry_opacity = 0.35"),
    ]
    parts = ['#usda 1.0', '(defaultPrim = "World" upAxis = "Y")',
             'def Xform "World" {']
    for index, (name, inputs) in enumerate(lobes):
        x0 = -6 + index * 2
        x1 = x0 + 1.8
        parts.append(f'''  def Mesh "Panel{name}" {{
    uniform bool doubleSided = 1
    point3f[] points = [({x0},-1,0), ({x1},-1,0), ({x1},1,0), ({x0},1,0)]
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0,1,2,3]
    rel material:binding = </World/Mat{name}>
  }}
  def Material "Mat{name}" {{
    token outputs:surface.connect = </World/Mat{name}/P.outputs:surface>
    def Shader "P" {{
      uniform token info:id = "ND_open_pbr_surface_surfaceshader"
      float inputs:base_weight = 1
      float inputs:specular_roughness = 0.3
      {inputs}
      token outputs:surface
    }}
  }}''')
    parts.append('''  def DistantLight "Key" {
    float inputs:intensity = 5
    float3 xformOp:rotateXYZ = (0,25,0)
    uniform token[] xformOpOrder = ["xformOp:rotateXYZ"]
  }
}''')
    path.write_text("\n".join(parts) + "\n", encoding="utf-8")


def write_displacement_fixture(path: pathlib.Path) -> None:
    tex = path.with_name("displacement-checker.ppm")
    pixels = bytearray()
    for y in range(16):
        for x in range(16):
            value = 255 if (x + y) & 1 else 0
            pixels.extend((value, value, value))
    tex.write_bytes(b"P6\n16 16\n255\n" + pixels)
    path.write_text('''#usda 1.0
(defaultPrim = "World" upAxis = "Y")
def Xform "World" {
  def Mesh "BentQuad" {
    uniform bool doubleSided = 1
    point3f[] points = [(-1,-1,0), (0,-1,0), (1,-1,0), (-1,1,0), (0,1,0), (1,1,0)]
    int[] faceVertexCounts = [4,4]
    int[] faceVertexIndices = [0,1,4,3, 1,2,5,4]
    texCoord2f[] primvars:st = [(0,0), (0.5,0), (1,0), (0,1), (0.5,1), (1,1)] (interpolation = "vertex")
    normal3f[] normals = [(-0.7,0,0.7),(0,0,1),(0.7,0,0.7),(-0.7,0,0.7),(0,0,1),(0.7,0,0.7)] (interpolation = "vertex")
    rel material:binding = </World/M>
  }
  def Material "M" {
    token outputs:surface.connect = </World/M/P.outputs:surface>
    def Shader "P" {
      uniform token info:id = "UsdPreviewSurface"
      color3f inputs:diffuseColor = (0.7,0.3,0.1)
      float inputs:displacement.connect = </World/M/D.outputs:r>
      token outputs:surface
    }
    def Shader "ST" {
      uniform token info:id = "UsdPrimvarReader_float2"
      token inputs:varname = "st"
      float2 outputs:result
    }
    def Shader "D" {
      uniform token info:id = "UsdUVTexture"
      asset inputs:file = @./displacement-checker.ppm@
      float2 inputs:st.connect = </World/M/ST.outputs:result>
      token inputs:sourceColorSpace = "raw"
      float inputs:scale = 4.0
      float outputs:r
    }
  }
  def DistantLight "Key" { float inputs:intensity = 5 }
}
''', encoding="utf-8")


def write_swizzle_fixture(path: pathlib.Path) -> None:
    path.write_text('''#usda 1.0
(defaultPrim = "World" upAxis = "Y")
def Xform "World" {
  def Mesh "Card" {
    uniform bool doubleSided = 1
    point3f[] points = [(-1,-1,0), (1,-1,0), (1,1,0), (-1,1,0)]
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0,1,2,3]
    rel material:binding = </World/M>
  }
  def Material "M" (prepend apiSchemas = ["MaterialXConfigAPI"]) {
    token outputs:surface.connect = </World/M/P.outputs:surface>
    token outputs:mtlx:surface.connect = </World/M/P.outputs:surface>
    def Shader "P" {
      uniform token info:id = "ND_open_pbr_surface_surfaceshader"
      float inputs:base_weight = 1
      color3f inputs:base_color.connect = </World/M/NG.outputs:base>
      float inputs:specular_roughness = 0.45
      token outputs:surface
    }
    def NodeGraph "NG" {
      color3f outputs:base.connect = </World/M/NG/S.outputs:out>
      def Shader "C" {
        uniform token info:id = "ND_constant_color4"
        color4f inputs:value = (0.02,0.08,0.35,1)
        color4f outputs:out
      }
      def Shader "S" {
        uniform token info:id = "ND_swizzle_color4_color3"
        color4f inputs:in.connect = </World/M/NG/C.outputs:out>
        string inputs:channels = "bgr"
        color3f outputs:out
      }
    }
  }
  def DistantLight "Key" { float inputs:intensity = 0.5 }
}
''', encoding="utf-8")


def main() -> int:
    if len(sys.argv) not in (2, 3):
        print(f"usage: {sys.argv[0]} TUSDRENDER [REPO_ROOT]", file=sys.stderr)
        return 2
    binary = pathlib.Path(sys.argv[1]).resolve()
    repo = pathlib.Path(sys.argv[2]).resolve() if len(sys.argv) == 3 else pathlib.Path(__file__).resolve().parents[2]
    if not binary.is_file():
        print(f"SKIP: tusdrender not found: {binary}")
        return SKIP
    read_image = load_image_reader(repo)
    grid = repo / "tests" / "usda" / "tusdview-material-semantic-grid.usda"
    graph = repo / "tests" / "feat" / "node-mtlx" / "ChainTest.usda"
    if not grid.is_file() or not graph.is_file():
        fail("MaterialX parity fixtures are missing")

    required = {item for item in os.environ.get(
        "TUSDR_PARITY_REQUIRE_BACKENDS", "").split(",") if item}
    require_hardware = os.environ.get("TUSDR_PARITY_REQUIRE_HARDWARE") == "1"
    cuda_cache_expect = os.environ.get("TUSDR_PARITY_CUDA_CACHE_EXPECT", "")
    vulkan_devices = parse_vulkan_devices(os.environ.get(
        "TUSDR_PARITY_VULKAN_DEVICES", ""))
    unknown = required.difference(BACKENDS)
    if unknown:
        fail(f"unknown required backend(s): {sorted(unknown)}")
    if cuda_cache_expect not in ("", "cold", "warm"):
        fail("TUSDR_PARITY_CUDA_CACHE_EXPECT must be 'cold' or 'warm'")
    if cuda_cache_expect and "cuda" not in required:
        fail("CUDA cache validation requires cuda in "
             "TUSDR_PARITY_REQUIRE_BACKENDS")

    with tempfile.TemporaryDirectory(prefix="tusdrender-mtlx-parity-") as tmp:
        out_dir = pathlib.Path(tmp)
        fixture_dir = out_dir / "semantic-fixtures"
        fixture_dir.mkdir()
        generate_texture_fixtures(repo, fixture_dir)
        lobe_grid = out_dir / "openpbr-lobes.usda"
        displacement = out_dir / "displacement.usda"
        swizzle = out_dir / "swizzle.usda"
        write_lobe_grid(lobe_grid)
        write_displacement_fixture(displacement)
        write_swizzle_fixture(swizzle)
        available = set()
        grid_images = {}
        grid_means = {}
        grid_logs = {}
        print("=== tusdrender MaterialX/OpenPBR semantic grid ===")
        for backend in BACKENDS:
            output = out_dir / f"grid-{backend}.png"
            log, ok = render(binary, grid, backend, output)
            if not ok:
                continue
            available.add(backend)
            grid_logs[backend] = log
            if require_hardware:
                require_hardware_identity(log, backend)
            grid_images[backend] = output
            grid_means[backend] = check_semantic_grid(output, backend, read_image)

        missing = required.difference(available)
        if missing:
            fail(f"required backend(s) unavailable: {sorted(missing)}")
        if not available:
            print("SKIP: no production GPU RT backend is available")
            return SKIP

        if cuda_cache_expect:
            cuda_log = grid_logs["cuda"]
            if cuda_cache_expect == "cold":
                if ("CUDA kernel cache miss" not in cuda_log or
                        "CUDA kernel cached:" not in cuda_log):
                    fail("CUDA cold-cache run did not compile and persist PTX",
                         cuda_log)
            elif "CUDA kernel cache hit:" not in cuda_log:
                fail("CUDA warm-cache run did not reuse PTX", cuda_log)
            print(f"  cuda: {cuda_cache_expect} cache behavior verified")

        if {"cuda", "hip"}.issubset(available):
            error = normalized_rmse(grid_images["cuda"], grid_images["hip"], read_image)
            print(f"  CUDA/HIP semantic-grid normalized RMSE: {error:.6f}")
            if error > 0.015:
                fail(f"CUDA/HIP OpenPBR semantic grid diverged (RMSE={error:.6f})")

        if "vkr" in available:
            reference_name = "cuda" if "cuda" in available else "hip" if "hip" in available else ""
            if reference_name:
                for panel, (vk_rgb, ref_rgb) in enumerate(zip(
                        grid_means["vkr"], grid_means[reference_name])):
                    delta = max(abs(a - b) for a, b in zip(chroma(vk_rgb), chroma(ref_rgb)))
                    if delta > 0.12:
                        fail(f"Vulkan/{reference_name} panel {panel} chromaticity diverged ({delta:.4f})")

        for device_index, vendor in vulkan_devices:
            output = out_dir / f"grid-vkr-device-{device_index}.png"
            log, ok = render(binary, grid, "vkr", output,
                             ("-vkDevice", str(device_index)))
            if not ok:
                fail(f"Vulkan device {device_index} ({vendor}) is unavailable")
            if not re.search(rf"Vulkan device: [^\n]*{re.escape(vendor)}", log, re.I):
                fail(f"Vulkan device {device_index} did not identify vendor {vendor}", log)
            if not re.search(r"Vulkan caps:.*\bray_query\b", log):
                fail(f"Vulkan device {device_index} lacks hardware ray query", log)
            check_semantic_grid(output, f"vkr[{device_index}:{vendor}]", read_image)
            print(f"  Vulkan device {device_index}: {vendor} hardware RT verified")

        print("=== tusdrender executable MaterialX graph ===")
        graph_images = {}
        for backend in sorted(available):
            output = out_dir / f"graph-{backend}.png"
            log, ok = render(binary, graph, backend, output)
            if not ok:
                fail(f"{backend} disappeared between semantic and graph renders")
            require_topology(log, backend)
            check_nonblank(output, read_image)
            graph_images[backend] = output
            print(f"  {backend}: executable graph retained and rendered")

        if {"cuda", "hip"}.issubset(graph_images):
            error = normalized_rmse(graph_images["cuda"], graph_images["hip"], read_image)
            print(f"  CUDA/HIP graph normalized RMSE: {error:.6f}")
            # CUDA and HIP share the same interpreter, but their transcendental
            # implementations and stochastic path decisions are not bitwise
            # identical. Keep the same strict 3% normalized image bound used by
            # the texture cases below.
            if error > 0.03:
                fail(f"CUDA/HIP executable MaterialX graph diverged (RMSE={error:.6f})")

        print("=== tusdrender typed MaterialX swizzle ===")
        for backend in sorted(available):
            output = out_dir / f"swizzle-{backend}.png"
            log, ok = render(binary, swizzle, backend, output)
            if not ok:
                fail(f"{backend} disappeared during swizzle validation")
            require_topology(log, backend)
            mean = center_mean(output, read_image)
            if not (mean[0] > mean[1] > mean[2] and mean[0] - mean[2] > 8.0):
                fail(f"{backend} did not execute bgr swizzle: mean={mean}", log)
            print(f"  {backend}: bgr swizzle mean=" +
                  "%.1f,%.1f,%.1f" % mean)

        print("=== tusdrender extended OpenPBR and texture semantics ===")
        semantic_cases = [
            ("lobes", lobe_grid, False),
            ("core-textures", fixture_dir / "core-openpbr.usda", True),
            ("core-udim", fixture_dir / "core-openpbr-udim.usda", True),
            ("coat", fixture_dir / "coat.usda", True),
            ("coat-udim", fixture_dir / "coat-udim.usda", True),
            ("coat-normal", fixture_dir / "coat-normal.usda", True),
            ("coat-normal-udim", fixture_dir / "coat-normal-udim.usda", True),
            ("opacity", fixture_dir / "opacity-openpbr.usda", True),
            ("opacity-udim", fixture_dir / "opacity-openpbr-udim.usda", True),
            ("normal", fixture_dir / "normal-openpbr.usda", True),
            ("normal-udim", fixture_dir / "normal-openpbr-udim.usda", True),
            ("uv-routing", repo / "models" / "multi-uv-quad.usda", True),
            ("colorspace", repo / "tests" / "usda" /
             "colorspace-materialx-config-texture-render.usda", True),
        ]
        for case_name, case_asset, expects_textures in semantic_cases:
            case_images = {}
            for backend in sorted(available):
                output = out_dir / f"{case_name}-{backend}.png"
                log, ok = render(binary, case_asset, backend, output)
                if not ok:
                    fail(f"{backend} disappeared during {case_name}")
                if expects_textures:
                    if backend == "vkr":
                        texture_pattern = r"Vulkan material ABI:.*"
                    else:
                        texture_pattern = r"textures=[1-9][0-9]*"
                    if not re.search(texture_pattern, log):
                        fail(f"{backend} did not retain {case_name} texture data", log)
                # Some combined texture fixtures intentionally saturate under
                # shaded path tracing (the diagnostic tusdview AOV test checks
                # their individual channels). Here the loader/backend contract
                # is texture retention plus cross-backend parity.
                check_nonblank(output, read_image, require_variation=False)
                case_images[backend] = output
            if {"cuda", "hip"}.issubset(case_images):
                error = normalized_rmse(
                    case_images["cuda"], case_images["hip"], read_image)
                if error > 0.03:
                    fail(f"CUDA/HIP {case_name} diverged (RMSE={error:.6f})")
            print(f"  {case_name}: {len(case_images)} backend(s) passed")

        print("=== tusdrender displacement response ===")
        displacement_images = {}
        for backend in sorted(available):
            enabled = out_dir / f"displacement-on-{backend}.png"
            disabled = out_dir / f"displacement-off-{backend}.png"
            _, ok_on = render(binary, displacement, backend, enabled)
            _, ok_off = render(binary, displacement, backend, disabled,
                               ("-noDisplace",))
            if not ok_on or not ok_off:
                fail(f"{backend} disappeared during displacement validation")
            response = normalized_rmse(enabled, disabled, read_image)
            if response < 0.002:
                fail(f"{backend} displacement had no visible effect (RMSE={response:.6f})")
            displacement_images[backend] = enabled
            print(f"  {backend}: displacement response RMSE={response:.6f}")
        if {"cuda", "hip"}.issubset(displacement_images):
            error = normalized_rmse(displacement_images["cuda"],
                                    displacement_images["hip"], read_image)
            if error > 0.03:
                fail(f"CUDA/HIP displacement diverged (RMSE={error:.6f})")

    print("PASS: headless tusdrender MaterialX/OpenPBR lobe, texture, graph, and displacement parity")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except RuntimeError:
        sys.exit(1)
