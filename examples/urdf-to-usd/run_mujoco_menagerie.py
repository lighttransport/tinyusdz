#!/usr/bin/env python3
"""Run URDF/MJCF -> USD export tests over MuJoCo Menagerie scenes."""

from __future__ import annotations

import argparse
import concurrent.futures
import glob
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
from typing import Any


# Previously-failing cases are now handled and pass on both runners:
#   - google_robot / hello_robot_stretch[_3] / skydio_x2: <compiler assetdir>
#     plus an assets/ + recursive-basename fallback now resolve the meshes.
#   - ms_human_700: same basename fallback finds assets referenced relative to
#     an included file's dir (../geometry/*); the JS OBJ loader now merges
#     multi-object files into one mesh (matching native + MuJoCo semantics).
#   - apptronik_apollo: the binary meshRef payload + the JS CLI's raised USDC
#     cap (--max-usdc-mb) export its ~110MB output.
#   - robot_soccer_kit: exports within the cap via the binary meshRef path.
# Add entries back here as (runner, scene_id) -> reason if a scene regresses.
KNOWN_XFAIL: dict[tuple[str, str], str] = {}


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def default_menagerie_root() -> Path:
    for key in ("MUJOCO_MENAGERIE", "MENAGERIE_DIR"):
        env = os.environ.get(key)
        if env:
            return Path(env)
    return repo_root() / "mujoco_menagerie"


def parse_args() -> argparse.Namespace:
    root = repo_root()
    parser = argparse.ArgumentParser(
        description="Run native and JS/WASM URDF-to-USD CLIs for MuJoCo Menagerie scene.xml files."
    )
    parser.add_argument(
        "--menagerie-root",
        "--test-file-dir",
        dest="menagerie_root",
        type=Path,
        default=default_menagerie_root(),
        help="MuJoCo Menagerie checkout root / test file directory. "
             "Default: $MUJOCO_MENAGERIE, $MENAGERIE_DIR, or <repo>/mujoco_menagerie",
    )
    parser.add_argument(
        "--glob-pattern",
        default="**/scene.xml",
        help="Glob pattern relative to --menagerie-root. Default: **/scene.xml",
    )
    parser.add_argument(
        "--native-cli",
        type=Path,
        default=root / "build/examples/urdf-to-usd/urdf-to-usd",
        help="Native urdf-to-usd executable path.",
    )
    parser.add_argument(
        "--js-cli",
        type=Path,
        default=root / "web/js/cli/urdf-to-usd.js",
        help="JS/WASM urdf-to-usd.js path.",
    )
    parser.add_argument(
        "--usd-urdf-roundtrip-cli",
        type=Path,
        default=root / "web/js/cli/usd-urdf-roundtrip.js",
        help="JS/WASM USD Physics -> URDF roundtrip tester path.",
    )
    parser.add_argument("--node", default=shutil.which("node") or "node")
    parser.add_argument(
        "--runner",
        choices=["both", "native", "js"],
        default="both",
        help="Which CLI path to test. Default: both",
    )
    parser.add_argument(
        "--format",
        choices=["usda", "usdc", "usdz"],
        default="usdc",
        help="Output format to test. Default: usdc",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("/tmp/tinyusdz-mujoco-menagerie"),
        help="Directory for logs, summary JSON, and temporary outputs.",
    )
    parser.add_argument(
        "--jobs",
        type=int,
        default=1,
        help="Number of scene/runner jobs to execute concurrently. Default: 1",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=300.0,
        help="Per-command timeout in seconds. Default: 300",
    )
    parser.add_argument("--allow-missing", action="store_true")
    parser.add_argument("--tessellate-collision-shapes", action="store_true")
    parser.add_argument(
        "--max-usdc-mb",
        type=int,
        default=2048,
        help="USDC max output size (MB) passed to the JS CLI. Default: 2048",
    )
    parser.add_argument(
        "--max-mem-mb",
        type=int,
        default=4096,
        help="USDC max memory estimate (MB) passed to the JS CLI. Default: 4096",
    )
    parser.add_argument(
        "--js-verify",
        action="store_true",
        help="Let the JS CLI run its USDA schema verification. Default: pass --no-verify for speed.",
    )
    parser.add_argument(
        "--roundtrip",
        action="store_true",
        help="After each successful USD export, run the JS USD Physics -> URDF roundtrip tester.",
    )
    parser.add_argument(
        "--roundtrip-no-assert",
        action="store_true",
        help="Run USD -> URDF conversion but pass --no-assert to the roundtrip tester.",
    )
    parser.add_argument(
        "--keep-outputs",
        action="store_true",
        help="Keep generated USD files. Default: remove successful outputs after recording byte size.",
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=0,
        help="Limit number of discovered scenes. 0 means all scenes.",
    )
    parser.add_argument(
        "--summary",
        type=Path,
        default=None,
        help="Summary JSON path. Default: <output-dir>/summary.json",
    )
    parser.add_argument(
        "--include-xfail",
        action="store_true",
        help="Run known expected-failure cases. Default: skip them.",
    )
    parser.add_argument(
        "--only-xfail",
        action="store_true",
        help="Run only known expected-failure cases.",
    )
    return parser.parse_args()


