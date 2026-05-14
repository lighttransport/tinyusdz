"""assetInfo prim metadata round-trip."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_assetinfo_identifier_and_name(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "Asset" (
    assetInfo = {
        asset identifier = @./model.usda@
        string name = "MyModel"
    }
) {}
''')
    assert "assetInfo" in txt
    assert "@./model.usda@" in txt
    assert '"MyModel"' in txt


def test_assetinfo_version(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "Asset" (
    assetInfo = {
        string version = "1.2.3"
    }
) {}
''')
    assert '"1.2.3"' in txt


def test_assetinfo_payloadassetdependencies_usda_only(tmp_path):
    """payloadAssetDependencies survives USDA->USDA round-trip."""
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "Asset" (
    assetInfo = {
        asset[] payloadAssetDependencies = [@./a.usda@, @./b.usda@]
    }
) {}
''')
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = tinyusdz.load(str(out)).export_to_string()
    assert "payloadAssetDependencies" in txt
    assert "@./a.usda@" in txt
