"""Stage-level prim iteration via traverse and root_prims."""
import tinyusdz


def test_root_prims_count():
    s = tinyusdz.Stage()
    s.add_root_prim(tinyusdz.Prim("Xform", name="A"))
    s.add_root_prim(tinyusdz.Prim("Sphere", name="B"))
    s.add_root_prim(tinyusdz.Prim("Cube", name="C"))
    rs = s.root_prims()
    assert len(rs) == 3
    names = [p.name for p in rs]
    assert names == ["A", "B", "C"]


def test_traverse_yields_all_descendants():
    s = tinyusdz.Stage()
    root = tinyusdz.Prim("Xform", name="root")
    child = tinyusdz.Prim("Xform", name="child")
    leaf = tinyusdz.Prim("Sphere", name="leaf")
    child.add_child(leaf)
    root.add_child(child)
    s.add_root_prim(root)

    names = [p.name for p in tinyusdz.traverse(s)]
    assert names == ["root", "child", "leaf"]


def test_get_prim_at_path_missing():
    s = tinyusdz.Stage()
    s.add_root_prim(tinyusdz.Prim("Xform", name="X"))
    p = s.get_prim_at_path("/Missing")
    assert p is None


def test_get_prim_at_path_root():
    s = tinyusdz.Stage()
    s.add_root_prim(tinyusdz.Prim("Xform", name="X"))
    p = s.get_prim_at_path("/X")
    assert p is not None
    assert p.name == "X"


def test_get_prim_at_path_nested(tmp_path):
    s = tinyusdz.Stage()
    root = tinyusdz.Prim("Xform", name="root")
    child = tinyusdz.Prim("Xform", name="child")
    root.add_child(child)
    s.add_root_prim(root)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    p = s2.get_prim_at_path("/root/child")
    assert p is not None
    assert p.name == "child"


def test_get_prim_at_path_grand_missing(tmp_path):
    s = tinyusdz.Stage()
    root = tinyusdz.Prim("Xform", name="root")
    s.add_root_prim(root)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    assert s2.get_prim_at_path("/root/nope") is None
