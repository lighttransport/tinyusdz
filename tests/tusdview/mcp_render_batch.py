#!/usr/bin/env python3
"""Render a JSON manifest through one persistent tusdview MCP process.

Cases may provide either ``path`` or inline ``usda``. Backend/process-wide
options belong in ``viewer_args``; tests that need different backends, loaders,
environment variables, or startup-only options intentionally use another batch.
"""

import json
from pathlib import Path
import subprocess
import sys
import threading
import time


SKIP = 77


class McpClient:
    def __init__(self, command):
        self.proc = subprocess.Popen(
            command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, bufsize=1)
        self.next_id = 1
        self.responses = {}
        self.condition = threading.Condition()
        self.stderr = []
        threading.Thread(target=self._read_stdout, daemon=True).start()
        threading.Thread(target=self._read_stderr, daemon=True).start()

    def _read_stdout(self):
        for line in self.proc.stdout:
            try:
                response = json.loads(line)
            except json.JSONDecodeError:
                continue
            with self.condition:
                self.responses[response.get("id")] = response
                self.condition.notify_all()

    def _read_stderr(self):
        for line in self.proc.stderr:
            self.stderr.append(line)
            if len(self.stderr) > 2000:
                del self.stderr[:1000]

    def call(self, name, arguments=None, timeout=30.0):
        request_id = self.next_id
        self.next_id += 1
        request = {"jsonrpc": "2.0", "id": request_id,
                   "method": "tools/call",
                   "params": {"name": name,
                              "arguments": arguments or {}}}
        try:
            self.proc.stdin.write(json.dumps(request) + "\n")
            self.proc.stdin.flush()
        except (BrokenPipeError, OSError) as exc:
            raise RuntimeError(f"tusdview exited before {name}: {exc}") from exc
        deadline = time.monotonic() + timeout
        with self.condition:
            while request_id not in self.responses:
                if self.proc.poll() is not None:
                    raise RuntimeError(
                        f"tusdview exited with {self.proc.returncode} during {name}")
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise RuntimeError(f"MCP {name} timed out")
                self.condition.wait(min(remaining, 0.1))
            response = self.responses.pop(request_id)
        if "error" in response:
            raise RuntimeError(f"MCP {name}: {response['error']}")
        return response["result"]["structuredContent"]

    def close(self):
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()

    def stderr_text(self):
        return "".join(self.stderr)


def wait_for_load(client, path, old_generation, timeout):
    deadline = time.monotonic() + timeout
    last = {}
    while time.monotonic() < deadline:
        last = client.call("get_scene_info")
        if (not last.get("loading") and last.get("filepath") == str(path) and
                last.get("scene_generation", 0) > old_generation):
            if not last.get("loaded"):
                raise RuntimeError(f"load failed: {last}")
            return last
        time.sleep(0.02)
    raise RuntimeError(f"load timed out: {last}")


def check_ppm(path):
    data = path.read_bytes()
    if not data.startswith(b"P6") or len(data) < 32:
        raise RuntimeError(f"invalid or empty screenshot: {path}")
    # A flat clear image is never a useful render regression result.
    payload = data.split(b"\n", 3)[-1]
    if len(set(payload[: min(len(payload), 65536)])) < 2:
        raise RuntimeError(f"flat screenshot: {path}")


def main():
    if len(sys.argv) < 4:
        print(f"usage: {sys.argv[0]} tusdview manifest.json output-dir "
              "[--windowed] [viewer args...]",
              file=sys.stderr)
        return 2
    viewer = Path(sys.argv[1]).resolve()
    manifest = json.loads(Path(sys.argv[2]).read_text())
    output = Path(sys.argv[3]).resolve()
    output.mkdir(parents=True, exist_ok=True)
    extra_args = sys.argv[4:]
    windowed = "--windowed" in extra_args
    extra_args = [arg for arg in extra_args if arg != "--windowed"]
    command = [str(viewer)]
    if not windowed:
        command.append("--headless")
    command += ["--frames", "1000000", "--mcp-stdio", "--no-grid"]
    command += manifest.get("viewer_args", []) + extra_args
    client = McpClient(command)
    try:
        # A first request distinguishes transport/startup failure from a case.
        initial = client.call("get_scene_info", timeout=15)
        pid = client.proc.pid
        lifecycle = (initial.get("window_generation"),
                     initial.get("renderer_generation"))
        for index, case in enumerate(manifest["cases"]):
            name = case.get("name", f"case-{index}")
            if "usda" in case:
                load_args = {"usda": case["usda"]}
            else:
                path = Path(case["path"]).resolve()
                load_args = {"path": str(path)}

            load_settings = case.get("load_settings", {})
            if load_settings:
                client.call("render_settings", load_settings)
            before = client.call("get_scene_info").get("scene_generation", 0)
            started = client.call("load_usd", load_args)
            path = Path(started["path"])
            info = wait_for_load(client, path, before,
                                 float(case.get("timeout", 30)))
            for key, expected in case.get("expect", {}).items():
                if info.get(key) != expected:
                    raise RuntimeError(
                        f"{name}: expected {key}={expected!r}, "
                        f"got {info.get(key)!r}")

            settings = case.get("settings", {})
            if settings:
                client.call("render_settings", settings)
            if case.get("viewport"):
                client.call("viewport", case["viewport"])
            elif "viewport" not in case:
                client.call("viewport", {"op": "fit"})

            shot = output / f"{index:03d}-{name}.ppm"
            result = client.call("screenshot", {"path": str(shot)})
            if not result.get("written"):
                raise RuntimeError(f"{name}: screenshot was not written")
            check_ppm(shot)
            if client.proc.pid != pid:
                raise RuntimeError("viewer PID changed inside a batch")
            current_lifecycle = (info.get("window_generation"),
                                 info.get("renderer_generation"))
            if current_lifecycle != lifecycle:
                raise RuntimeError(
                    f"{name}: window/renderer recreated: "
                    f"{lifecycle} -> {current_lifecycle}")
            print(f"PASS: {name}: generation={info['scene_generation']} "
                  f"triangles={info['triangle_count']} pid={pid} "
                  f"lifecycle={lifecycle}")
        print(f"PASS: {len(manifest['cases'])} cases rendered in one tusdview process")
        return 0
    except Exception as exc:
        log = client.stderr_text()
        unavailable = ("failed to initialize" in log.lower() or
                       "no vulkan" in log.lower() or
                       "vulkan initialization failed" in log.lower())
        print(("SKIP" if unavailable else "FAIL") + f": {exc}", file=sys.stderr)
        if log:
            print(log[-12000:], file=sys.stderr)
        return SKIP if unavailable else 1
    finally:
        client.close()


if __name__ == "__main__":
    sys.exit(main())
