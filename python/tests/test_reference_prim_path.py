"""References/payloads with explicit prim_path selection."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_reference_with_prim_path(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "Local" (
    references = @./other.usda@</Asset/Sub>
) {}
''')
    assert "@./other.usda@" in txt
    assert "</Asset/Sub>" in txt


def test_payload_with_prim_path(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "Local" (
    payload = @./big.usda@</Asset/Sub>
) {}
''')
    assert "@./big.usda@" in txt


def test_internal_reference_no_asset(tmp_path):
    """Internal reference: only prim path, no asset."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "Source" {
    custom int n = 7
}
def Xform "Target" (
    references = </Source>
) {}
''')
    assert "</Source>" in txt


def test_reference_list_multiple(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" (
    references = [@./a.usda@, @./b.usda@</B>]
) {}
''')
    assert "@./a.usda@" in txt
    assert "@./b.usda@" in txt


def test_prepend_payload(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" (
    prepend payload = @./asset.usda@
) {}
''')
    assert "@./asset.usda@" in txt


def test_reference_with_offset_and_scale_usda_only(tmp_path):
    """references with `(offset = N; scale = M)` — USDA fence."""
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" (
    references = @./asset.usda@</Foo> (offset = 10; scale = 2)
) {}
''')
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = tinyusdz.load(str(out)).export_to_string()
    assert "@./asset.usda@" in txt
    assert "offset" in txt or "scale" in txt
