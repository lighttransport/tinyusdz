"""Type stubs for the tinyusdz._core CPython extension."""
from __future__ import annotations

from typing import Callable, List, Optional

class UsdError(Exception): ...
class UsdParseError(UsdError): ...
class UsdIoError(UsdError): ...

class Value:
    type_name: str
    is_array: bool
    def to_string(self) -> str: ...
    def as_scalar(self) -> bool | int | float | str | None: ...
    def __buffer__(self, flags: int) -> memoryview: ...  # PEP 688
    def __repr__(self) -> str: ...

class Attribute:
    name: str
    type_name: str
    value: Optional[Value]
    def __repr__(self) -> str: ...

class Prim:
    type_name: Optional[str]
    element_name: Optional[str]
    name: Optional[str]
    def __init__(self, type_name: str, name: Optional[str] = None) -> None: ...
    def children(self) -> List["Prim"]: ...
    def to_string(self) -> str: ...
    def get_attribute(self, name: str) -> Optional[Attribute]: ...
    def property_names(self) -> List[str]: ...
    def add_child(self, prim: "Prim") -> None: ...
    def set_element_name(self, name: str) -> None: ...
    def set_attribute(self, name: str, value: object,
                      dtype: Optional[str] = None) -> None:
        """Author an attribute on this prim.

        Default dtype is inferred from the Python value:
          int / bool                   -> int / bool
          float                        -> float
          str                          -> string
          list[int]                    -> int[]
          list[float]                  -> float[]
          tuple[int]   length 2/3/4    -> int2/int3/int4
          tuple[float] length 2/3/4    -> float2/float3/float4

        Explicit dtype overrides:
          "double"                     -> double
          "double2"/"double3"/"double4"-> double-precision packed vector
          "color3f"/"point3f"/"normal3f"/"vector3f" — typed float3 alias
          "color3d"/"point3d"/"normal3d"/"vector3d" — typed double3 alias
          "matrix2d"/"matrix3d"/"matrix4d" -- nested NxN tuple/list of floats
          "token"/"token[]"            -> single/array of tokens
          "asset"/"asset[]"            -> asset path / array of asset paths
          "string[]"                   -> array of strings
        """
        ...
    def set_metadata(self, name: str, value: str | bool) -> None: ...
    def get_metadata(self, name: str) -> Optional[str | bool]: ...
    def apply_api_schema(self, schema_name: str,
                         instance_name: Optional[str] = None) -> None: ...
    def api_schemas(self) -> List[str]: ...
    def add_relationship(self, name: str,
                         targets: str | List[str]) -> None: ...
    def get_relationship_targets(self, name: str) -> Optional[List[str]]: ...
    def add_attribute_connection(
        self, name: str, targets: str | List[str],
        dtype: Optional[str] = None,
    ) -> None: ...
    def get_attribute_connections(self, name: str) -> Optional[List[str]]: ...
    def set_attribute_metadata(
        self, attr_name: str, key: str, value: str | bool,
    ) -> None: ...
    def get_attribute_metadata(
        self, attr_name: str, key: str,
    ) -> Optional[str]: ...
    def set_attribute_at_time(
        self, name: str, time: float, value: object,
        dtype: Optional[str] = None,
    ) -> None: ...
    def get_attribute_timesamples(
        self, name: str,
    ) -> List[tuple[float, object]]: ...
    def add_reference(
        self, asset_path: str, prim_path: Optional[str] = None,
        offset: float = 0.0, scale: float = 1.0,
        qualifier: str = "prepend",
    ) -> None: ...
    def add_payload(
        self, asset_path: str, prim_path: Optional[str] = None,
        offset: float = 0.0, scale: float = 1.0,
        qualifier: str = "prepend",
    ) -> None: ...
    def add_inherit(
        self, prim_path: str, qualifier: str = "prepend",
    ) -> None: ...
    def add_specialize(
        self, prim_path: str, qualifier: str = "prepend",
    ) -> None: ...
    def clear_references(self) -> None: ...
    def clear_payload(self) -> None: ...
    def clear_inherits(self) -> None: ...
    def clear_specializes(self) -> None: ...
    def add_variant_set_name(
        self, name: str, qualifier: str = "prepend",
    ) -> None: ...
    def clear_variant_set_names(self) -> None: ...
    def set_variant_selection(
        self, variant_set_name: str, variant_name: str,
    ) -> None: ...
    def clear_variant_selection(
        self, variant_set_name: Optional[str] = None,
    ) -> None: ...
    def define_variant(
        self, variant_set_name: str, variant_name: str,
    ) -> None: ...
    def variant_add_child(
        self, variant_set_name: str, variant_name: str, child: "Prim",
    ) -> None: ...
    def variant_set_attribute(
        self, variant_set_name: str, variant_name: str,
        attr_name: str, value: object, dtype: Optional[str] = None,
    ) -> None: ...
    def __repr__(self) -> str: ...

class Stage:
    def __init__(self) -> None: ...
    def export_to_string(self) -> str: ...
    def save(self, path: str, format: Optional[str] = None) -> None: ...
    def get_prim_at_path(self, path: str) -> Optional[Prim]: ...
    def root_prims(self) -> List[Prim]: ...
    def visit_prims(self, callback: Callable[[Prim, str, int], bool]) -> None: ...
    def add_root_prim(self, prim: Prim) -> None: ...
    def set_metadata(self, key: str, value: str | float | int) -> None: ...
    def get_metadata(self, key: str) -> Optional[str | float]: ...
    def set_up_axis(self, axis: str) -> None: ...
    def set_meters_per_unit(self, value: float) -> None: ...
    def set_time_codes_per_second(self, value: float) -> None: ...
    def set_frames_per_second(self, value: float) -> None: ...
    def set_start_time_code(self, value: float) -> None: ...
    def set_end_time_code(self, value: float) -> None: ...
    def __repr__(self) -> str: ...

