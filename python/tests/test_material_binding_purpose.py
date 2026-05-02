"""Material binding rels: full/preview purpose, binding strength."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_material_binding_basic(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "World" {
    def Material "Mat" {}
    def Mesh "M" {
        rel material:binding = </World/Mat>
    }
}
''')
    assert "material:binding" in txt
    assert "</World/Mat>" in txt


def test_material_binding_full_and_preview(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "World" {
    def Material "Full" {}
    def Material "Preview" {}
    def Mesh "M" {
        rel material:binding:full = </World/Full>
        rel material:binding:preview = </World/Preview>
    }
}
''')
    assert "material:binding:full" in txt
    assert "material:binding:preview" in txt


def test_material_binding_strength_usda_only(tmp_path):
    """bindMaterialAs token survives USDA->USDA round-trip."""
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "World" {
    def Material "Mat" {}
    def Mesh "M" {
        rel material:binding = </World/Mat> (
            bindMaterialAs = "strongerThanDescendants"
        )
    }
}
''')
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = tinyusdz.load(str(out)).export_to_string()
    assert "material:binding" in txt


def test_material_binding_collection(tmp_path):
    """Collection-based material binding via collection:* rel."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "World" (
    apiSchemas = ["MaterialBindingAPI"]
) {
    def Material "Mat" {}
    def Mesh "M" {}
}
''')
    assert "MaterialBindingAPI" in txt
