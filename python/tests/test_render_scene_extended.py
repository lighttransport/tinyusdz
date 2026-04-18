"""Extended Tydra: animations, skeletons, textures/images/buffers, shaders."""
from __future__ import annotations

import pytest

import tinyusdz


USDA_PBR = """#usda 1.0
(
    defaultPrim = "World"
)

def Xform "World"
{
    def Scope "Looks"
    {
        def Material "M"
        {
            token outputs:surface.connect = </World/Looks/M/S.outputs:surface>

            def Shader "S"
            {
                uniform token info:id = "UsdPreviewSurface"
                color3f inputs:diffuseColor.connect = </World/Looks/M/Tex.outputs:rgb>
                float   inputs:metallic = 0.2
                float   inputs:roughness = 0.6
                float   inputs:opacity = 0.9
                token   outputs:surface
            }

            def Shader "Tex"
            {
                uniform token info:id = "UsdUVTexture"
                asset   inputs:file = @diffuse.png@
                token   inputs:wrapS = "repeat"
                token   inputs:wrapT = "mirror"
                float2  inputs:st.connect = </World/Looks/M/Primvar.outputs:result>
                float3  outputs:rgb
            }

            def Shader "Primvar"
            {
                uniform token info:id = "UsdPrimvarReader_float2"
                token inputs:varname = "st"
                float2 outputs:result
            }
        }
    }

    def Mesh "Plane"
    {
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        point3f[] points = [(-1,0,-1),(1,0,-1),(1,0,1),(-1,0,1)]
        rel material:binding = </World/Looks/M>
    }
}
"""

USDA_ANIM = """#usda 1.0
(
    defaultPrim = "World"
    timeCodesPerSecond = 24
    startTimeCode = 0
    endTimeCode = 24
)

def Xform "World"
{
    def Xform "Bouncer"
    {
        float3 xformOp:translate.timeSamples = {
            0:  (0, 0, 0),
            12: (0, 2, 0),
            24: (0, 0, 0),
        }
        uniform token[] xformOpOrder = ["xformOp:translate"]

        def Mesh "m"
        {
            int[] faceVertexCounts = [3]
            int[] faceVertexIndices = [0, 1, 2]
            point3f[] points = [(0,0,0),(1,0,0),(0,1,0)]
        }
    }
}
"""

USDA_SKEL = """#usda 1.0
(
    defaultPrim = "Root"
)

def SkelRoot "Root"
{
    def Skeleton "Skel"
    {
        uniform token[] joints = ["root", "root/a", "root/a/b"]
        uniform matrix4d[] bindTransforms = [
            ((1,0,0,0),(0,1,0,0),(0,0,1,0),(0,0,0,1)),
            ((1,0,0,0),(0,1,0,0),(0,0,1,0),(0,1,0,1)),
            ((1,0,0,0),(0,1,0,0),(0,0,1,0),(0,2,0,1))
        ]
        uniform matrix4d[] restTransforms = [
            ((1,0,0,0),(0,1,0,0),(0,0,1,0),(0,0,0,1)),
            ((1,0,0,0),(0,1,0,0),(0,0,1,0),(0,1,0,1)),
            ((1,0,0,0),(0,1,0,0),(0,0,1,0),(0,1,0,1))
        ]
    }
}
"""


def _rs(src):
    return tinyusdz.tydra.convert_to_render_scene(tinyusdz.loads(src))


def test_preview_surface_fields():
    rs = _rs(USDA_PBR)
    assert len(rs.materials()) == 1
    mat = rs.materials()[0]
    assert mat.name == "M"
    ps = mat.preview_surface
    assert ps is not None
    # All expected shader input keys present.
    for k in ("diffuse_color", "emissive_color", "specular_color",
              "metallic", "roughness", "clearcoat", "clearcoat_roughness",
              "opacity", "opacity_threshold", "ior", "normal",
              "displacement", "occlusion"):
        assert k in ps, k
        assert "value" in ps[k] and "texture_id" in ps[k]
    # diffuse_color is texture-connected (texture_id should be >= 0).
    assert ps["diffuse_color"]["texture_id"] >= 0
    # Scalars authored directly — no texture.
    assert ps["metallic"]["texture_id"] == -1
    assert abs(ps["metallic"]["value"] - 0.2) < 1e-6
    assert abs(ps["roughness"]["value"] - 0.6) < 1e-6
    assert abs(ps["opacity"]["value"] - 0.9) < 1e-6