def discover_scenes(root: Path, pattern: str, limit: int) -> list[Path]:
    matches = sorted(Path(p) for p in glob.glob(str(root / pattern), recursive=True))
    scenes = [p for p in matches if p.is_file()]
    if limit > 0:
        scenes = scenes[:limit]
    return scenes


def scene_id(root: Path, scene: Path) -> str:
    try:
        rel = scene.parent.relative_to(root)
    except ValueError:
        rel = scene.parent
    text = rel.as_posix().strip("/")
    if not text:
        text = scene.stem
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", text)


def output_path(out_dir: Path, sid: str, runner: str, fmt: str) -> Path:
    return out_dir / "usd" / runner / f"{sid}.{fmt}"


def log_path(out_dir: Path, sid: str, runner: str) -> Path:
    return out_dir / "logs" / runner / f"{sid}.log"


def command_for(args: argparse.Namespace, runner: str, scene: Path, out: Path) -> list[str]:
    base = [
        str(scene),
        "--input-format",
        "mjcf",
        "--format",
        args.format,
        "-o",
        str(out),
    ]
    if args.allow_missing:
        base.append("--allow-missing")
    if args.tessellate_collision_shapes:
        base.append("--tessellate-collision-shapes")

    if runner == "native":
        return [str(args.native_cli), *base]

    js_cmd = [args.node, str(args.js_cli), *base]
    if not args.js_verify:
        js_cmd.append("--no-verify")
    # The native writer defaults to a 1GB USDC cap; the JS/WASM CLI defaults to
    # a 100MB browser-safety cap. Raise it here so mesh-dense scenes (e.g.
    # apptronik_apollo at ~110MB) export under node, matching native.
    js_cmd += ["--max-usdc-mb", str(args.max_usdc_mb), "--max-mem-mb", str(args.max_mem_mb)]
    return js_cmd


def roundtrip_command_for(args: argparse.Namespace, out: Path) -> list[str]:
    cmd = [args.node, str(args.usd_urdf_roundtrip_cli), str(out)]
    if args.roundtrip_no_assert:
        cmd.append("--no-assert")
    return cmd


def known_xfail_reason(args: argparse.Namespace, runner: str, sid: str) -> str:
    return KNOWN_XFAIL.get((runner, sid), "")


def parse_counts(text: str) -> dict[str, int]:
    match = re.search(
        r"(?P<links>\d+)\s+links,\s+(?P<joints>\d+)\s+joints,\s+"
        r"(?P<visuals>\d+)\s+visual(?:\s+meshes)?,\s+"
        r"(?P<collisions>\d+)\s+collisions?",
        text,
    )
    if not match:
        match = re.search(
            r"(?P<links>\d+)\s+links,\s+(?P<joints>\d+)\s+joints,\s+"
            r"(?P<visuals>\d+)\s+visual\s+meshes,\s+"
            r"(?P<collisions>\d+)\s+collision\s+meshes",
            text,
        )
    if not match:
        return {}
    return {key: int(value) for key, value in match.groupdict().items()}


