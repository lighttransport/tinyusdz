"""Relationships with multiple targets and various qualifiers."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_rel_multiple_targets(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "Owner" {
    rel things = [</A>, </B>, </C>, </D>]
}
def Xform "A" {}
def Xform "B" {}
def Xform "C" {}
def Xform "D" {}
''')
    assert "</A>" in txt
    assert "</B>" in txt
    assert "</C>" in txt
    assert "</D>" in txt


def test_rel_with_listop_explicit_targets(tmp_path):
    """Multiple explicit targets without listedit qualifier."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "Owner" {
    rel members = [</X>, </Y>]
}
def Xform "X" {}
def Xform "Y" {}
''')
    assert "members" in txt
    assert "</X>" in txt
    assert "</Y>" in txt


def test_rel_prepend_qualifier(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "Owner" {
    prepend rel things = [</A>]
}
def Xform "A" {}
''')
    assert "prepend rel" in txt or "prepend" in txt


def test_rel_append_qualifier(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "Owner" {
    append rel things = [</A>]
}
def Xform "A" {}
''')
    assert "append" in txt


def test_rel_to_property_path(tmp_path):
    """Relationship target can reference a property path."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "Owner" {
    rel link = </Target.someAttr>
}
def Xform "Target" {
    custom int someAttr = 1
}
''')
    assert "</Target.someAttr>" in txt


def test_attr_connection_single(tmp_path):
    """Attribute connection with a single source target."""
    txt = _rt(tmp_path, '''#usda 1.0
def Material "M" {
    def Shader "Mix" {
        float inputs:weight.connect = </M/A.outputs:result>
        token outputs:result
    }
    def Shader "A" {
        float outputs:result
    }
}
''')
    assert "</M/A.outputs:result>" in txt