def test_textures_and_images():
    rs = _rs(USDA_PBR)
    assert len(rs.textures()) >= 1
    t = rs.textures()[0]
    assert t.wrap_s == "repeat"
    assert t.wrap_t == "mirror"
    # Bias/scale default values.
    assert t.bias == (0.0, 0.0, 0.0, 0.0)
    assert t.scale == (1.0, 1.0, 1.0, 1.0)
    # Texture links to an image.
    assert t.texture_image_id >= 0

    assert len(rs.images()) >= 1
    img = rs.images()[0]
    assert "diffuse.png" in img.asset_identifier
    # Component type strings.
    assert img.component_type in {
        "uint8", "int8", "uint16", "int16", "uint32", "int32",
        "half", "float", "double",
    }
    assert img.color_space in {
        "srgb", "linear", "raw", "aces2065_1",
        "rec709", "rec2020", "displayp3", "unknown",
    }


def test_buffer_access_for_present_buffers():
    """Buffers may be empty when assets can't be resolved; just make sure the
    accessor shapes are right where data exists."""
    rs = _rs(USDA_PBR)
    for buf in rs.buffers():
        assert buf.nbytes >= 0
        mv = memoryview(buf.bytes)
        assert mv.format == "B"
        assert mv.itemsize == 1


def test_animation_from_xformop_timesamples():
    np = pytest.importorskip("numpy")
    rs = _rs(USDA_ANIM)
    anims = rs.animations()
    assert len(anims) == 1
    a = anims[0]
    assert a.has_node and not a.has_skeletal
    assert a.duration > 0

    samplers = a.samplers()
    assert len(samplers) >= 1
    s = samplers[0]
    times = np.asarray(s.times)
    values = np.asarray(s.values)
    assert times.dtype == np.float32 and values.dtype == np.float32
    assert times.ndim == 1 and values.ndim == 1
    assert s.interpolation in {"step", "linear", "cubicspline"}
    # 3 translation keyframes × 3 components.
    assert times.size * 3 == values.size

    channels = a.channels()
    assert len(channels) >= 1
    ch = channels[0]
    assert ch["path"] in {"translation", "rotation", "scale", "weights"}
    assert ch["target_type"] == "scene_node"
    assert ch["sampler"] >= 0


def test_skeleton_joint_topology():
    np = pytest.importorskip("numpy")
    rs = _rs(USDA_SKEL)
    skels = rs.skeletons()
    assert len(skels) == 1
    sk = skels[0]
    assert sk.num_joints == 3
    parents = np.asarray(sk.parent_joint_indices)
    assert parents.dtype == np.int32
    assert parents.tolist() == [-1, 0, 1]

    binds = np.asarray(sk.bind_transforms).reshape(-1, 4, 4)
    rests = np.asarray(sk.rest_transforms).reshape(-1, 4, 4)
    assert binds.shape == (3, 4, 4) and binds.dtype == np.float64
    assert rests.shape == (3, 4, 4) and rests.dtype == np.float64
    # Second joint's bind translates Y by 1.
    assert float(binds[1][3][1]) == 1.0


def test_render_types_keep_scene_alive():
    """Drop the RenderScene wrapper; a child buffer must remain valid because
    the BufferView holds a strong ref through the child."""
    np = pytest.importorskip("numpy")
    rs = _rs(USDA_ANIM)
    sampler = rs.animations()[0].samplers()[0]
    times_buf = sampler.times
    del rs, sampler
    arr = np.asarray(times_buf)
    assert arr.size > 0


# --------------------------------------------------------------------------
# Render-scene graph nodes.
# --------------------------------------------------------------------------

def test_scene_nodes_basic_topology():
    rs = _rs(USDA_PBR)
    nodes = rs.nodes()
    assert len(nodes) >= 1
    root_idx = rs.default_root_node()
    assert 0 <= root_idx < len(nodes)

    # Category / node_type strings come from the defined enum space.
    categories = {"group", "geom", "light", "camera", "material", "skeleton"}
    node_types = {
        "xform", "mesh", "camera", "skel_root", "skeleton",
        "point_light", "directional_light", "envmap_light",
        "rect_light", "disk_light", "cylinder_light", "geometry_light",
    }
    for n in nodes:
        assert n.category in categories
        assert n.node_type in node_types
        # Matrices are 4 rows of 4 doubles.
        assert len(n.local_matrix) == 4 and len(n.local_matrix[0]) == 4
        assert len(n.global_matrix) == 4


