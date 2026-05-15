"""tinyusdz.tydra.visit_prims and list_prims_by_type."""
import tinyusdz


def test_list_prims_by_type_xform(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "A" {}
def Xform "B" {}
def Sphere "S" {}
''')
    s = tinyusdz.load(str(src))
    results = tinyusdz.tydra.list_prims_by_type(s, "Xform")
    paths = {r[1] for r in results}
    assert paths == {"/A", "/B"}


def test_list_prims_by_type_sphere(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Sphere "Ball" {}
def Cube "Box" {}
def Sphere "Marble" {}
''')
    s = tinyusdz.load(str(src))
    results = tinyusdz.tydra.list_prims_by_type(s, "Sphere")
    paths = {r[1] for r in results}
    assert paths == {"/Ball", "/Marble"}


def test_list_prims_by_type_none_match(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" {}
''')
    s = tinyusdz.load(str(src))
    paths = tinyusdz.tydra.list_prims_by_type(s, "Mesh")
    assert paths == []


def test_tydra_visit_prims(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "Root" {
    def Sphere "S" {}
    def Cube "C" {}
}
''')
    s = tinyusdz.load(str(src))
    visited = []

    def cb(prim, path, depth):
        visited.append(path)
        return True

    tinyusdz.tydra.visit_prims(s, cb)
    assert "/Root" in visited
    assert "/Root/S" in visited
    assert "/Root/C" in visited
