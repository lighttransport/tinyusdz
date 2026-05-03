"""Prim.get_relationship_targets() API."""
import tinyusdz


def test_single_target(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "Owner" {
    rel link = </Target>
}
def Xform "Target" {}
''')
    s = tinyusdz.load(str(src))
    p = s.get_prim_at_path("/Owner")
    targets = p.get_relationship_targets("link")
    assert "/Target" in targets


def test_multi_target(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "Owner" {
    rel members = [</A>, </B>, </C>]
}
def Xform "A" {}
def Xform "B" {}
def Xform "C" {}
''')
    s = tinyusdz.load(str(src))
    p = s.get_prim_at_path("/Owner")
    targets = p.get_relationship_targets("members")
    assert set(targets) == {"/A", "/B", "/C"}


def test_get_targets_authored_in_python(tmp_path):
    s = tinyusdz.Stage()
    a = tinyusdz.Prim("Xform", name="A")
    o = tinyusdz.Prim("Xform", name="O")
    o.add_relationship("link", ["/A"])
    s.add_root_prim(a)
    s.add_root_prim(o)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    p = s2.get_prim_at_path("/O")
    targets = p.get_relationship_targets("link")
    assert "/A" in targets


def test_relationship_targets_property_path(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "Owner" {
    rel link = </Target.someAttr>
}
def Xform "Target" {
    custom int someAttr = 1
}
''')
    s = tinyusdz.load(str(src))
    p = s.get_prim_at_path("/Owner")
    targets = p.get_relationship_targets("link")
    assert any("Target" in t for t in targets)
