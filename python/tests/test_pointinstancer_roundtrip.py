"""GeomPointInstancer round-trip — protoIndices, positions, scales,
orientations, and the prototypes relationship.
"""
import tinyusdz


_PI_USDA = '''#usda 1.0
def Xform "scene"
{
    def "Prototypes" {
        def Cube "proto_a" {}
        def Sphere "proto_b" {}
    }

    def PointInstancer "instancer"
    {
        rel prototypes = [
            </scene/Prototypes/proto_a>,
            </scene/Prototypes/proto_b>
        ]
        int[] protoIndices = [0, 1, 0, 1, 0]
        point3f[] positions = [
            (0, 0, 0), (2, 0, 0), (4, 0, 0), (6, 0, 0), (8, 0, 0)
        ]
        float3[] scales = [
            (1, 1, 1), (2, 1, 1), (1, 2, 1), (1, 1, 2), (0.5, 0.5, 0.5)
        ]
        quath[] orientations = [
            (1, 0, 0, 0), (0.707, 0.707, 0, 0), (1, 0, 0, 0),
            (0.707, 0, 0.707, 0), (1, 0, 0, 0)
        ]
    }
}
'''


def _rt(tmp_path, fmt):
    src = tmp_path / "x.usda"
    src.write_text(_PI_USDA)
    s = tinyusdz.load(str(src))
    out = tmp_path / f"x.{fmt}"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_pointinstancer_usda_roundtrip(tmp_path):
    txt = _rt(tmp_path, "usda")
    assert "PointInstancer" in txt
    assert "protoIndices" in txt
    assert "[0, 1, 0, 1, 0]" in txt
    assert "(2, 0, 0)" in txt


def test_pointinstancer_usdc_roundtrip(tmp_path):
    txt = _rt(tmp_path, "usdc")
    assert "PointInstancer" in txt
    assert "[0, 1, 0, 1, 0]" in txt
    assert "(8, 0, 0)" in txt


def test_pointinstancer_prototypes_relationship_targets_preserved(tmp_path):
    txt = _rt(tmp_path, "usdc")
    assert "/scene/Prototypes/proto_a" in txt
    assert "/scene/Prototypes/proto_b" in txt


def test_pointinstancer_quath_orientation_components(tmp_path):
    """quath[] orientations through USDC must keep (real, imag) parsing
    in USDA print form. The first orientation is identity (1, 0, 0, 0)."""
    txt = _rt(tmp_path, "usdc")
    assert "(1, 0, 0, 0)" in txt
    # 0.707 may print as 0.707 or rounded — just ensure component
    # values appear somewhere.
    assert "0.707" in txt
