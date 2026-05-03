"""Prim.children() API."""
import tinyusdz


def test_children_on_orphan_prim():
    """Prim not yet attached to a stage — children() must not crash."""
    root = tinyusdz.Prim("Xform", name="root")
    a = tinyusdz.Prim("Xform", name="a")
    root.add_child(a)
    ch = root.children()
    assert len(ch) == 1
    assert ch[0].name == "a"


def test_children_empty():
    p = tinyusdz.Prim("Sphere", name="leaf")
    assert p.children() == []


def test_children_three_in_order():
    root = tinyusdz.Prim("Xform", name="root")
    for n in ("alpha", "beta", "gamma"):
        root.add_child(tinyusdz.Prim("Xform", name=n))
    names = [c.name for c in root.children()]
    assert names == ["alpha", "beta", "gamma"]


def test_children_after_save_load(tmp_path):
    s = tinyusdz.Stage()
    r = tinyusdz.Prim("Xform", name="r")
    r.add_child(tinyusdz.Prim("Sphere", name="s1"))
    r.add_child(tinyusdz.Prim("Cube", name="c1"))
    s.add_root_prim(r)
    out = tmp_path / "x.usdc"
    s.save(str(out))

    s2 = tinyusdz.load(str(out))
    r2 = s2.get_prim_at_path("/r")
    ch = r2.children()
    assert len(ch) == 2
    types = sorted(c.type_name for c in ch)
    assert types == ["Cube", "Sphere"]


def test_children_grandchildren_orphan():
    """Multi-level orphan tree — children() walks both levels."""
    root = tinyusdz.Prim("Xform", name="root")
    mid = tinyusdz.Prim("Xform", name="mid")
    leaf = tinyusdz.Prim("Sphere", name="leaf")
    mid.add_child(leaf)
    root.add_child(mid)
    mid_actual = root.children()[0]
    assert mid_actual.name == "mid"
    leaves = mid_actual.children()
    assert len(leaves) == 1
    assert leaves[0].name == "leaf"
