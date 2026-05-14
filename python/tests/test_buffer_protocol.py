"""Buffer protocol on Value and BufferView."""
import numpy as np
import tinyusdz


def test_value_array_buffer_float():
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("v", [1.0, 2.0, 3.0, 4.0], dtype="float[]")
    s.add_root_prim(p)
    val = p.get_attribute("v").value
    out = np.asarray(val)
    assert out.dtype == np.float32
    assert out.tolist() == [1.0, 2.0, 3.0, 4.0]


def test_value_array_buffer_double():
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("v", [0.5, 1.5, 2.5], dtype="double[]")
    s.add_root_prim(p)
    val = p.get_attribute("v").value
    out = np.asarray(val)
    assert out.dtype == np.float64
    assert out.tolist() == [0.5, 1.5, 2.5]


def test_value_array_buffer_int(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("v", [10, 20, 30, 40], dtype="int[]")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    val = s2.get_prim_at_path("/X").get_attribute("v").value
    arr = np.asarray(val)
    assert arr.tolist() == [10, 20, 30, 40]


def test_render_mesh_points_dtype():
    s = tinyusdz.loads('''#usda 1.0
def Mesh "M" {
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0, 1, 2]
    point3f[] points = [(0,0,0), (1,0,0), (0,1,0)]
}
''')
    rs = tinyusdz.tydra.convert_to_render_scene(s)
    pts = np.asarray(rs.meshes()[0].points)
    assert pts.dtype == np.float32


def test_value_buffer_round_trip_after_save(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("v", list(range(10)), dtype="int[]")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    val = s2.get_prim_at_path("/X").get_attribute("v").value
    assert np.asarray(val).tolist() == list(range(10))
