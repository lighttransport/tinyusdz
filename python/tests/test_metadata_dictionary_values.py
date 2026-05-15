"""Dictionary-typed metadata: customData, assetInfo, customLayerData."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_customdata_nested_dict(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" (
    customData = {
        dictionary user = {
            string author = "alice"
            int frame = 42
            dictionary nested = {
                bool active = true
            }
        }
    }
) {}
''')
    assert "customData" in txt
    assert '"alice"' in txt
    assert "42" in txt


def test_customdata_with_string_array(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" (
    customData = {
        string[] tags = ["red", "blue"]
    }
) {}
''')
    assert "customData" in txt
    assert '"red"' in txt and '"blue"' in txt


def test_customdata_on_attribute(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom int n = 5 (
        customData = {
            string note = "the answer"
        }
    )
}
''')
    assert "the answer" in txt


def test_customdata_with_multiple_types(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" (
    customData = {
        bool flag = true
        int count = 10
        float weight = 0.5
        double scale = 2.0
        string label = "demo"
    }
) {}
''')
    assert "flag" in txt
    assert "count" in txt
    assert "weight" in txt
