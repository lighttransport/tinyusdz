"""Prim.variant_selection() zero-arg returns dict of all selections."""
import tinyusdz


def test_zero_arg_returns_dict():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_variant_selection("color", "red")
    p.set_variant_selection("size", "M")
    sel = p.variant_selection()
    assert isinstance(sel, dict)
    assert sel == {"color": "red", "size": "M"}


def test_zero_arg_empty():
    p = tinyusdz.Prim("Xform", name="X")
    assert p.variant_selection() == {}


def test_single_arg_still_works():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_variant_selection("color", "blue")
    assert p.variant_selection("color") == "blue"


def test_zero_arg_after_clear():
    p = tinyusdz.Prim("Xform", name="X")
    p.set_variant_selection("color", "red")
    p.clear_variant_selection("color")
    assert p.variant_selection() == {}


def test_zero_arg_round_trip(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_variant_set_name("color")
    p.set_variant_selection("color", "red")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    p2 = s2.get_prim_at_path("/X")
    assert p2.variant_selection() == {"color": "red"}
