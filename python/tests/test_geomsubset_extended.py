"""GeomSubset round-trip — material binding partitioning of meshes."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


_MESH_WITH_SUBSETS = '''#usda 1.0
def Mesh "M"
{
    point3f[] points = [(0,0,0), (1,0,0), (1,1,0), (0,1,0),
                        (2,0,0), (3,0,0), (3,1,0), (2,1,0)]
    int[] faceVertexCounts = [4, 4]
    int[] faceVertexIndices = [0, 1, 2, 3, 4, 5, 6, 7]

    def GeomSubset "front"
    {
        uniform token elementType = "face"
        uniform token familyName = "materialBind"
        int[] indices = [0]
        rel material:binding = </Mat1>
    }

    def GeomSubset "back"
    {
        uniform token elementType = "face"
        uniform token familyName = "materialBind"
        int[] indices = [1]
        rel material:binding = </Mat2>
    }
}
def Material "Mat1" {}
def Material "Mat2" {}
'''


def test_geomsubset_under_mesh_roundtrip(tmp_path):
    txt = _rt(tmp_path, _MESH_WITH_SUBSETS)
    assert "GeomSubset" in txt
    assert '"front"' in txt or "front" in txt
    assert '"back"' in txt or "back" in txt


def test_geomsubset_indices_preserved(tmp_path):
    txt = _rt(tmp_path, _MESH_WITH_SUBSETS)
    assert "[0]" in txt
    assert "[1]" in txt


def test_geomsubset_material_binding_preserved(tmp_path):
    txt = _rt(tmp_path, _MESH_WITH_SUBSETS)
    assert "</Mat1>" in txt
    assert "</Mat2>" in txt


def test_geomsubset_familyname_uniform(tmp_path):
    txt = _rt(tmp_path, _MESH_WITH_SUBSETS)
    assert 'familyName = "materialBind"' in txt
    assert 'elementType = "face"' in txt
