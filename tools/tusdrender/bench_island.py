#!/usr/bin/env python3
"""Benchmark tusdrender vs. Pixar usdrecord (hdEmbree) on Moana Island elements.

For each independent per-element geometry USD file this runs both renderers under
``/usr/bin/time -v`` (peak RSS + wall) and, for tusdrender, parses the ``-stats``
load / triangle-stream / BVH-build / render breakdown. Results are written as a
Markdown table plus per-run JSON.

This mirrors the methodology of the Activision Caldera benchmark documented in
``tools/tusdrender/README.md`` and ``doc/large-scene.md``.

Example:
    python3 tools/tusdrender/bench_island.py \
        --island /mnt/disk1/data/island \
        --dist /mnt/nvme02/work/lightusd-repo/OpenUSD/dist \
        --out /tmp/island_bench
"""

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import time

# Graduated small -> large ladder of independent element geom files.
DEFAULT_ELEMENTS = [
    "isNaupakaA",
    "isGardeniaA",
    "isPalmDead",
    "isHibiscus",
    "isDunesA",
    "isIronwoodA1",
    "isCoral",
    "isBeach",
]

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def dir_size_bytes(path):
    total = 0
    for root, _dirs, files in os.walk(path):
        for f in files:
            try:
                total += os.path.getsize(os.path.join(root, f))
            except OSError:
                pass
    return total


def human_bytes(n):
    if n is None:
        return "-"
    for unit, div in (("GB", 1 << 30), ("MB", 1 << 20), ("KB", 1 << 10)):
        if n >= div:
            return f"{n / div:.1f} {unit}"
    return f"{n} B"


def run_timed(cmd, env=None, timeout=None):
    """Run ``cmd`` under /usr/bin/time -v; return (returncode, stdout, stderr,
    peak_rss_bytes, wall_seconds, timed_out)."""
    time_bin = shutil.which("time") or "/usr/bin/time"
    full = ["/usr/bin/time", "-v"] + cmd
    t0 = time.monotonic()
    timed_out = False
    try:
        proc = subprocess.run(
            full, env=env, capture_output=True, text=True, timeout=timeout
        )
        rc, out, err = proc.returncode, proc.stdout, proc.stderr
    except subprocess.TimeoutExpired as e:
        timed_out = True
        rc, out = -1, (e.stdout or "")
        err = (e.stderr or "") + "\n[TIMEOUT]"
    wall = time.monotonic() - t0

    peak_rss = None
    m = re.search(r"Maximum resident set size \(kbytes\):\s*(\d+)", err)
    if m:
        peak_rss = int(m.group(1)) * 1024
    # Prefer /usr/bin/time's own elapsed if present.
    me = re.search(r"Elapsed \(wall clock\) time .*?:\s*([0-9:.]+)", err)
    if me:
        wall = parse_elapsed(me.group(1))
    return rc, out, err, peak_rss, wall, timed_out


def parse_elapsed(s):
    parts = s.split(":")
    parts = [float(p) for p in parts]
    if len(parts) == 3:
        return parts[0] * 3600 + parts[1] * 60 + parts[2]
    if len(parts) == 2:
        return parts[0] * 60 + parts[1]
    return parts[0]


def parse_tusd_stats(err):
    stats = {}
    patterns = {
        "load_s": r"load seconds:\s*([0-9.eE+-]+)",
        "stream_s": r"rt triangle stream seconds:\s*([0-9.eE+-]+)",
        "bvh_s": r"rt bvh build seconds:\s*([0-9.eE+-]+)",
        "render_s": r"render seconds:\s*([0-9.eE+-]+)",
        "triangles": r"^triangles:\s*(\d+)",
        "bvh_mem": r"bvh nodes:.*memory:\s*(\d+) bytes",
        "mem_cap": r"memory cap:\s*([0-9.]+) GiB",
    }
    for key, pat in patterns.items():
        m = re.search(pat, err, re.MULTILINE)
        if m:
            v = m.group(1)
            stats[key] = int(v) if key in ("triangles", "bvh_mem") else float(v)
    aborted = "memory cap" in err and "Aborting" in err
    stats["aborted"] = aborted
    return stats


def run_tusdrender(binpath, scene, out_png, width, height, extra, timeout):
    cmd = [
        binpath, scene, out_png,
        "-rtPreview", "-stats",
        "-w", str(width), "-height", str(height),
        "-autoframe",
    ] + extra
    rc, out, err, rss, wall, to = run_timed(cmd, timeout=timeout)
    stats = parse_tusd_stats(err)
    ok = rc == 0 and os.path.exists(out_png) and os.path.getsize(out_png) > 0
    return {
        "ok": ok, "rc": rc, "timed_out": to, "peak_rss": rss, "wall_s": wall,
        "stats": stats, "png": out_png if ok else None,
        "stderr_tail": "\n".join(err.strip().splitlines()[-8:]),
    }


def run_usdrecord(dist, scene, out_png, width, timeout):
    env = dict(os.environ)
    env["PYTHONPATH"] = os.path.join(dist, "lib", "python")
    env["LD_LIBRARY_PATH"] = os.path.join(dist, "lib") + ":" + env.get(
        "LD_LIBRARY_PATH", ""
    )
    env["PXR_PLUGINPATH_NAME"] = os.path.join(dist, "plugin", "usd")
    cmd = [
        os.path.join(dist, "bin", "usdrecord"),
        "--renderer", "Embree", "--disableGpu",
        "--imageWidth", str(width),
        scene, out_png,
    ]
    rc, out, err, rss, wall, to = run_timed(cmd, env=env, timeout=timeout)
    ok = rc == 0 and os.path.exists(out_png) and os.path.getsize(out_png) > 0
    return {
        "ok": ok, "rc": rc, "timed_out": to, "peak_rss": rss, "wall_s": wall,
        "png": out_png if ok else None,
        "stderr_tail": "\n".join((err or "").strip().splitlines()[-8:]),
    }


