"""GeomSubset family metadata: familyName, familyType, elementType."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_geomsubset_face(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Mesh "M" {
    int[] faceVertexCounts = [4, 4]
    int[] faceVertexIndices = [0, 1, 2, 3, 4, 5, 6, 7]
    point3f[] points = [(0,0,0), (1,0,0), (1,1,0), (0,1,0),
                        (2,0,0), (3,0,0), (3,1,0), (2,1,0)]
    def GeomSubset "Group1" {
        uniform token elementType = "face"
        int[] indices = [0]
    }
}
''')
    assert "GeomSubset" in txt
    assert "elementType" in txt
    assert "face" in txt


def test_geomsubset_with_familyname(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Mesh "M" {
    int[] faceVertexCounts = [4, 4]
    int[] faceVertexIndices = [0, 1, 2, 3, 4, 5, 6, 7]
    point3f[] points = [(0,0,0), (1,0,0), (1,1,0), (0,1,0),
                        (2,0,0), (3,0,0), (3,1,0), (2,1,0)]
    def GeomSubset "MatA" {
        uniform token elementType = "face"
        uniform token familyName = "materialBind"
        int[] indices = [0]
    }
    def GeomSubset "MatB" {
        uniform token elementType = "face"
        uniform token familyName = "materialBind"
        int[] indices = [1]
    }
}
''')
    assert '"materialBind"' in txt
    assert "MatA" in txt
    assert "MatB" in txt


def test_subset_familytype_partition_usda_only(tmp_path):
    """familyType = "partition" stored on parent Mesh — USDA fence."""
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Mesh "M" {
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0, 1, 2, 3]
    point3f[] points = [(0,0,0), (1,0,0), (1,1,0), (0,1,0)]
    uniform token subsetFamily:materialBind:familyType = "partition"
    def GeomSubset "S1" {
        uniform token elementType = "face"
        uniform token familyName = "materialBind"
        int[] indices = [0]
    }
}
''')
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = tinyusdz.load(str(out)).export_to_string()
    assert "subsetFamily" in txt
    assert "partition" in txt
