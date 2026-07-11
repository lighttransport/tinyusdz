#!/usr/bin/env python3
#
# Compare tusdview CPU and GPU skinning screenshots.
#
# The harness intentionally runs the normal windowed path through xvfb-run on
# Linux. Xvfb supplies the X server; GL/Vulkan still use the host renderer.

import argparse
import json
import platform
import shutil
import subprocess
import sys
from pathlib import Path


def parse_args():
    parser = argparse.ArgumentParser(
        description="Render CPU/GPU skinning screenshots and compare their PPM pixels."
    )
    parser.add_argument("--app", required=True, help="Path to the tusdview executable.")
    parser.add_argument("--model", required=True, help="USD model with skeletal animation.")
    parser.add_argument("--out-dir", required=True, help="Directory for screenshots/report.")
    parser.add_argument("--backend", default="gl", choices=("gl", "vk", "vulkan"))
    parser.add_argument("--frames", type=int, default=4)
    parser.add_argument("--time", type=float, default=12.0)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=800)
    parser.add_argument("--max-mean", type=float, default=1.0)
    parser.add_argument("--max-pixel", type=int, default=128)
    parser.add_argument("--max-changed-ratio", type=float, default=0.15)
    parser.add_argument("--xvfb-run", default=None, help="Path to xvfb-run.")
    parser.add_argument("--no-xvfb", action="store_true", help="Run without xvfb-run.")
    return parser.parse_args()


def fail(message):
    print(f"ERROR: {message}", file=sys.stderr)
    return 1


def command_prefix(args):
    if args.no_xvfb or platform.system() != "Linux":
        return []
    xvfb_run = args.xvfb_run or shutil.which("xvfb-run")
    if not xvfb_run:
        raise RuntimeError("xvfb-run was not found; install it or pass --no-xvfb")
    screen = f"-screen 0 {args.width}x{args.height}x24"
    return [xvfb_run, "-a", "-s", screen]


def run_viewer(args, mode, output_path):
    cmd = command_prefix(args) + [
        args.app,
        "--backend",
        args.backend,
        "--frames",
        str(args.frames),
        "--time",
        format(args.time, "g"),
        "--skinning",
        mode,
        "--screenshot",
        str(output_path),
        args.model,
    ]
    proc = subprocess.run(cmd, text=True, capture_output=True, check=False)
    log = (proc.stdout or "") + (proc.stderr or "")
    if proc.returncode != 0:
        raise RuntimeError(
            f"{mode} render failed with exit code {proc.returncode}\n"
            f"command: {' '.join(cmd)}\n"
            f"stdout:\n{proc.stdout}\n"
            f"stderr:\n{proc.stderr}"
        )
    expected = f"skinning: {mode.upper()}"
    if expected not in log:
        raise RuntimeError(
            f"{mode} render did not report '{expected}'\n"
            f"command: {' '.join(cmd)}\n"
            f"stdout:\n{proc.stdout}\n"
            f"stderr:\n{proc.stderr}"
        )
    if not output_path.is_file():
        raise RuntimeError(f"{mode} render did not create {output_path}")
    return {"command": cmd, "stdout": proc.stdout, "stderr": proc.stderr}


def read_token(data, offset):
    n = len(data)
    while offset < n and data[offset] in b" \t\r\n":
        offset += 1
    while offset < n and data[offset] == ord("#"):
        while offset < n and data[offset] not in b"\r\n":
            offset += 1
        while offset < n and data[offset] in b" \t\r\n":
            offset += 1
    start = offset
    while offset < n and data[offset] not in b" \t\r\n":
        offset += 1
    if start == offset:
        raise ValueError("unexpected end of PPM header")
    return data[start:offset].decode("ascii"), offset


def read_ppm(path):
    data = path.read_bytes()
    token, offset = read_token(data, 0)
    if token != "P6":
        raise ValueError(f"{path}: expected P6 PPM, got {token!r}")
    width_token, offset = read_token(data, offset)
    height_token, offset = read_token(data, offset)
    maxval_token, offset = read_token(data, offset)
    width = int(width_token)
    height = int(height_token)
    maxval = int(maxval_token)
    if maxval != 255:
        raise ValueError(f"{path}: expected maxval 255, got {maxval}")
    if offset >= len(data) or data[offset] not in b" \t\r\n":
        raise ValueError(f"{path}: missing whitespace after PPM header")
    offset += 1
    expected_size = width * height * 3
    pixels = data[offset:]
    if len(pixels) != expected_size:
        raise ValueError(
            f"{path}: expected {expected_size} pixel bytes, got {len(pixels)}"
        )
    return width, height, pixels


