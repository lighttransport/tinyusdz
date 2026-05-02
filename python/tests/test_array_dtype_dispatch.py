"""Array dtype dispatch fence: int64[] / uint64[] / bool[] / half[].

Phase C.8: confirm each routes to its proper array constructor (not a
fallback float[]/int[]) and round-trips through USDA + USDC.
"""
import pytest
import tinyusdz


FORMATS = ["usda", "usdc"]


def _roundtrip(stage, fmt, tmp_path):
    out = tmp_path / f"x.{fmt}"
    stage.save(str(out))
    return tinyusdz.load(str(out))


@pytest.mark.parametrize("fmt", FORMATS)
@pytest.mark.parametrize("dtype,values", [
    ("int64[]", [1, 2, 3, -9999999999]),
    ("uint64[]", [10, 20, 12345678901234]),
    ("bool[]", [True, False, True, False]),
    ("half[]", [0.5, 1.5, 2.5]),
])
def test_array_dtype_roundtrip(tmp_path, fmt, dtype, values):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("a", values, dtype=dtype)
    s.add_root_prim(p)
    s2 = _roundtrip(s, fmt, tmp_path)
    txt = s2.export_to_string()
    assert dtype + " a " in txt or dtype + " a=" in txt or dtype + " a=" in txt.replace(" ", ""), txt
    # Robust check: dtype prefix appears.
    assert dtype + " a" in txt, txt
