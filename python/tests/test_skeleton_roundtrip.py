"""Skeleton + SkelAnimation + SkelRoot round-trip — joint hierarchy,
bind/rest transforms, animation channels.
"""
import tinyusdz


_SKEL_USDA = '''#usda 1.0
def SkelRoot "rig"
{
    def Skeleton "skel"
    {
        uniform token[] joints = ["root", "root/spine", "root/spine/head"]
        uniform matrix4d[] bindTransforms = [
            ((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1)),
            ((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 1, 0, 1)),
            ((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 2, 0, 1))
        ]
        uniform matrix4d[] restTransforms = [
            ((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1)),
            ((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 1, 0, 1)),
            ((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 2, 0, 1))
        ]
    }

    def SkelAnimation "anim"
    {
        uniform token[] joints = ["root", "root/spine"]
        float3[] translations.timeSamples = {
            0: [(0, 0, 0), (0, 1, 0)],
            10: [(0, 0, 1), (0, 1, 1)]
        }
    }
}
'''


def _rt(tmp_path, fmt):
    src = tmp_path / "x.usda"
    src.write_text(_SKEL_USDA)
    s = tinyusdz.load(str(src))
    out = tmp_path / f"x.{fmt}"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_skeleton_joint_hierarchy_roundtrip(tmp_path):
    txt = _rt(tmp_path, "usdc")
    assert "Skeleton" in txt
    assert '"root"' in txt
    assert '"root/spine"' in txt
    assert '"root/spine/head"' in txt


def test_skeleton_bind_and_rest_transforms_roundtrip(tmp_path):
    txt = _rt(tmp_path, "usdc")
    assert "bindTransforms" in txt
    assert "restTransforms" in txt
    # Translation column for joint 1 = (0, 1, 0)
    assert "(0, 1, 0, 1)" in txt
    # Translation column for joint 2 = (0, 2, 0)
    assert "(0, 2, 0, 1)" in txt


def test_skelroot_wraps_skeleton(tmp_path):
    txt = _rt(tmp_path, "usdc")
    assert "SkelRoot" in txt
    assert "Skeleton" in txt


def test_skelanimation_translations_roundtrip(tmp_path):
    txt = _rt(tmp_path, "usdc")
    assert "SkelAnimation" in txt
    assert "translations.timeSamples" in txt
    assert "0:" in txt
    assert "10:" in txt
