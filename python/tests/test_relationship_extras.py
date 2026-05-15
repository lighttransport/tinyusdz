"""Relationship target listops, multi-target relationships, and
relationship metadata.
"""
import tinyusdz


def _rt(tmp_path, usda, fmt="usdc"):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / f"x.{fmt}"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_relationship_with_multiple_targets(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    rel binding = [</A>, </B>, </C>]
}
''')
    assert "</A>" in txt
    assert "</B>" in txt
    assert "</C>" in txt


def test_relationship_with_no_targets_usda_only(tmp_path):
    """A `rel name` declaration with no `=` still defines the rel.
    USDA->USDA preserves; USDC dropping is a separate, pre-existing
    issue (define-only relationships are not currently emitted to the
    crate field stream)."""
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" {
    rel binding
}
''')
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = tinyusdz.load(str(out)).export_to_string()
    assert "rel binding" in txt


def test_relationship_listop_prepend_only(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    prepend rel binding = [</Foo>]
}
''')
    assert "prepend rel binding" in txt
    assert "</Foo>" in txt


def test_relationship_listop_append_only(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    append rel binding = [</Bar>]
}
''')
    assert "append rel binding" in txt
    assert "</Bar>" in txt


def test_relationship_with_metadata(tmp_path):
    """Relationship-level metadata (displayName) survives round-trip."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    rel binding = </A> (
        displayName = "primary"
    )
}
''')
    # Metadata may print on the same or following lines.
    assert '"primary"' in txt or "displayName" in txt


def test_python_add_relationship_single_target(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_relationship("link", "/Other")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert "rel link = </Other>" in txt


def test_python_add_relationship_multiple_targets(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_relationship("links", ["/A", "/B", "/C"])
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert "</A>" in txt and "</B>" in txt and "</C>" in txt


def test_get_relationship_targets_after_roundtrip(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_relationship("links", ["/A", "/B"])
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    p2 = s2.get_prim_at_path("/X")
    targets = p2.get_relationship_targets("links")
    assert targets is not None
    assert "/A" in targets
    assert "/B" in targets


def test_get_missing_relationship_returns_none():
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    s.add_root_prim(p)
    assert p.get_relationship_targets("nonexistent") is None