def load(path: str, format: Optional[str] = None) -> Stage: ...
def loads(usda_text: str) -> Stage: ...
def load_bytes(data: bytes, format: Optional[str] = None) -> Stage: ...
def is_usd(path: str) -> bool: ...
def detect_format(path: str) -> str: ...

class BufferView:
    def __buffer__(self, flags: int) -> memoryview: ...
    def __repr__(self) -> str: ...

class RenderMesh:
    name: str
    abs_path: str
    display_name: str
    points: BufferView
    face_vertex_indices: BufferView
    face_vertex_counts: BufferView
    normals: Optional[BufferView]
    display_color: tuple[float, float, float]
    material_id: int
    is_right_handed: bool
    is_double_sided: bool
    def texcoord(self, slot_id: int = 0) -> Optional[BufferView]: ...
    def texcoord_slot_ids(self) -> List[int]: ...

class RenderCamera:
    name: str
    abs_path: str
    znear: float
    zfar: float
    focal_length: float
    horizontal_aperture: float
    vertical_aperture: float
    projection: str  # "perspective" | "orthographic"

class RenderLight:
    name: str
    abs_path: str
    type: str  # "point" | "sphere" | "disk" | "rect" | ...
    intensity: float
    color: tuple[float, float, float]

class RenderTexture:
    name: str
    abs_path: str
    varname_uv: str
    wrap_s: str  # "clamp_to_edge" | "repeat" | "mirror" | "clamp_to_border"
    wrap_t: str
    texture_image_id: int
    bias: tuple[float, float, float, float]
    scale: tuple[float, float, float, float]
    output_channel: str  # "r" | "g" | "b" | "a" | "rgb" | "rgba"
    fallback_uv: tuple[float, float, float, float]
    has_transform2d: bool
    tx_rotation: float
    tx_scale: tuple[float, float]
    tx_translation: tuple[float, float]
    transform: tuple[
        tuple[float, float, float],
        tuple[float, float, float],
        tuple[float, float, float],
    ]

class RenderImage:
    asset_identifier: str
    width: int
    height: int
    channels: int
    miplevel: int
    buffer_id: int
    color_space: str    # "srgb" | "linear" | "raw" | ...
    is_decoded: bool
    component_type: str # "uint8" | "float" | ...

class RenderBuffer:
    component_type: str
    bytes: BufferView
    nbytes: int

class AnimationSampler:
    times: BufferView       # float32[N]
    values: BufferView      # float32[M]
    interpolation: str       # "step" | "linear" | "cubicspline"

class RenderAnimation:
    name: str
    abs_path: str
    duration: float
    has_skeletal: bool
    has_node: bool
    def samplers(self) -> List[AnimationSampler]: ...
    def channels(self) -> List[dict]: ...

class RenderSkeleton:
    name: str
    abs_path: str
    num_joints: int
    default_anim_id: int
    parent_joint_indices: BufferView    # int32[N]
    bind_transforms: BufferView         # float64[N, 16]
    rest_transforms: BufferView         # float64[N, 16]

class RenderMaterial:
    name: str
    abs_path: str
    has_preview_surface: bool
    has_open_pbr: bool
    preview_surface: Optional[dict]  # keys map to {"value", "texture_id"}
    open_pbr: Optional[dict]         # OpenPBR inputs; plain-float keys for
                                     # tangent_rotation, normal_map_scale, etc.
    node_graph_json: Optional[str]   # MaterialX node graph JSON, or None

class RenderNode:
    name: str
    abs_path: str
    display_name: str
    category: str   # "group" | "geom" | "light" | "camera" | "material" | "skeleton"
    node_type: str  # "xform" | "mesh" | "camera" | "skel_root" | ...
    content_id: int
    is_instance: bool
    prototype_index: int
    instance_id: int
    has_reset_xform: bool
    local_matrix: tuple[
        tuple[float, float, float, float],
        tuple[float, float, float, float],
        tuple[float, float, float, float],
        tuple[float, float, float, float],
    ]
    global_matrix: tuple[
        tuple[float, float, float, float],
        tuple[float, float, float, float],
        tuple[float, float, float, float],
        tuple[float, float, float, float],
    ]
    def children(self) -> List["RenderNode"]: ...

class RenderScene:
    def meshes(self)     -> List[RenderMesh]: ...
    def materials(self)  -> List[RenderMaterial]: ...
    def cameras(self)    -> List[RenderCamera]: ...
    def lights(self)     -> List[RenderLight]: ...
    def textures(self)   -> List[RenderTexture]: ...
    def images(self)     -> List[RenderImage]: ...
    def buffers(self)    -> List[RenderBuffer]: ...
    def animations(self) -> List[RenderAnimation]: ...
    def skeletons(self)  -> List[RenderSkeleton]: ...
    def nodes(self)      -> List[RenderNode]: ...
    def default_root_node(self) -> int: ...

class _Tydra:
    def list_prims_by_type(
        self, stage: Stage, type_name: Optional[str] = None
    ) -> List[tuple[Prim, str, int]]: ...
    def visit_prims(
        self, stage: Stage, callback: Callable[[Prim, str, int], bool]
    ) -> None: ...
    def convert_to_render_scene(self, stage: Stage) -> RenderScene: ...

tydra: _Tydra