def run_case(args: argparse.Namespace, scene: Path, runner: str) -> dict[str, Any]:
    sid = scene_id(args.menagerie_root, scene)
    xfail_reason = known_xfail_reason(args, runner, sid)
    out = output_path(args.output_dir, sid, runner, args.format)
    log = log_path(args.output_dir, sid, runner)
    out.parent.mkdir(parents=True, exist_ok=True)
    log.parent.mkdir(parents=True, exist_ok=True)

    cmd = command_for(args, runner, scene, out)
    start = time.monotonic()
    timed_out = False
    roundtrip_output = ""
    roundtrip_rc = None
    roundtrip_timed_out = False
    roundtrip_elapsed = 0.0
    try:
        proc = subprocess.run(
            cmd,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=args.timeout,
            check=False,
        )
        rc = proc.returncode
        output = proc.stdout
    except subprocess.TimeoutExpired as exc:
        rc = 124
        timed_out = True
        output = (exc.stdout or "") + (exc.stderr or "")
        if isinstance(output, bytes):
            output = output.decode("utf-8", "replace")
        output += f"\nTIMEOUT after {args.timeout:.1f}s\n"
    export_elapsed = time.monotonic() - start

    byte_size = out.stat().st_size if out.exists() else 0
    export_ok = rc == 0 and byte_size > 0 and not timed_out
    if export_ok and args.roundtrip:
        rt_cmd = roundtrip_command_for(args, out)
        rt_start = time.monotonic()
        try:
            rt_proc = subprocess.run(
                rt_cmd,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                timeout=args.timeout,
                check=False,
            )
            roundtrip_rc = rt_proc.returncode
            roundtrip_output = rt_proc.stdout
        except subprocess.TimeoutExpired as exc:
            roundtrip_rc = 124
            roundtrip_timed_out = True
            roundtrip_output = (exc.stdout or "") + (exc.stderr or "")
            if isinstance(roundtrip_output, bytes):
                roundtrip_output = roundtrip_output.decode("utf-8", "replace")
            roundtrip_output += f"\nROUNDTRIP TIMEOUT after {args.timeout:.1f}s\n"
        roundtrip_elapsed = time.monotonic() - rt_start
        output += "\n\n$ " + " ".join(rt_cmd) + "\n\n" + roundtrip_output

    elapsed = export_elapsed + roundtrip_elapsed
    log.write_text("$ " + " ".join(cmd) + "\n\n" + output, encoding="utf-8")
    roundtrip_ok = (not args.roundtrip) or (
        roundtrip_rc == 0 and not roundtrip_timed_out
    )
    ok = export_ok and roundtrip_ok
    xfail = bool(xfail_reason)
    xpass = xfail and ok
    expected_fail = xfail and not ok
    if ok and not args.keep_outputs:
        try:
            out.unlink()
        except OSError:
            pass

    return {
        "scene": str(scene),
        "scene_id": sid,
        "runner": runner,
        "ok": ok,
        "xfail": xfail,
        "xpass": xpass,
        "expected_fail": expected_fail,
        "xfail_reason": xfail_reason,
        "returncode": rc,
        "timeout": timed_out,
        "export_ok": export_ok,
        "export_elapsed_sec": round(export_elapsed, 3),
        "roundtrip": args.roundtrip,
        "roundtrip_ok": roundtrip_ok,
        "roundtrip_returncode": roundtrip_rc,
        "roundtrip_timeout": roundtrip_timed_out,
        "roundtrip_elapsed_sec": round(roundtrip_elapsed, 3),
        "elapsed_sec": round(elapsed, 3),
        "output": str(out),
        "output_bytes": byte_size,
        "log": str(log),
        "counts": parse_counts(roundtrip_output if args.roundtrip else output),
        "last_output_lines": output.strip().splitlines()[-8:],
    }


def print_result(result: dict[str, Any]) -> None:
    if result.get("xpass"):
        status = "XPASS"
    elif result.get("expected_fail"):
        status = "XFAIL"
    else:
        status = "PASS" if result["ok"] else "FAIL"
    counts = result.get("counts") or {}
    count_text = ""
    if counts:
        count_text = (
            f" links={counts.get('links', 0)} joints={counts.get('joints', 0)}"
            f" visual={counts.get('visuals', 0)} collisions={counts.get('collisions', 0)}"
        )
    print(
        f"[{status}] {result['runner']:6s} {result['scene_id']}"
        f" {result['elapsed_sec']:.1f}s bytes={result['output_bytes']}"
        f"{' roundtrip' if result.get('roundtrip') else ''}{count_text}",
        flush=True,
    )