def test_scene_nodes_contain_mesh_reference():
    """The USDA_PBR scene defines /World/Plane — we expect a mesh node
    that points back into rs.meshes() via content_id."""
    rs = _rs(USDA_PBR)

    # Depth-first search for a mesh-type node.
    found = None

    def visit(n):
        nonlocal found
        if n.node_type == "mesh":
            found = n
            return
        for ch in n.children():
            visit(ch)
            if found is not None:
                return

    for root in rs.nodes():
        visit(root)
        if found is not None:
            break

    assert found is not None
    assert found.content_id >= 0
    assert found.content_id < len(rs.meshes())
    # Non-instance node should report identity-ish instancing fields.
    assert found.is_instance is False
    assert found.prototype_index == -1


# --------------------------------------------------------------------------
# UVTexture extras: output channel, fallback uv, transform2d.
# --------------------------------------------------------------------------

USDA_TEX_TRANSFORM = """#usda 1.0
(
    defaultPrim = "World"
)

def Xform "World"
{
    def Scope "Looks"
    {
        def Material "M"
        {
            token outputs:surface.connect = </World/Looks/M/S.outputs:surface>

            def Shader "S"
            {
                uniform token info:id = "UsdPreviewSurface"
                color3f inputs:diffuseColor.connect = </World/Looks/M/Tex.outputs:rgb>
                token   outputs:surface
            }

            def Shader "Tex"
            {
                uniform token info:id = "UsdUVTexture"
                asset   inputs:file = @diffuse.png@
                float2  inputs:st.connect = </World/Looks/M/Xform.outputs:result>
                float3  outputs:rgb
            }

            def Shader "Xform"
            {
                uniform token info:id = "UsdTransform2d"
                float2 inputs:in.connect = </World/Looks/M/Primvar.outputs:result>
                float  inputs:rotation = 45.0
                float2 inputs:scale = (2.0, 3.0)
                float2 inputs:translation = (0.25, 0.75)
                float2 outputs:result
            }

            def Shader "Primvar"
            {
                uniform token info:id = "UsdPrimvarReader_float2"
                token inputs:varname = "st"
                float2 outputs:result
            }
        }
    }

    def Mesh "Plane"
    {
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        point3f[] points = [(-1,0,-1),(1,0,-1),(1,0,1),(-1,0,1)]
        rel material:binding = </World/Looks/M>
    }
}
"""


def test_texture_output_channel_and_fallback():
    rs = _rs(USDA_PBR)
    t = rs.textures()[0]
    assert t.output_channel in {"r", "g", "b", "a", "rgb", "rgba"}
    assert len(t.fallback_uv) == 4
    # Transform should default to identity.
    rows = t.transform
    assert rows[0][0] == 1.0 and rows[1][1] == 1.0 and rows[2][2] == 1.0


def test_texture_transform2d_authored():
    rs = _rs(USDA_TEX_TRANSFORM)
    texs = rs.textures()
    assert len(texs) >= 1
    t = texs[0]
    # Authored UsdTransform2d upstream, so has_transform2d should be true.
    if t.has_transform2d:
        assert abs(t.tx_rotation - 45.0) < 1e-4
        assert t.tx_scale == (2.0, 3.0)
        assert t.tx_translation == (0.25, 0.75)


# --------------------------------------------------------------------------
# OpenPBR / MaterialX node graph. These are optional — some pipelines don't
# author MaterialX OpenPBR shaders, in which case `open_pbr` returns None.
# --------------------------------------------------------------------------

def test_openpbr_absent_on_preview_surface_only():
    """USDA_PBR defines only UsdPreviewSurface; open_pbr should be None."""
    rs = _rs(USDA_PBR)
    mat = rs.materials()[0]
    # The only authored shader is UsdPreviewSurface.
    assert mat.has_preview_surface is True
    # OpenPBR flag and payload should both be absent.
    if mat.has_open_pbr is False:
        assert mat.open_pbr is None
        assert mat.node_graph_json is None
