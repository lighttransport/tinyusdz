"""Larger array values stress test."""
import numpy as np
import tinyusdz


def test_1k_int_array(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("v", list(range(1000)), dtype="int[]")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    arr = np.asarray(s2.get_prim_at_path("/X").get_attribute("v").value)
    assert len(arr) == 1000
    assert arr[0] == 0
    assert arr[-1] == 999


def test_10k_float_array(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    vals = [i * 0.1 for i in range(10_000)]
    p.set_attribute("v", vals, dtype="float[]")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    arr = np.asarray(s2.get_prim_at_path("/X").get_attribute("v").value)
    assert arr.shape == (10_000,)
    assert abs(arr[5000] - 500.0) < 1e-2


def test_long_string_array(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("tags", [f"tag_{i}" for i in range(100)],
                    dtype="string[]")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert '"tag_99"' in txt


def test_many_prims(tmp_path):
    s = tinyusdz.Stage()
    for i in range(200):
        p = tinyusdz.Prim("Xform", name=f"P_{i}")
        p.set_attribute("idx", i)
        s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    assert len(s2.root_prims()) == 200


def test_deep_hierarchy(tmp_path):
    """Build the tree bottom-up before attaching to the stage."""
    s = tinyusdz.Stage()
    leaf = tinyusdz.Prim("Sphere", name="L29")
    cur = leaf
    for i in range(28, -1, -1):
        parent = tinyusdz.Prim("Xform", name=f"L{i}")
        parent.add_child(cur)
        cur = parent
    s.add_root_prim(cur)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    path = "/" + "/".join(f"L{i}" for i in range(30))
    assert s2.get_prim_at_path(path) is not None