def percentile(sorted_values, percentile_value):
    if not sorted_values:
        return 0
    index = int(round((len(sorted_values) - 1) * percentile_value))
    return sorted_values[index]


def compare_ppm(cpu_path, gpu_path):
    cpu_width, cpu_height, cpu_pixels = read_ppm(cpu_path)
    gpu_width, gpu_height, gpu_pixels = read_ppm(gpu_path)
    if (cpu_width, cpu_height) != (gpu_width, gpu_height):
        raise ValueError(
            "screenshot dimensions differ: "
            f"CPU {cpu_width}x{cpu_height}, GPU {gpu_width}x{gpu_height}"
        )

    max_diff = 0
    total_diff = 0
    channel_count = len(cpu_pixels)
    changed_pixels = 0
    channel_diffs = []

    for pixel_index in range(0, channel_count, 3):
        pixel_changed = False
        for channel in range(3):
            diff = abs(cpu_pixels[pixel_index + channel] - gpu_pixels[pixel_index + channel])
            total_diff += diff
            if diff > max_diff:
                max_diff = diff
            if diff:
                pixel_changed = True
                channel_diffs.append(diff)
        if pixel_changed:
            changed_pixels += 1

    channel_diffs.sort()
    pixel_count = cpu_width * cpu_height
    return {
        "width": cpu_width,
        "height": cpu_height,
        "channels": channel_count,
        "max_channel_diff": max_diff,
        "mean_channel_diff": total_diff / float(channel_count),
        "changed_pixels": changed_pixels,
        "changed_pixel_ratio": changed_pixels / float(pixel_count),
        "p95_changed_channel_diff": percentile(channel_diffs, 0.95),
        "p99_changed_channel_diff": percentile(channel_diffs, 0.99),
    }


def main():
    args = parse_args()
    app = Path(args.app)
    model = Path(args.model)
    if not app.is_file():
        return fail(f"tusdview executable not found: {app}")
    if not model.is_file():
        return fail(f"model not found: {model}")
    if args.frames <= 0:
        return fail("--frames must be positive")

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    cpu_path = out_dir / "skinning-cpu.ppm"
    gpu_path = out_dir / "skinning-gpu.ppm"
    report_path = out_dir / "skinning-screenshot-diff.json"

    try:
        cpu_run = run_viewer(args, "cpu", cpu_path)
        gpu_run = run_viewer(args, "gpu", gpu_path)
        metrics = compare_ppm(cpu_path, gpu_path)
    except Exception as exc:
        return fail(str(exc))

    report = {
        "app": str(app),
        "model": str(model),
        "backend": args.backend,
        "frames": args.frames,
        "time": args.time,
        "thresholds": {
            "max_mean": args.max_mean,
            "max_pixel": args.max_pixel,
            "max_changed_ratio": args.max_changed_ratio,
        },
        "metrics": metrics,
        "cpu": cpu_run,
        "gpu": gpu_run,
    }
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    print(
        "tusdview CPU/GPU skinning screenshot diff: "
        f"{metrics['width']}x{metrics['height']}, "
        f"max={metrics['max_channel_diff']}, "
        f"mean={metrics['mean_channel_diff']:.6f}, "
        f"changed={metrics['changed_pixels']} "
        f"({metrics['changed_pixel_ratio']:.6%}), "
        f"p95={metrics['p95_changed_channel_diff']}, "
        f"p99={metrics['p99_changed_channel_diff']}"
    )
    print(f"report: {report_path}")

    failures = []
    if metrics["max_channel_diff"] > args.max_pixel:
        failures.append(
            f"max channel diff {metrics['max_channel_diff']} > {args.max_pixel}"
        )
    if metrics["mean_channel_diff"] > args.max_mean:
        failures.append(
            f"mean channel diff {metrics['mean_channel_diff']:.6f} > {args.max_mean}"
        )
    if metrics["changed_pixel_ratio"] > args.max_changed_ratio:
        failures.append(
            "changed pixel ratio "
            f"{metrics['changed_pixel_ratio']:.6%} > {args.max_changed_ratio:.6%}"
        )
    if failures:
        return fail("; ".join(failures))
    return 0


if __name__ == "__main__":
    sys.exit(main())
