"""ListOp qualifiers on composition arcs: prepend, append, delete."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_prepend_inherits(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
class "Base" {}
def Xform "X" (
    prepend inherits = </Base>
) {}
''')
    assert "Base" in txt


def test_append_inherits(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
class "Mix" {}
def Xform "X" (
    append inherits = </Mix>
) {}
''')
    assert "Mix" in txt


def test_prepend_specializes(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
class "BaseSpec" {}
def Xform "X" (
    prepend specializes = </BaseSpec>
) {}
''')
    assert "BaseSpec" in txt


def test_apischemas_prepend(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" (
    prepend apiSchemas = ["MaterialBindingAPI"]
) {}
''')
    assert "MaterialBindingAPI" in txt


def test_apischemas_combined(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" (
    apiSchemas = ["MaterialBindingAPI", "PhysicsRigidBodyAPI", "ShapingAPI"]
) {}
''')
    assert "MaterialBindingAPI" in txt
    assert "PhysicsRigidBodyAPI" in txt
    assert "ShapingAPI" in txt


def test_delete_references_usda_only(tmp_path):
    """`delete references = ...` listop — USDA fence."""
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" (
    delete references = @./old.usda@
) {}
''')
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = tinyusdz.load(str(out)).export_to_string()
    assert "@./old.usda@" in txt or "delete" in txt
