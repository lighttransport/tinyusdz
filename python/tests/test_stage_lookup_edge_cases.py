"""Stage.get_prim_at_path edge cases — invalid paths, deep paths,
relative paths, and prim/attribute boundary semantics.
"""
import tinyusdz


def _make_deep_stage():
    """Build /A/B/C/D/leaf with one attribute on the leaf."""
    s = tinyusdz.Stage()
    a = tinyusdz.Prim("Xform", name="A")
    b = tinyusdz.Prim("Xform", name="B")
    c = tinyusdz.Prim("Xform", name="C")
    d = tinyusdz.Prim("Xform", name="D")
    leaf = tinyusdz.Prim("Sphere", name="leaf")
    leaf.set_attribute("radius", 2.5, dtype="double")
    d.add_child(leaf)
    c.add_child(d)
    b.add_child(c)
    a.add_child(b)
    s.add_root_prim(a)
    return s


def test_get_root_prim_by_name():
    s = _make_deep_stage()
    p = s.get_prim_at_path("/A")
    assert p is not None
    assert p.name == "A"


def test_get_deep_prim():
    s = _make_deep_stage()
    p = s.get_prim_at_path("/A/B/C/D/leaf")
    assert p is not None
    assert p.name == "leaf"
    assert p.type_name == "Sphere"


def test_get_intermediate_prim():
    s = _make_deep_stage()
    p = s.get_prim_at_path("/A/B/C")
    assert p is not None
    assert p.type_name == "Xform"


def test_get_nonexistent_prim_returns_none():
    s = _make_deep_stage()
    assert s.get_prim_at_path("/A/B/C/Missing") is None
    assert s.get_prim_at_path("/Missing") is None
    assert s.get_prim_at_path("/A/Missing/C") is None


def test_get_prim_with_attribute_path_returns_none_or_prim():
    """`/A/B/C/D/leaf.radius` is an attribute path, not a prim path —
    `get_prim_at_path` should not return the attribute as a prim."""
    s = _make_deep_stage()
    # Most reasonable behavior: return None (path doesn't refer to a
    # prim). Either way, the value must NOT be a prim with type
    # "double" or similar.
    p = s.get_prim_at_path("/A/B/C/D/leaf.radius")
    assert p is None or p.type_name != "double"


def test_get_prim_with_empty_path_returns_none():
    s = _make_deep_stage()
    assert s.get_prim_at_path("") is None


def test_get_prim_with_root_path_returns_none_or_pseudoroot():
    """`/` is the pseudo-root. tinyusdz historically returns None for
    this; either way it must not crash."""
    s = _make_deep_stage()
    p = s.get_prim_at_path("/")
    # Acceptable: None (no real prim at /) or a pseudo-prim.
    if p is not None:
        assert p.name in ("", "/")


def test_get_prim_does_not_crash_on_garbage_path():
    s = _make_deep_stage()
    # Should not raise — just return None or a valid result.
    for bad in ["//", "/A//B", "not-a-path", "/A/B/.."]:
        try:
            s.get_prim_at_path(bad)
        except (ValueError, RuntimeError):
            pass  # rejecting malformed input is also acceptable


def test_root_prims_returns_top_level_only():
    s = _make_deep_stage()
    roots = s.root_prims()
    names = [p.name for p in roots]
    assert names == ["A"]


def test_deep_path_via_traverse():
    """Sanity: traverse() yields all prims, including deep ones."""
    s = _make_deep_stage()
    names = [p.name for p in tinyusdz.traverse(s)]
    for expected in ["A", "B", "C", "D", "leaf"]:
        assert expected in names
