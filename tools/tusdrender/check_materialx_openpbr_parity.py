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
           output: pathlib.Path) -> tuple[str, bool]:
    flag, success_pattern = BACKENDS[backend]
    command = [
        str(binary), str(asset), str(output), flag, "-stats", "--path-trace",
        "--pt-samples", "4", "--pt-max-depth", "3", "--pt-rr-depth", "2",
        "-w", "192", "-height", "96", "-autoframe",
    ]
    try:
        run = subprocess.run(
            command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, timeout=60, check=False)
    except subprocess.TimeoutExpired as exc:
        fail(f"{backend} timed out after 60 seconds", exc.stdout or "")
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


def check_nonblank(image, read_image) -> None:
    width, height, px = pixels(image, read_image)
    foreground = [value for value in px if max(value) >= 4]
    if len(foreground) < 64:
        fail(f"{image.name} has too little rendered foreground")
    values = [channel for value in foreground for channel in value]
    if max(values) - min(values) < 4:
        fail(f"{image.name} has no meaningful image variation")


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
    unknown = required.difference(BACKENDS)
    if unknown:
        fail(f"unknown required backend(s): {sorted(unknown)}")

    with tempfile.TemporaryDirectory(prefix="tusdrender-mtlx-parity-") as tmp:
        out_dir = pathlib.Path(tmp)
        available = set()
        grid_images = {}
        grid_means = {}
        print("=== tusdrender MaterialX/OpenPBR semantic grid ===")
        for backend in BACKENDS:
            output = out_dir / f"grid-{backend}.png"
            log, ok = render(binary, grid, backend, output)
            if not ok:
                continue
            available.add(backend)
            grid_images[backend] = output
            grid_means[backend] = check_semantic_grid(output, backend, read_image)

        missing = required.difference(available)
        if missing:
            fail(f"required backend(s) unavailable: {sorted(missing)}")
        if not available:
            print("SKIP: no production GPU RT backend is available")
            return SKIP

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
            if error > 0.02:
                fail(f"CUDA/HIP executable MaterialX graph diverged (RMSE={error:.6f})")

    print("PASS: headless tusdrender MaterialX/OpenPBR semantic and graph parity")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except RuntimeError:
        sys.exit(1)
