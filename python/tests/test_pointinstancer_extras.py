"""PointInstancer additional attributes."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_pointinstancer_proto_indices(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def PointInstancer "PI" {
    point3f[] positions = [(0,0,0), (1,0,0), (2,0,0)]
    int[] protoIndices = [0, 1, 0]
    rel prototypes = [</PI/Proto1>, </PI/Proto2>]
    def Sphere "Proto1" {}
    def Cube "Proto2" {}
}
''')
    assert "PointInstancer" in txt
    assert "protoIndices" in txt


def test_pointinstancer_velocities(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def PointInstancer "PI" {
    point3f[] positions = [(0,0,0), (1,0,0)]
    vector3f[] velocities = [(0,1,0), (0,2,0)]
    int[] protoIndices = [0, 0]
    rel prototypes = [</PI/Proto>]
    def Sphere "Proto" {}
}
''')
    assert "velocities" in txt


def test_pointinstancer_orientations(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def PointInstancer "PI" {
    point3f[] positions = [(0,0,0)]
    quath[] orientations = [(1, 0, 0, 0)]
    int[] protoIndices = [0]
    rel prototypes = [</PI/Proto>]
    def Sphere "Proto" {}
}
''')
    assert "orientations" in txt


def test_pointinstancer_scales(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def PointInstancer "PI" {
    point3f[] positions = [(0,0,0), (1,0,0)]
    float3[] scales = [(1, 1, 1), (2, 2, 2)]
    int[] protoIndices = [0, 0]
    rel prototypes = [</PI/Proto>]
    def Sphere "Proto" {}
}
''')
    assert "scales" in txt
    assert "(2, 2, 2)" in txt


def test_pointinstancer_invisibleids_usda_only(tmp_path):
    """invisibleIds — USDA fence."""
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def PointInstancer "PI" {
    point3f[] positions = [(0,0,0), (1,0,0), (2,0,0)]
    int[] protoIndices = [0, 0, 0]
    int64[] invisibleIds = [1]
    rel prototypes = [</PI/Proto>]
    def Sphere "Proto" {}
}
''')
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = tinyusdz.load(str(out)).export_to_string()
    assert "invisibleIds" in txt
