"""Stage / Prim __repr__ surface authored stage metadata + child counts.

Phase C.9.
"""
import tinyusdz


def test_empty_stage_repr_is_minimal():
    s = tinyusdz.Stage()
    r = repr(s)
    assert r == "<tinyusdz.Stage root_prims=0>"


def test_stage_repr_shows_authored_meta():
    s = tinyusdz.Stage()
    s.set_default_prim("Foo")
    s.set_up_axis("Z")
    s.set_meters_per_unit(0.01)
    s.add_root_prim(tinyusdz.Prim("Xform", name="Foo"))
    r = repr(s)
    assert "root_prims=1" in r
    assert "defaultPrim=Foo" in r
    assert "upAxis=Z" in r
    assert "metersPerUnit=0.01" in r


def test_stage_repr_omits_unauthored_meta():
    s = tinyusdz.Stage()
    s.set_default_prim("Foo")
    r = repr(s)
    assert "defaultPrim=Foo" in r
    assert "upAxis" not in r
    assert "metersPerUnit" not in r


def test_prim_repr_shows_child_count():
    p = tinyusdz.Prim("Xform", name="Root")
    p.add_child(tinyusdz.Prim("Xform", name="a"))
    p.add_child(tinyusdz.Prim("Xform", name="b"))
    p.add_child(tinyusdz.Prim("Mesh", name="m"))
    r = repr(p)
    assert "type=Xform" in r
    assert "name=Root" in r
    assert "children=3" in r


def test_prim_repr_zero_children():
    p = tinyusdz.Prim("Mesh", name="m")
    assert "children=0" in repr(p)
