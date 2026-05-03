"""Multi-target attribute `.connect = [</a>, </b>, ...]` parser."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_two_target_array(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Material "M" {
    def Shader "Mix" {
        float inputs:weight.connect = [</M/A.outputs:result>, </M/B.outputs:result>]
        token outputs:result
    }
    def Shader "A" { float outputs:result }
    def Shader "B" { float outputs:result }
}
''')
    assert "</M/A.outputs:result>" in txt
    assert "</M/B.outputs:result>" in txt


def test_three_target_array(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Material "M" {
    def Shader "Out" {
        float inputs:v.connect = [</M/A.outputs:r>, </M/B.outputs:r>, </M/C.outputs:r>]
    }
    def Shader "A" { float outputs:r }
    def Shader "B" { float outputs:r }
    def Shader "C" { float outputs:r }
}
''')
    for n in ("A", "B", "C"):
        assert f"</M/{n}.outputs:r>" in txt


def test_single_target_still_works(tmp_path):
    """Single-path form (no array brackets) — fence regression."""
    txt = _rt(tmp_path, '''#usda 1.0
def Material "M" {
    def Shader "S" {
        float inputs:v.connect = </M/A.outputs:r>
    }
    def Shader "A" { float outputs:r }
}
''')
    assert "</M/A.outputs:r>" in txt


def test_python_get_returns_all_targets(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Material "M" {
    def Shader "Mix" {
        float inputs:w.connect = [</M/A.outputs:r>, </M/B.outputs:r>]
    }
    def Shader "A" { float outputs:r }
    def Shader "B" { float outputs:r }
}
''')
    s = tinyusdz.load(str(src))
    p = s.get_prim_at_path("/M/Mix")
    conns = p.get_attribute_connections("inputs:w")
    assert conns is not None
    assert len(conns) == 2
    assert any("A.outputs:r" in c for c in conns)
    assert any("B.outputs:r" in c for c in conns)
