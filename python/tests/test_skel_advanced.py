"""Skeleton/SkelRoot advanced cases."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_skel_root_basic(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def SkelRoot "Char" {
    def Skeleton "Skel" {
        uniform token[] joints = ["Root", "Root/Spine"]
    }
}
''')
    assert "SkelRoot" in txt
    assert "Skeleton" in txt
    assert '"Root"' in txt


def test_skel_animation_separate_prim(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def SkelAnimation "Anim" {
    uniform token[] joints = ["Root"]
    float3[] translations = [(0, 0, 0)]
    quatf[] rotations = [(1, 0, 0, 0)]
    half3[] scales = [(1, 1, 1)]
}
''')
    assert "SkelAnimation" in txt


def test_skel_animation_with_multiple_joints(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def SkelAnimation "Anim" {
    uniform token[] joints = ["a", "b", "c"]
    float3[] translations = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
}
''')
    assert "translations" in txt
    assert "(1, 0, 0)" in txt


def test_skel_animation_blendshape_targets(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def SkelAnimation "Anim" {
    uniform token[] blendShapes = ["smile", "blink"]
    float[] blendShapeWeights = [0.7, 0.2]
}
''')
    assert "blendShapes" in txt
    assert "blendShapeWeights" in txt
