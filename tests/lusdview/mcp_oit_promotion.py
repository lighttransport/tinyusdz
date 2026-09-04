#!/usr/bin/env python3
"""Exercise OIT promotion in a persistent viewer, including rendering parity."""

import argparse
import os
import sys
import tempfile
import time
from pathlib import Path

from mcp_render_batch import McpClient, SKIP, rss_kib, wait_for_load


def wait_stats(client, predicate, timeout=180):
    deadline = time.monotonic() + timeout
    last = {}
    calls = 0
    worst = 0.0
    while time.monotonic() < deadline:
        start = time.monotonic()
        last = client.call("get_render_stats", timeout=10)
        worst = max(worst, time.monotonic() - start)
        calls += 1
        if last.get("transparency_error"):
            raise RuntimeError(f"OIT failed: {last}")
        if predicate(last):
            return last, calls, worst
        time.sleep(0.02)
    raise RuntimeError(f"OIT state timed out: {last}")


def capture(client, path, timeout=10):
    for _ in range(30):
        result = client.call("screenshot", {"path": str(path)}, timeout=timeout)
        if result.get("written"):
            return
        time.sleep(0.05)
    raise RuntimeError(f"screenshot not ready: {result}")


def pixels(path):
    # Viewer PPM output has a fixed three-line P6 header.
    header = path.read_bytes().split(b"\n", 3)
    if len(header) != 4 or header[0] != b"P6" or header[2] != b"255":
        raise RuntimeError("unexpected screenshot encoding")
    return header[1], header[3]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("viewer", type=Path)
    parser.add_argument("scene", type=Path)
    parser.add_argument("--threaded", action="store_true")
    parser.add_argument("--scene-changes", action="store_true")
    args = parser.parse_args()
    command = [str(args.viewer.resolve()), "--headless", "--backend", "vk",
               "--frames", "1000000", "--mcp-stdio", "--no-grid",
               "--size", "96x96", "--view-dir", "0,0,-1"]
    if os.environ.get("LUSDVIEW_VK_DEVICE"):
        command += ["--vk-device", os.environ["LUSDVIEW_VK_DEVICE"]]
    if args.threaded:
        command += ["--threaded"]
    command += [str(args.scene.resolve())]
    with tempfile.TemporaryDirectory(prefix="lusdview-oit-") as td:
        root = Path(td)
        os.environ["LUSDVIEW_VK_PIPELINE_CACHE_DIR"] = str(root / "cache")
        client = McpClient(command)
        try:
            baseline, _, _ = wait_stats(client, lambda s: s.get("transparency") == "auto")
            if not baseline["weighted_oit_supported"]:
                print("SKIP: weighted OIT unavailable")
                return SKIP
            assert not baseline["weighted_oit_active"], baseline
            assert baseline["oit_attachment_bytes"] == 0, baseline
            # Software drivers may defer their initial JIT until the first draw.
            # Establish a rendered baseline before measuring promotion latency.
            capture(client, root / "sorted.ppm", timeout=180)
            generation = client.call("get_scene_info")["renderer_generation"]
            start = time.monotonic()
            client.call("render_settings", {"transparency": "weighted"}, timeout=10)
            requested_ms = (time.monotonic() - start) * 1000
            # Coalesce repeated requests and honor a newer sorted request,
            # including when the compilation happens to finish immediately.
            client.call("render_settings", {"transparency": "weighted"}, timeout=10)
            client.call("render_settings", {"transparency": "sorted"}, timeout=10)
            wait_stats(client, lambda s: s["transparency"] == "sorted"
                       and not s["weighted_oit_active"] and s["transparency_phase"] != "warming")
            client.call("render_settings", {"transparency": "weighted"}, timeout=10)
            ready, calls, worst = wait_stats(client, lambda s: s["weighted_oit_active"])
            assert ready["oit_attachment_bytes"] > 0, ready
            capture(client, root / "promoted.ppm")
            assert client.call("get_scene_info")["renderer_generation"] == generation
            compile_ms = ready["transparency_compile_ms"]
            for mode in ("auto", "sorted"):
                client.call("render_settings", {"transparency": mode})
                wait_stats(client, lambda s: s["transparency"] == mode and not s["weighted_oit_active"])
                client.call("render_settings", {"transparency": "weighted"})
                reused, _, _ = wait_stats(client, lambda s: s["weighted_oit_active"])
                assert reused["transparency_compile_ms"] == compile_ms, reused
            print(f"promotion: request={requested_ms:.1f}ms compile={compile_ms:.1f}ms "
                  f"polls={calls} max_poll={worst*1000:.1f}ms rss_kib={rss_kib(client.proc.pid)}")
            # Loading new kinds of geometry while weighted is requested must
            # schedule their missing variants, without replacing the renderer.
            extra_scenes = []
            if args.scene_changes:
                repo = Path(__file__).resolve().parents[2]
                for name in ("lusdview-nonmesh-points-curves.usda",
                             "pointinstancer-expand-001.usda",
                             "lusdview-materialx-swizzle.usda"):
                    text = (repo / "tests/usda" / name).read_text()
                    if name.startswith("pointinstancer"):
                        text = text.replace('color3f[] primvars:displayColor',
                                            'float[] primvars:displayOpacity = [0.5]\n        color3f[] primvars:displayColor')
                    if name.startswith("lusdview-materialx"):
                        text = text.replace('float inputs:base_weight = 1',
                                            'float inputs:base_weight = 1\n            float inputs:geometry_opacity = 0.5')
                    path = root / name
                    path.write_text(text)
                    extra_scenes.append(path)
                    old = client.call("get_scene_info")["scene_generation"]
                    client.call("load_usd", {"path": str(path)})
                    wait_for_load(client, path, old, 180)
                    wait_stats(client, lambda s: s["weighted_oit_active"])
                    capture(client, path.with_suffix(".promoted.ppm"))
                    assert client.call("get_scene_info")["renderer_generation"] == generation
        except Exception:
            print(client.stderr_text(), file=sys.stderr)
            raise
        finally:
            client.close()
        # Explicit weighted startup shares the pipeline builder but uses the
        # full shader; comparing pixels checks graph-free specialization too.
        reference = McpClient(command[:-1] + ["--transparency", "weighted"] + command[-1:])
        try:
            # Explicit startup may compile before starting the MCP transport.
            reference.call("get_scene_info", timeout=180)
            capture(reference, root / "reference.ppm", timeout=180)
            wait_stats(reference, lambda s: s["weighted_oit_active"])
            capture(reference, root / "reference.ppm")
            for path in extra_scenes:
                old = reference.call("get_scene_info")["scene_generation"]
                reference.call("load_usd", {"path": str(path)})
                wait_for_load(reference, path, old, 180)
                wait_stats(reference, lambda s: s["weighted_oit_active"])
                capture(reference, path.with_suffix(".reference.ppm"))
                dims, actual = pixels(path.with_suffix(".promoted.ppm"))
                ref_dims, expected = pixels(path.with_suffix(".reference.ppm"))
                assert dims == ref_dims and len(actual) == len(expected)
                delta = max(abs(a - b) for a, b in zip(actual, expected))
                assert delta <= 2, f"{path.name}: image delta {delta}"
                print(f"PASS: scene change {path.name} (delta={delta})")
        except Exception:
            print(reference.stderr_text(), file=sys.stderr)
            raise
        finally:
            reference.close()
        dims, actual = pixels(root / "promoted.ppm")
        ref_dims, expected = pixels(root / "reference.ppm")
        assert dims == ref_dims and len(actual) == len(expected)
        delta = max(abs(a - b) for a, b in zip(actual, expected))
        assert delta <= 2, f"promotion differs from weighted startup: max delta {delta}"
        assert pixels(root / "sorted.ppm")[1] != actual, "promotion did not change compositing"
        print(f"PASS: OIT promotion, cancellation, reuse, and image parity (delta={delta})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
