#!/usr/bin/env python3
#
# Compare tusdview CPU and GPU skinning screenshots.
#
# The harness intentionally runs the normal windowed path through xvfb-run on
# Linux. Xvfb supplies the X server; GL/Vulkan still use the host renderer.

import argparse
import json
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gpu_backend import (  # noqa: E402
    detect_gpu,
    device_name,
    gpu_offload_env,
    is_software_renderer,
)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Render CPU/GPU skinning screenshots and compare their PPM pixels."
    )
    parser.add_argument("--app", required=True, help="Path to the tusdview executable.")
    parser.add_argument("--model", required=True, help="USD model with skeletal animation.")
    parser.add_argument("--out-dir", required=True, help="Directory for screenshots/report.")
    parser.add_argument("--backend", default="gl", choices=("gl", "vk", "vulkan"))
    parser.add_argument("--rt", action="store_true", help="Enable Vulkan ray-query RT.")
    parser.add_argument("--mode", default=None, help="Forward --mode NAME to tusdview.")
    parser.add_argument("--vk-device", default=None, help="Forward --vk-device INDEX|NAME to tusdview.")
    parser.add_argument("--headless", action="store_true", help="Use tusdview's Vulkan headless path.")
    parser.add_argument(
        "--skip-unavailable",
        action="store_true",
        help="Return 77 when the requested backend/RT/GPU skinning path is unavailable.",
    )
    parser.add_argument(
        "--env",
        action="append",
        default=[],
        metavar="NAME=VALUE",
        help="Environment variable to set for tusdview; may be repeated.",
    )
    parser.add_argument(
        "--nvidia-offload",
        action="store_true",
        help="Set common PRIME render-offload environment variables for NVIDIA.",
    )
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


class SkipTest(RuntimeError):
    pass


def fail(message):
    print(f"ERROR: {message}", file=sys.stderr)
    return 1


def skip(message):
    print(f"SKIP: {message}")
    return 77


def child_env(args):
    env = os.environ.copy()
    if args.nvidia_offload:
        env.setdefault("__NV_PRIME_RENDER_OFFLOAD", "1")
        env.setdefault("__GLX_VENDOR_LIBRARY_NAME", "nvidia")
        env.setdefault(
            "__EGL_VENDOR_LIBRARY_FILENAMES",
            "/usr/share/glvnd/egl_vendor.d/10_nvidia.json",
        )
    for item in args.env:
        if "=" not in item:
            raise ValueError(f"--env expects NAME=VALUE, got {item!r}")
        name, value = item.split("=", 1)
        if not name:
            raise ValueError("--env variable name must not be empty")
        env[name] = value
    return env


def command_prefixes(args):
    """Launch prefixes to try, in order.

    An inherited DISPLAY comes first: that is where a HARDWARE GL device lives,
    and GPU skinning only works on one (Xvfb has no DRI, so Mesa falls back to
    llvmpipe, which fetches no skin attributes -- see gpu_backend.py). Xvfb is
    the fallback, both when there is no DISPLAY and when the inherited one turns
    out to be unusable (a stale forwarded X11 socket).
    """
    if args.headless:
        return [[]]
    if args.no_xvfb or platform.system() != "Linux":
        return [[]]
    prefixes = []
    if os.environ.get("DISPLAY"):
        prefixes.append([])
    xvfb_run = args.xvfb_run or shutil.which("xvfb-run")
    if xvfb_run:
        screen = f"-screen 0 {args.width}x{args.height}x24"
        prefixes.append([xvfb_run, "-a", "-s", screen])
    if not prefixes:
        raise RuntimeError("xvfb-run was not found; install it or pass --no-xvfb")
    return prefixes


def run_viewer(args, mode, output_path):
    viewer = [args.app]
    if args.headless:
        viewer.append("--headless")
    if args.rt:
        viewer.append("--rt")
    if args.vk_device:
        viewer += ["--vk-device", args.vk_device]
    if args.mode:
        viewer += ["--mode", args.mode]
    viewer += [
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
    prefixes = command_prefixes(args)
    for i, prefix in enumerate(prefixes):
        cmd = prefix + viewer
        env = child_env(args)
        if prefix:
            # Xvfb prefix: without DRI Mesa gives llvmpipe, so route GL to the
            # hardware GPU when one is present -- see gpu_backend.py.
            env = gpu_offload_env(env)
        proc = subprocess.run(
            cmd, text=True, capture_output=True, check=False, env=env
        )
        log = (proc.stdout or "") + (proc.stderr or "")
        # An inherited DISPLAY that is unusable is not a failure -- fall through
        # to the Xvfb prefix. A stale forwarded X11 socket fails glfwInit; a
        # live forwarded display can open but still refuse a GL context
        # (GLX BadValue), which surfaces as glfwCreateWindow failing.
        if (
            proc.returncode != 0
            and i + 1 < len(prefixes)
            and ("glfwInit failed" in log or "glfwCreateWindow failed" in log)
        ):
            continue
        break
    # A software rasterizer (Xvfb / forwarded X11 give Mesa llvmpipe, which has
    # no DRI) fetches only aPosition: the skin joint/weight attributes read back
    # as zero, so the GPU-skinned render is the rest pose no matter what the
    # skinning code does, and CPU-vs-GPU always differs -- see gpu_backend.py.
    # The comparison is meaningless there, so skip rather than fail.
    if is_software_renderer(log):
        raise SkipTest(
            f"{args.backend} is a software renderer ({device_name(log)}); it "
            f"does not fetch the skin vertex attributes, so CPU-vs-GPU skinning "
            f"cannot be compared on it"
        )
    if proc.returncode != 0:
        if args.skip_unavailable and (
            "Vulkan" in log
            or "GLFW" in log
            or "ray tracing is unavailable" in log
            or "GPU skinning unsupported" in log
        ):
            raise SkipTest(
                f"{mode} render unavailable with exit code {proc.returncode}"
            )
        raise RuntimeError(
            f"{mode} render failed with exit code {proc.returncode}\n"
            f"command: {' '.join(cmd)}\n"
            f"stdout:\n{proc.stdout}\n"
            f"stderr:\n{proc.stderr}"
        )
    if args.rt and "ray tracing is unavailable" in log:
        if args.skip_unavailable:
            raise SkipTest("Vulkan ray tracing unavailable")
        raise RuntimeError(
            f"{mode} render requested --rt, but ray tracing was unavailable\n"
            f"command: {' '.join(cmd)}\n"
            f"stdout:\n{proc.stdout}\n"
            f"stderr:\n{proc.stderr}"
        )
    expected = f"skinning: {mode.upper()}"
    if expected not in log:
        if args.skip_unavailable and (
            "GPU skinning unsupported" in log
            or "requested GPU, using CPU" in log
            or "ray tracing is unavailable" in log
        ):
            raise SkipTest(f"{mode} render did not use requested GPU skinning path")
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
    if args.backend in ("vk", "vulkan") and not args.vk_device:
        # Inside Xvfb/sandboxed sessions the default Vulkan device can be
        # lavapipe even though the hardware ICD enumerates -- see gpu_backend.py.
        args.vk_device = detect_gpu()
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
    except SkipTest as exc:
        return skip(str(exc))
    except Exception as exc:
        return fail(str(exc))

    report = {
        "app": str(app),
        "model": str(model),
        "backend": args.backend,
        "rt": args.rt,
        "mode": args.mode,
        "vk_device": args.vk_device,
        "headless": args.headless,
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