def fmt_s(x):
    return f"{x:.2f}" if isinstance(x, (int, float)) else "-"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--island", default="/mnt/disk1/data/island",
                    help="Moana Island dataset root")
    ap.add_argument("--dist", default=os.path.join(
        os.path.dirname(REPO), "OpenUSD", "dist"),
        help="OpenUSD dist dir (contains bin/usdrecord, lib/, plugin/)")
    ap.add_argument("--bin", default=os.path.join(
        REPO, "build_ninja", "tools", "tusdrender", "tusdrender"),
        help="tusdrender binary")
    ap.add_argument("--out", default="/tmp/island_bench", help="output dir")
    ap.add_argument("--width", type=int, default=320)
    ap.add_argument("--height", type=int, default=180)
    ap.add_argument("--elements", nargs="*", default=DEFAULT_ELEMENTS)
    ap.add_argument("--timeout", type=float, default=1200.0,
                    help="per-run timeout seconds")
    ap.add_argument("--tusd-extra", default="",
                    help="extra args passed to tusdrender as one string, "
                         "shlex-split (e.g. --tusd-extra='-maxMem 4')")
    ap.add_argument("--skip-usdrecord", action="store_true")
    ap.add_argument("--skip-tusdrender", action="store_true")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    rows = []
    for name in args.elements:
        eldir = os.path.join(args.island, "usd", "elements", name)
        scene = os.path.join(eldir, "element.usda")
        if not os.path.exists(scene):
            print(f"!! skip {name}: {scene} not found", file=sys.stderr)
            continue
        size = dir_size_bytes(eldir)
        print(f"\n=== {name}  ({human_bytes(size)}) ===", file=sys.stderr)
        row = {"element": name, "dir_bytes": size}

        if not args.skip_tusdrender:
            out_png = os.path.join(args.out, f"{name}_tusd.png")
            print("  tusdrender ...", file=sys.stderr)
            r = run_tusdrender(args.bin, scene, out_png, args.width,
                               args.height, shlex.split(args.tusd_extra),
                               args.timeout)
            row["tusd"] = r
            st = r["stats"]
            print(f"    ok={r['ok']} tris={st.get('triangles')} "
                  f"load={st.get('load_s')} bvh={st.get('bvh_s')} "
                  f"render={st.get('render_s')} wall={r['wall_s']:.2f} "
                  f"rss={human_bytes(r['peak_rss'])}", file=sys.stderr)
            if not r["ok"]:
                print(f"    stderr: {r['stderr_tail']}", file=sys.stderr)

        if not args.skip_usdrecord:
            out_png = os.path.join(args.out, f"{name}_usdr.png")
            print("  usdrecord(Embree) ...", file=sys.stderr)
            r = run_usdrecord(args.dist, scene, out_png, args.width, args.timeout)
            row["usdr"] = r
            print(f"    ok={r['ok']} wall={r['wall_s']:.2f} "
                  f"rss={human_bytes(r['peak_rss'])}", file=sys.stderr)
            if not r["ok"]:
                print(f"    stderr: {r['stderr_tail']}", file=sys.stderr)

        rows.append(row)

    # JSON dump
    json_path = os.path.join(args.out, "results.json")
    with open(json_path, "w") as f:
        json.dump({"args": vars(args), "rows": rows}, f, indent=2)

    # Markdown table
    md = []
    md.append(f"| element | size | tris | tusd load s | tusd bvh s | "
              f"tusd render s | tusd total s | tusd RSS | usdrecord s | "
              f"usdrecord RSS | speedup |")
    md.append("|---|---|---|---|---|---|---|---|---|---|---|")
    for row in rows:
        t = row.get("tusd", {})
        u = row.get("usdr", {})
        st = t.get("stats", {})
        tusd_total = t.get("wall_s")
        usdr_total = u.get("wall_s")
        speed = "-"
        if isinstance(tusd_total, (int, float)) and isinstance(
                usdr_total, (int, float)) and tusd_total > 0:
            speed = f"{usdr_total / tusd_total:.1f}x"
        tris = st.get("triangles")
        tris_s = f"{tris/1e6:.2f} M" if tris else "-"
        tusd_status = "" if t.get("ok", True) else (
            " (ABORT)" if st.get("aborted") else " (FAIL)")
        usdr_status = "" if u.get("ok", True) else " (FAIL)"
        md.append(
            f"| {row['element']} | {human_bytes(row['dir_bytes'])} | {tris_s} | "
            f"{fmt_s(st.get('load_s'))} | {fmt_s(st.get('bvh_s'))} | "
            f"{fmt_s(st.get('render_s'))} | {fmt_s(tusd_total)}{tusd_status} | "
            f"{human_bytes(t.get('peak_rss'))} | {fmt_s(usdr_total)}{usdr_status} | "
            f"{human_bytes(u.get('peak_rss'))} | {speed} |"
        )
    md_text = "\n".join(md)
    md_path = os.path.join(args.out, "results.md")
    with open(md_path, "w") as f:
        f.write(md_text + "\n")

    print("\n" + md_text)
    print(f"\nJSON: {json_path}\nMarkdown: {md_path}", file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main())
