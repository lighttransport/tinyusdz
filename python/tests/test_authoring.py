# SPDX-License-Identifier: Apache-2.0
import pytest

import tinyusdz

np = pytest.importorskip("numpy")


def build_stage():
    st = tinyusdz.Stage.create()
    st.up_axis = "Z"
    st.meters_per_unit = 1.0
    world = st.define_prim("/World", "Xform")
    grid = st.define_prim("/World/Grid", "Mesh")
    pts = np.array([[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0]], np.float32)
    grid.set("points", pts, type="point3f[]")
    grid.set("faceVertexCounts", np.array([4], np.int32))
    grid.set("faceVertexIndices", np.array([0, 1, 2, 3], np.int32))
    grid.set("displayColor", [(0.8, 0.2, 0.1)], type="color3f[]")
    grid.set("radius", 2.5, custom=True)
    grid.set("purpose", "render", uniform=True)
    grid.set("xformOpOrder", ["xformOp:translate"], type="token[]",
             uniform=True)
    grid.set("xformOp:translate", (0.0, 1.0, 0.0), time=0.0)
    grid.set("xformOp:translate", (0.0, 2.0, 0.0), time=24.0)
    st.define_prim("/World/Looks/Red", "Material")
    grid.add_relationship("material:binding", ["/World/Looks/Red"])
    world.set_metadata("kind", "assembly")
    st.set_default_prim("World")
    return st, pts


def verify(st2, pts):
    g = st2.prim_at("/World/Grid")
    assert np.allclose(np.asarray(g["points"]), pts)
    assert g["radius"] == 2.5
    assert g["purpose"] == "render"
    assert g["xformOpOrder"] == ("xformOp:translate",)
    assert g.attribute("purpose").is_uniform
    assert list(g.relationship("material:binding")) == ["/World/Looks/Red"]
    assert st2.prim_at("/World").kind == "assembly"
    attr = g.attribute("xformOp:translate")
    assert attr.has_timesamples and len(attr.timesamples) == 2
    assert st2.up_axis == "Z"
    assert st2.default_prim_path == "World"


def test_author_and_usda_roundtrip():
    st, pts = build_stage()
    # `custom` is a deprecated USDA qualifier the writer omits by default,
    # so assert it on the authoring stage only.
    assert st.prim_at("/World/Grid").attribute("radius").is_custom
    st2 = tinyusdz.loads(st.export_usda())
    verify(st2, pts)


def test_author_and_usdc_roundtrip():
    st, pts = build_stage()
    st2 = tinyusdz.load_bytes(st.export_usdc())
    verify(st2, pts)


def test_define_prim_variants_roundtrip():
    st = tinyusdz.Stage.create()
    w = st.define_prim("/World", "Xform")
    w.add_variant_set("lod")
    vs = w.variant_sets["lod"]
    vs.add_variant("high")
    vs.add_variant("low")
    vs.selection = "high"
    st2 = tinyusdz.loads(st.export_usda())
    vs2 = st2.prim_at("/World").variant_sets["lod"]
    assert set(vs2.names) == {"high", "low"}
    assert vs2.selection == "high"


def test_arcs_roundtrip():
    st = tinyusdz.Stage.create()
    w = st.define_prim("/World", "Xform")
    w.add_reference("./library.usda", "/Proto")
    w.add_inherit("/_class_base")
    usda = st.export_usda()
    assert "@./library.usda@</Proto>" in usda
    assert "</_class_base>" in usda


def test_value_types():
    st = tinyusdz.Stage.create()
    p = st.define_prim("/p", "Xform")
    p.set("a_bool", True)
    p.set("a_int", 7)
    p.set("a_float", 1.5)
    p.set("a_str", "hello", type="string")
    p.set("a_token", "tok")
    p.set("a_vec3", (1.0, 2.0, 3.0))
    p.set("a_ints", np.arange(5, dtype=np.int32))
    p.set("a_doubles", np.linspace(0, 1, 3))
    p.set("a_matrix", np.eye(4), type="matrix4d")
    st2 = tinyusdz.loads(st.export_usda())
    p2 = st2.prim_at("/p")
    assert p2["a_bool"] is True
    assert p2["a_int"] == 7
    assert p2["a_float"] == 1.5
    assert p2["a_str"] == "hello"
    assert p2["a_token"] == "tok"
    assert p2["a_vec3"] == (1.0, 2.0, 3.0)
    assert np.asarray(p2["a_ints"]).tolist() == [0, 1, 2, 3, 4]
    assert np.allclose(np.asarray(p2["a_doubles"]), [0, 0.5, 1])
    # matrix scalars read back as a flat row-major 16-tuple
    m = p2["a_matrix"]
    assert len(m) == 16
    assert np.allclose(np.asarray(m).reshape(4, 4), np.eye(4))


