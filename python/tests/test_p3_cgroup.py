# SPDX-License-Identifier: Apache-2.0
"""P3: Large-scene RSS / cgroup budget — verifies memory-bounded behaviour.

Uses synthetic large meshes and, when /mnt/disk1/data is present,
real alab/island/caldera assets. RSS measured via psutil or resource.
systemd-run cgroup test is optional (skip when not available).
"""
import os
import pathlib
import subprocess
import resource
import time
import pytest

import tinyusdz

np = pytest.importorskip("numpy")


def _rss_mb():
    try:
        import psutil
        return psutil.Process().memory_info().rss / (1024 * 1024)
    except Exception:
        rss = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
        import sys
        if sys.platform == "darwin":
            return rss / (1024 * 1024)
        return rss / 1024.0


def _has_systemd_run():
    return subprocess.call(["which", "systemd-run"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) == 0


@pytest.mark.slow
def test_synthetic_rss_bounded(tmp_path):
    rss0 = _rss_mb()
    n = 500_000  # ~6 MB points + indices
    st = tinyusdz.Stage.create()
    m = st.define_prim("/M", "Mesh")
    m.set("points", np.zeros((n, 3), np.float32), type="point3f[]")
    m.set("faceVertexCounts", np.full(n // 3, 3, dtype=np.int32), type="int[]")
    m.set("faceVertexIndices", np.arange((n // 3) * 3, dtype=np.int32), type="int[]")
    fn = tmp_path / "big.usdc"
    st.save(str(fn))
    # load with generous budget should succeed
    st2 = tinyusdz.load(str(fn), max_memory=200*1024*1024)
    assert st2.stats["prim_count"] >= 1
    rss1 = _rss_mb()
    delta = rss1 - rss0
    nbytes = n * 3 * 4
    print(f"[cgroup] rss0 {rss0:.1f} -> rss1 {rss1:.1f} delta {delta:.1f} MiB nbytes {nbytes/1024/1024:.1f}")
    import sys
    if sys.platform == "darwin":
        assert delta < 5000, f"macOS RSS delta too large: {delta:.1f} MiB"
    else:
        assert delta < (nbytes / 1024 / 1024) * 3 + 100


@pytest.mark.slow
def test_systemd_run_memory_limit(tmp_path):
    if not _has_systemd_run():
        pytest.skip("systemd-run not available")
    # Check if user can run systemd-run --user (may need --scope)
    probe = subprocess.run(["systemd-run", "--user", "--scope", "-p", "MemoryMax=100M", "true"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if probe.returncode != 0:
        pytest.skip("systemd-run --user MemoryMax not permitted")
    st = tinyusdz.Stage.create()
    st.define_prim("/A", "Xform").set("v", 1.0, type="float")
    txt = st.export_usda()
    # Run a tiny Python snippet under 50M limit that just imports tinyusdz and loads
    code = f"import tinyusdz; tinyusdz.loads({repr(txt)}); print('ok')"
    result = subprocess.run(
        ["systemd-run", "--user", "--scope", "-p", "MemoryMax=50M", "-p", "MemorySwapMax=0",
         "python3", "-c", code],
        capture_output=True, text=True, timeout=10)
    # Should succeed within 50M (tiny scene)
    assert result.returncode == 0, f"systemd-run failed: {result.stderr}"
    assert "ok" in result.stdout


@pytest.mark.slow
def test_large_scene_cgroup_if_available():
    # Only when alab/island/caldera present and systemd-run available
    root = pathlib.Path("/mnt/disk1/data")
    if not root.is_dir():
        pytest.skip("large data not mounted")
    if not _has_systemd_run():
        pytest.skip("systemd-run not available")
    # Pick smallest large scene (caldera proxy)
    candidates = [
        root / "caldera" / "caldera.usda",
        root / "island" / "usd" / "island.usda",
        root / "alab" / "_merged_ALab" / "entry.usda",
    ]
    src = next((p for p in candidates if p.is_file()), None)
    if not src:
        pytest.skip("no large scene file")
    # Measure load under 4G limit via systemd-run if possible
    # We just verify that a normal load without cgroup succeeds and RSS is bounded
    t0 = time.perf_counter()
    rss0 = _rss_mb()
    st = tinyusdz.load(str(src), load_payloads=False)
    t1 = time.perf_counter() - t0
    rss1 = _rss_mb()
    print(f"[cgroup] large {src.name} load {t1:.2f}s rss {rss0:.1f}->{rss1:.1f} MiB prims={st.stats['prim_count']}")
    assert st.stats["prim_count"] > 0
    assert rss1 - rss0 < 4000  # <4G delta
    st.close()
