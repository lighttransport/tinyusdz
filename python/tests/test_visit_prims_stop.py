"""Stage.visit_prims early-stop semantics and exception propagation."""
import pytest
import tinyusdz


def test_visit_callback_collects_paths():
    s = tinyusdz.Stage()
    root = tinyusdz.Prim("Xform", name="root")
    a = tinyusdz.Prim("Xform", name="a")
    b = tinyusdz.Sphere if hasattr(tinyusdz, "Sphere") else tinyusdz.Prim("Sphere", name="b")
    if not isinstance(b, tinyusdz.Prim):
        b = tinyusdz.Prim("Sphere", name="b")
    root.add_child(a)
    root.add_child(b)
    s.add_root_prim(root)

    paths = []
    def cb(prim, path, depth):
        paths.append(path)
        return True

    s.visit_prims(cb)
    assert "/root" in paths
    assert "/root/a" in paths
    assert "/root/b" in paths


def test_visit_callback_continue_visits_all():
    s = tinyusdz.Stage()
    for i in range(4):
        s.add_root_prim(tinyusdz.Prim("Xform", name=f"R{i}"))

    visited = []
    def cb(prim, path, depth):
        visited.append(prim.name)
        return True

    s.visit_prims(cb)
    assert set(visited) == {f"R{i}" for i in range(4)}


def test_visit_callback_exception_propagates():
    s = tinyusdz.Stage()
    s.add_root_prim(tinyusdz.Prim("Xform", name="X"))

    def cb(prim, path, depth):
        raise ValueError("from callback")

    with pytest.raises((ValueError, tinyusdz.UsdError)):
        s.visit_prims(cb)


def test_traverse_yields_iterable():
    s = tinyusdz.Stage()
    s.add_root_prim(tinyusdz.Prim("Xform", name="A"))
    s.add_root_prim(tinyusdz.Prim("Sphere", name="B"))

    it = tinyusdz.traverse(s)
    names = [p.name for p in it]
    assert names == ["A", "B"]


def test_traverse_post_order_or_pre_order():
    """Whatever the iteration order, every prim is visited exactly once."""
    s = tinyusdz.Stage()
    root = tinyusdz.Prim("Xform", name="root")
    for n in ("a", "b", "c"):
        root.add_child(tinyusdz.Prim("Xform", name=n))
    s.add_root_prim(root)
    names = sorted(p.name for p in tinyusdz.traverse(s))
    assert names == ["a", "b", "c", "root"]
