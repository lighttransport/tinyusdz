"""Cross-format chain round-trip stress tests.

USDA -> USDC -> USDA, USDC -> USDA -> USDC, and USDA -> USDZ -> USDA
chains must all preserve content.
"""
import zipfile

import tinyusdz


_USDA_FIXTURE = '''#usda 1.0
(
    defaultPrim = "Root"
    upAxis = "Y"
    metersPerUnit = 0.01
)
def Xform "Root" (
    kind = "component"
)
{
    custom int frob = 7
    custom float scale = 1.5
    custom string label = "hello"

    def Mesh "M"
    {
        point3f[] points = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
        int[] faceVertexIndices = [0, 1, 2]
        int[] faceVertexCounts = [3]
        normal3f[] normals = [(0, 0, 1), (0, 0, 1), (0, 0, 1)] (
            interpolation = "faceVarying"
        )
    }

    def Material "Mat"
    {
        custom token info:id = "UsdPreviewSurface"
    }
}
'''


def _content_features():
    """Substrings whose presence we check after each round-trip step.
    Keeping the list narrow avoids whitespace-format flakiness."""
    return [
        'defaultPrim = "Root"',
        'upAxis = "Y"',
        "frob = 7",
        "scale = 1.5",
        '"hello"',
        "(0, 0, 0)",
        "(1, 0, 0)",
        "(0, 1, 0)",
        "faceVarying",
        "UsdPreviewSurface",
    ]


def test_usda_to_usdc_to_usda(tmp_path):
    src = tmp_path / "src.usda"
    src.write_text(_USDA_FIXTURE)
    s1 = tinyusdz.load(str(src))
    out = tmp_path / "mid.usdc"
    s1.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    for feat in _content_features():
        assert feat in txt, f"{feat!r} lost after USDA->USDC->USDA"


def test_usdc_to_usda_to_usdc(tmp_path):
    src = tmp_path / "src.usda"
    src.write_text(_USDA_FIXTURE)
    s1 = tinyusdz.load(str(src))
    mid_usdc = tmp_path / "first.usdc"
    s1.save(str(mid_usdc))

    s2 = tinyusdz.load(str(mid_usdc))
    mid_usda = tmp_path / "mid.usda"
    s2.save(str(mid_usda))

    s3 = tinyusdz.load(str(mid_usda))
    final_usdc = tmp_path / "final.usdc"
    s3.save(str(final_usdc))

    s4 = tinyusdz.load(str(final_usdc))
    txt = s4.export_to_string()
    for feat in _content_features():
        assert feat in txt, f"{feat!r} lost after USDC->USDA->USDC"


def test_three_step_chain(tmp_path):
    """USDA -> USDC -> USDA -> USDC -> USDA — content survives all
    five reads and four writes."""
    text = _USDA_FIXTURE
    for i in range(2):
        s = tinyusdz.loads(text)
        usdc = tmp_path / f"step{i}.usdc"
        s.save(str(usdc))
        s = tinyusdz.load(str(usdc))
        text = s.export_to_string()
    for feat in _content_features():
        assert feat in text


def test_usdz_pack_then_load(tmp_path):
    """USDA -> USDZ (no extra assets) -> USDA preserves features."""
    s1 = tinyusdz.loads(_USDA_FIXTURE)
    out = tmp_path / "x.usdz"
    s1.save(str(out))
    # Sanity: it's a valid ZIP
    with zipfile.ZipFile(out, "r") as z:
        assert z.namelist()[0] in ("root.usdc", "root.usda")
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    for feat in _content_features():
        assert feat in txt


def test_usdz_with_assets_roundtrip(tmp_path):
    """USDA -> USDZ + textures -> USDA preserves the in-USD asset path."""
    src = '''#usda 1.0
def Xform "X" {
    asset diffuse = @./tex.png@
}
'''
    s1 = tinyusdz.loads(src)
    PNG_HEADER = bytes.fromhex(
        "89504E470D0A1A0A0000000D49484452")
    out = tmp_path / "x.usdz"
    s1.save(str(out), assets={"tex.png": PNG_HEADER})
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert "asset diffuse = @tex.png@" in txt or "asset diffuse = @./tex.png@" in txt
