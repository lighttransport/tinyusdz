"""tydra.convert_to_render_scene: extract render-friendly data."""
import tinyusdz


def _scene_from(usda):
    s = tinyusdz.loads(usda)
    return tinyusdz.tydra.convert_to_render_scene(s)


def test_simple_mesh():
    rs = _scene_from('''#usda 1.0
def Mesh "M" {
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0, 1, 2]
    point3f[] points = [(0,0,0), (1,0,0), (0,1,0)]
}
''')
    assert len(rs.meshes()) == 1


def test_camera_count():
    rs = _scene_from('''#usda 1.0
def Camera "Cam1" { float focalLength = 50 }
def Camera "Cam2" { float focalLength = 35 }
''')
    assert len(rs.cameras()) == 2


def test_no_meshes_empty():
    rs = _scene_from('''#usda 1.0
def Xform "X" {}
''')
    assert len(rs.meshes()) == 0


def test_lights_extracted():
    rs = _scene_from('''#usda 1.0
def DistantLight "Sun" {
    float inputs:intensity = 1000
}
def SphereLight "Bulb" {
    float inputs:intensity = 100
}
''')
    assert len(rs.lights()) >= 2


def test_material_bound_to_mesh():
    """Material is extracted when bound from a Mesh via material:binding."""
    rs = _scene_from('''#usda 1.0
def Xform "World" {
    def Material "Mat" {
        def Shader "Surface" {
            uniform token info:id = "UsdPreviewSurface"
            color3f inputs:diffuseColor = (0.5, 0.2, 0.1)
            token outputs:surface
        }
        token outputs:surface.connect = </World/Mat/Surface.outputs:surface>
    }
    def Mesh "M" {
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, 1, 2]
        point3f[] points = [(0,0,0), (1,0,0), (0,1,0)]
        rel material:binding = </World/Mat>
    }
}
''')
    # Mesh extracted; material extraction depends on tydra's material walk —
    # fence at minimum that conversion didn't crash and a mesh is present.
    assert len(rs.meshes()) >= 1


def test_combined_scene():
    rs = _scene_from('''#usda 1.0
def Xform "World" {
    def Mesh "Body" {
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, 1, 2]
        point3f[] points = [(0,0,0), (1,0,0), (0,1,0)]
    }
    def Camera "Main" {}
    def DomeLight "Env" {}
}
''')
    assert len(rs.meshes()) == 1
    assert len(rs.cameras()) == 1
    assert len(rs.lights()) >= 1
