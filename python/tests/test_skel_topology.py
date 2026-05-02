"""Skeleton topology: jointNames, bindTransforms, restTransforms."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_skeleton_joints_and_names(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Skeleton "Skel" {
    uniform token[] joints = ["root", "root/spine", "root/spine/head"]
    uniform token[] jointNames = ["root", "spine", "head"]
}
''')
    assert "joints" in txt
    assert "jointNames" in txt
    assert '"root/spine/head"' in txt


def test_skeleton_bind_and_rest_transforms(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Skeleton "Skel" {
    uniform token[] joints = ["root"]
    uniform matrix4d[] bindTransforms = [
        ((1,0,0,0), (0,1,0,0), (0,0,1,0), (0,0,0,1))
    ]
    uniform matrix4d[] restTransforms = [
        ((1,0,0,0), (0,1,0,0), (0,0,1,0), (0,0,0,1))
    ]
}
''')
    assert "bindTransforms" in txt
    assert "restTransforms" in txt


def test_skel_animation_translations_and_rotations(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def SkelAnimation "Anim" {
    uniform token[] joints = ["root"]
    float3[] translations = [(0, 0, 0)]
    quatf[] rotations = [(1, 0, 0, 0)]
    half3[] scales = [(1, 1, 1)]
}
''')
    assert "translations" in txt
    assert "rotations" in txt
    assert "scales" in txt


def test_skel_root_with_skeleton_rel_usda_only(tmp_path):
    """USDA->USDA preserves skel:skeleton rel; USDC drops the
    namespaced rel (same gap as collection:* rels)."""
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def SkelRoot "Char" {
    def Skeleton "Skel" {
        uniform token[] joints = ["root"]
    }
    def Mesh "Body" (
        apiSchemas = ["SkelBindingAPI"]
    ) {
        rel skel:skeleton = </Char/Skel>
    }
}
''')
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = tinyusdz.load(str(out)).export_to_string()
    assert "SkelRoot" in txt
    assert "skel:skeleton" in txt
    assert "</Char/Skel>" in txt


def test_skel_joint_indices_and_weights(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Mesh "Body" (
    apiSchemas = ["SkelBindingAPI"]
) {
    int[] primvars:skel:jointIndices = [0, 1, 0, 1] (
        elementSize = 2
        interpolation = "vertex"
    )
    float[] primvars:skel:jointWeights = [1.0, 0.0, 0.5, 0.5] (
        elementSize = 2
        interpolation = "vertex"
    )
}
''')
    assert "skel:jointIndices" in txt
    assert "skel:jointWeights" in txt