def test_attribute_set_and_block():
    st = tinyusdz.Stage.create()
    p = st.define_prim("/p", "Xform")
    p.set("radius", 1.0)
    attr = p.attribute("radius")
    attr.set(9.0)
    assert p["radius"] == 9.0
    attr.block()
    assert p["radius"] is tinyusdz.ValueBlock
    p.remove_property("radius")
    assert "radius" not in p


def test_remove_prim_and_staleness():
    st = tinyusdz.Stage.create()
    st.define_prim("/a/b/c", "Xform")
    b = st.prim_at("/a/b")
    st.define_prim("/a/d", "Xform")  # structural edit: handle self-heals
    assert b.name == "b"
    st.remove_prim("/a/b")
    with pytest.raises(tinyusdz.StaleHandleError):
        _ = b.name
    assert "/a/b/c" not in st
    with pytest.raises(KeyError):
        st.remove_prim("/nope")


def test_metadata_and_sublayers():
    st = tinyusdz.Stage.create()
    st.set_metadata("doc", "my doc")
    st.set_metadata("startTimeCode", 1.0)
    st.set_metadata("endTimeCode", 100.0)
    st.add_sublayer("./base.usda")
    assert st.doc == "my doc"
    assert st.start_time == 1.0
    assert st.end_time == 100.0
    assert st.sublayers == ("./base.usda",)
    with pytest.raises(ValueError):
        st.set_metadata("upAxis", "Q")


def test_flattened(simple_stage):
    flat = simple_stage.flattened()
    assert flat.prim_at("/World/Quad")["radius"] == 2.5


def test_flatten_file(tmp_path, simple_stage):
    src = tmp_path / "in.usda"
    dst = tmp_path / "out.usdc"
    simple_stage.save(str(src))
    tinyusdz.flatten_file(str(src), str(dst))
    st = tinyusdz.load(dst)
    assert st.prim_at("/World/Quad")["radius"] == 2.5


def test_refcount_sanity(simple_stage):
    import sys

    q = simple_stage.prim_at("/World/Quad")
    base = sys.getrefcount(simple_stage)
    arrays = [q["points"] for _ in range(8)]
    del arrays
    prims = [p for p in simple_stage]
    del prims
    assert abs(sys.getrefcount(simple_stage) - base) <= 1


def test_variants_usdc_roundtrip():
    st = tinyusdz.Stage.create()
    w = st.define_prim("/World", "Xform")
    w.add_variant_set("lod")
    vs = w.variant_sets["lod"]
    vs.add_variant("high")
    vs.add_variant("low")
    vs.selection = "high"
    st2 = tinyusdz.load_bytes(st.export_usdc())
    vs2 = st2.prim_at("/World").variant_sets["lod"]
    assert set(vs2.names) == {"high", "low"}
    assert vs2.selection == "high"
    # second generation stays stable
    st3 = tinyusdz.load_bytes(st2.export_usdc())
    vs3 = st3.prim_at("/World").variant_sets["lod"]
    assert set(vs3.names) == {"high", "low"} and vs3.selection == "high"


VARIANT_SRC = '''#usda 1.0
def Xform "root" (
    variants = { string lod = "high" }
    prepend variantSets = ["lod"]
)
{
    variantSet "lod" = {
        "high" { float a = 1
                 def Mesh "Extra" { float b = 2 } }
        "low" { float a = 0 }
    }
}
'''


@pytest.mark.parametrize("ext", ["usda", "usdc"])
def test_variant_content_and_override(tmp_path, ext):
    st = tinyusdz.loads(VARIANT_SRC)
    fn = tmp_path / f"v.{ext}"
    st.save(str(fn))

    hi = tinyusdz.load(fn)  # composed with authored selection
    assert hi.prim_at("/root").get("a") == 1.0
    assert hi.prim_at("/root/Extra").get("b") == 2.0

    lo = tinyusdz.load(fn, variants={"lod": "low"})
    assert lo.prim_at("/root").get("a") == 0.0
    assert lo.get_prim_at("/root/Extra") is None


def test_variant_cross_format_chain(tmp_path):
    # usda -> usdc -> usda: names, selection and content all survive.
    st = tinyusdz.loads(VARIANT_SRC)
    mid = tinyusdz.load_bytes(st.export_usdc())
    final = tinyusdz.loads(mid.export_usda())
    fn = tmp_path / "chain.usda"
    final.save(str(fn))
    comp = tinyusdz.load(fn)
    assert comp.prim_at("/root").get("a") == 1.0
    assert comp.prim_at("/root/Extra").get("b") == 2.0
