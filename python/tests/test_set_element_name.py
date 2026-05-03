"""Prim.set_element_name and element_name reads."""
import tinyusdz


def test_initial_element_name_matches_constructor():
    p = tinyusdz.Prim("Xform", name="alpha")
    assert p.element_name == "alpha"
    assert p.name == "alpha"


def test_set_element_name_changes_name():
    p = tinyusdz.Prim("Xform", name="old")
    p.set_element_name("new")
    assert p.element_name == "new"
    assert p.name == "new"


def test_renamed_prim_persists_through_save(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="orig")
    p.set_element_name("renamed")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))

    s2 = tinyusdz.load(str(out))
    assert s2.get_prim_at_path("/renamed") is not None
    assert s2.get_prim_at_path("/orig") is None


def test_element_name_underscore_and_digit():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_element_name("_a1")
    assert p.element_name == "_a1"
    p.set_element_name("foo_42_bar")
    assert p.element_name == "foo_42_bar"