def main() -> int:
    args = parse_args()
    args.menagerie_root = args.menagerie_root.resolve()
    args.native_cli = args.native_cli.resolve()
    args.js_cli = args.js_cli.resolve()
    args.usd_urdf_roundtrip_cli = args.usd_urdf_roundtrip_cli.resolve()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    summary_path = args.summary or (args.output_dir / "summary.json")

    scenes = discover_scenes(args.menagerie_root, args.glob_pattern, args.limit)
    if not scenes:
        print(f"No scenes found under {args.menagerie_root} with {args.glob_pattern}", file=sys.stderr)
        return 2

    runners = ["native", "js"] if args.runner == "both" else [args.runner]
    if "native" in runners and not args.native_cli.exists():
        print(f"Native CLI not found: {args.native_cli}", file=sys.stderr)
        return 2
    if "js" in runners and not args.js_cli.exists():
        print(f"JS CLI not found: {args.js_cli}", file=sys.stderr)
        return 2
    if args.roundtrip and not args.usd_urdf_roundtrip_cli.exists():
        print(f"USD -> URDF roundtrip CLI not found: {args.usd_urdf_roundtrip_cli}", file=sys.stderr)
        return 2

    all_tasks = [(scene, runner) for scene in scenes for runner in runners]
    skipped_xfail: list[dict[str, str]] = []
    tasks = []
    for scene, runner in all_tasks:
        sid = scene_id(args.menagerie_root, scene)
        reason = known_xfail_reason(args, runner, sid)
        is_xfail = bool(reason)
        if args.only_xfail and not is_xfail:
            continue
        if is_xfail and not (args.include_xfail or args.only_xfail):
            skipped_xfail.append(
                {
                    "scene": str(scene),
                    "scene_id": sid,
                    "runner": runner,
                    "xfail_reason": reason,
                }
            )
            continue
        tasks.append((scene, runner))

    print(
        f"Discovered {len(scenes)} scenes; selected {len(tasks)} of "
        f"{len(all_tasks)} case(s). Skipped xfail: {len(skipped_xfail)}. "
        f"Output dir: {args.output_dir}",
        flush=True,
    )
    results: list[dict[str, Any]] = []
    max_workers = max(1, args.jobs)
    with concurrent.futures.ThreadPoolExecutor(max_workers=max_workers) as pool:
        future_to_task = {
            pool.submit(run_case, args, scene, runner): (scene, runner)
            for scene, runner in tasks
        }
        for future in concurrent.futures.as_completed(future_to_task):
            result = future.result()
            results.append(result)
            print_result(result)

    results.sort(key=lambda r: (r["scene_id"], r["runner"]))
    passed = sum(1 for r in results if r["ok"] and not r.get("xpass"))
    xfailed = sum(1 for r in results if r.get("expected_fail"))
    xpassed = sum(1 for r in results if r.get("xpass"))
    failed = sum(1 for r in results if not r["ok"] and not r.get("expected_fail"))
    summary = {
        "menagerie_root": str(args.menagerie_root),
        "glob_pattern": args.glob_pattern,
        "format": args.format,
        "roundtrip": args.roundtrip,
        "roundtrip_no_assert": args.roundtrip_no_assert,
        "allow_missing": args.allow_missing,
        "tessellate_collision_shapes": args.tessellate_collision_shapes,
        "js_verify": args.js_verify,
        "keep_outputs": args.keep_outputs,
        "include_xfail": args.include_xfail,
        "only_xfail": args.only_xfail,
        "total": len(results),
        "passed": passed,
        "xfailed": xfailed,
        "xpassed": xpassed,
        "failed": failed,
        "skipped_xfail": skipped_xfail,
        "results": results,
    }
    summary_path.parent.mkdir(parents=True, exist_ok=True)
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")

    print(
        f"Summary: {passed} passed, {xfailed} xfailed, {xpassed} xpassed, "
        f"{failed} failed, {len(skipped_xfail)} skipped xfail"
    )
    print(f"Summary JSON: {summary_path}")
    if failed:
        print("Failed cases:")
        for r in results:
            if not r["ok"] and not r.get("expected_fail"):
                print(f"  {r['runner']:6s} {r['scene_id']} rc={r['returncode']} log={r['log']}")
    if xpassed:
        print("Unexpected passes:")
        for r in results:
            if r.get("xpass"):
                print(f"  {r['runner']:6s} {r['scene_id']} reason={r['xfail_reason']}")
    return 0 if failed == 0 and xpassed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
