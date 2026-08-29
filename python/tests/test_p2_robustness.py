# SPDX-License-Identifier: Apache-2.0
"""P2: Robustness — malformed/truncated USDA/USDC, overlong tokens,
negative indices, fuzz-style inputs. All must raise UsdError, never crash."""
import pytest

import tinyusdz

np = pytest.importorskip("numpy")


def test_garbage_bytes():
    for blob in [b"", b"\x00\x01\x02", b"not usd", b"\xff"*1024, b"PXR-USDC truncated"]:
        with pytest.raises((tinyusdz.UsdError, ValueError)):
            tinyusdz.load_bytes(blob)


def test_malformed_usda():
    bads = [
        "#usda 1.0\ndef Xform {",  # missing quote
        "#usda 1.0\ndef Xform \"A\" { float a = }",
        "#usda 1.0\ndef Xform \"A\" { float a = \"oops\" }",
        "#usda 1.0\n" + "def Xform \"A\" {" * 1000,  # deep nesting
    ]
    for txt in bads:
        with pytest.raises((tinyusdz.UsdError, tinyusdz.UsdParseError, ValueError)):
            tinyusdz.loads(txt)


def test_truncated_usdc():
    st = tinyusdz.Stage.create()
    st.define_prim("/X", "Xform").set("v", 1.0, type="float")
    blob = st.export_usdc()
    assert blob[:8] == b"PXR-USDC"
    for cut in [10, 100, len(blob)//2, len(blob)-1]:
        with pytest.raises(tinyusdz.UsdError):
            tinyusdz.load_bytes(blob[:cut])


def test_overlong_token():
    tok = "x" * 5000
    st = tinyusdz.Stage.create()
    p = st.define_prim("/P", "Xform")
    p.set("tok", tok, type="token")
    # roundtrip via USDA may truncate or keep, but must not crash
    txt = st.export_usda()
    st2 = tinyusdz.loads(txt)
    # Either preserves or truncates, but load must succeed and not crash
    assert st2.prim_at("/P") is not None


def test_negative_face_indices_tydra_warns():
    from tinyusdz import tydra
    st = tinyusdz.Stage.create()
    m = st.define_prim("/M", "Mesh")
    pts = np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0]], np.float32)
    m.set("points", pts, type="point3f[]")
    m.set("faceVertexCounts", np.array([3], dtype=np.int32), type="int[]")
    # negative index – USDA allows int, but tydra should warn/skip, not crash
    m.set("faceVertexIndices", np.array([0, 1, -1], dtype=np.int32), type="int[]")
    scene = tydra.to_render_scene(st)
    # Should either have 0 meshes or 1 mesh with warning; must not crash
    assert isinstance(scene.meshes, tuple)
    assert isinstance(scene.warnings(), list)


def test_very_large_array_max_memory():
    st = tinyusdz.Stage.create()
    n = 200_000
    m = st.define_prim("/M", "Mesh")
    m.set("points", np.zeros((n, 3), np.float32), type="point3f[]")
    m.set("faceVertexCounts", np.full(n // 3, 3, dtype=np.int32), type="int[]")
    m.set("faceVertexIndices", np.arange((n // 3) * 3, dtype=np.int32), type="int[]")
    import tempfile, pathlib
    import tempfile as tf
    with tf.NamedTemporaryFile(suffix=".usdc", delete=False) as f:
        fname = f.name
    try:
        st.save(fname)
        # tiny budget must raise, not crash
        with pytest.raises(tinyusdz.UsdError):
            tinyusdz.load(fname, max_memory=4096)
        # large budget must succeed
        st2 = tinyusdz.load(fname, max_memory=200*1024*1024)
        assert st2.stats["prim_count"] >= 1
    finally:
        pathlib.Path(fname).unlink(missing_ok=True)


def test_fuzz_random_usda_tokens():
    # Generate random but syntactically plausible USDA with many tokens
    import random
    random.seed(0)
    for _ in range(20):
        name = "P" + str(random.randint(0, 9999))
        tok = "".join(random.choice("abcdefghijklmnopqrstuvwxyz") for _ in range(random.randint(1, 20)))
        txt = f'#usda 1.0\ndef Xform "{name}" {{ token t = "{tok}" }}\n'
        # Must either load or raise UsdParseError, never crash
        try:
            st = tinyusdz.loads(txt)
            assert st.get_prim_at(f"/{name}") is not None
        except tinyusdz.UsdError:
            pass


def test_empty_and_whitespace_usda():
    for txt in ["", "   ", "\n", "#usda 1.0\n"]:
        try:
            st = tinyusdz.loads(txt)
            # Empty stage may be allowed (0 prims)
            assert st.stats["prim_count"] >= 0
        except (tinyusdz.UsdError, ValueError):
            pass


def test_double_free_close_idempotent():
    st = tinyusdz.Stage.create()
    st.define_prim("/A", "Xform")
    st.close()
    st.close()  # double close must not crash
    with pytest.raises(tinyusdz.UsdError):
        st.prim_at("/A")
