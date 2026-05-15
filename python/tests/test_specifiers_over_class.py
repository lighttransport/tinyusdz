"""Prim specifiers: `def`, `over`, `class`."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_over_specifier(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
over "X" {
    custom int n = 5
}
''')
    assert "over " in txt and '"X"' in txt


def test_class_specifier(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
class "Base" {
    custom string label = "base"
}
''')
    assert "class " in txt and '"Base"' in txt


def test_class_with_inherit(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
class "Base" {
    custom int weight = 1
}
def Xform "Derived" (
    inherits = </Base>
) {}
''')
    assert "Base" in txt
    assert "Derived" in txt


def test_def_with_typename(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {}
def Sphere "S" {}
def Mesh "M" {}
''')
    assert 'def Xform "X"' in txt
    assert 'def Sphere "S"' in txt
    assert 'def Mesh "M"' in txt


def test_def_no_typename(tmp_path):
    """`def "Foo"` with no typeName is valid."""
    txt = _rt(tmp_path, '''#usda 1.0
def "Untyped" {
    custom int n = 1
}
''')
    assert "Untyped" in txt


def test_over_nested(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
over "Root" {
    over "Child" {
        custom int n = 7
    }
}
''')
    assert "Root" in txt
    assert "Child" in txt
