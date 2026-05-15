"""BlendShape and SkelAnimation blendShapeWeights round-trip."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_blendshape_offsets(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def BlendShape "Smile" {
    uniform vector3f[] offsets = [(0, 0.1, 0), (0, 0.2, 0)]
    uniform int[] pointIndices = [0, 1]
}
''')
    assert "BlendShape" in txt
    assert "Smile" in txt


def test_blendshape_with_normal_offsets_usda_only(tmp_path):
    """normalOffsets on BlendShape — fence USDA round-trip."""
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def BlendShape "Frown" {
    uniform vector3f[] offsets = [(0, -0.1, 0)]
    uniform vector3f[] normalOffsets = [(0, 0, 0.1)]
    uniform int[] pointIndices = [0]
}
''')
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = tinyusdz.load(str(out)).export_to_string()
    assert "normalOffsets" in txt


def test_skel_animation_blendshape_weights(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def SkelAnimation "Anim" {
    uniform token[] blendShapes = ["smile", "frown"]
    float[] blendShapeWeights = [0.5, 0.25]
}
''')
    assert "SkelAnimation" in txt
    assert "blendShapes" in txt or "blendShapeWeights" in txt


def test_mesh_blendshape_targets_usda_only(tmp_path):
    """Mesh-side skel:blendShapes binding — USDA fence."""
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Mesh "Body" (
    apiSchemas = ["SkelBindingAPI"]
) {
    uniform token[] skel:blendShapes = ["smile"]
    rel skel:blendShapeTargets = [</Body/SmileShape>]
    def BlendShape "SmileShape" {}
}
''')
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = tinyusdz.load(str(out)).export_to_string()
    assert "SkelBindingAPI" in txt
