"""
TinyUSDZ Complete Python Bindings

Enhanced Python ctypes bindings for the TinyUSDZ C99 API with complete
function coverage including mesh, transform, material, and animation operations.

Run with: python3 tinyusdz_complete.py <usd_file>
"""

import ctypes
import ctypes.util
from pathlib import Path
from typing import Optional, Tuple, List, Union
import sys
import numpy as np
from dataclasses import dataclass

# ============================================================================
# Load C Library
# ============================================================================

def _find_library():
    """Find the TinyUSDZ C library"""
    names = [
        "tinyusdz_c", "libtinyusdz_c", "libtinyusdz_c.so",
        "libtinyusdz_c.so.1", "libtinyusdz_c.dylib", "tinyusdz_c.dll"
    ]

    for name in names:
        lib = ctypes.util.find_library(name)
        if lib:
            return lib

    local_paths = [
        Path(__file__).parent / "libtinyusdz_c.so",
        Path(__file__).parent / "build" / "libtinyusdz_c.so",
        Path(__file__).parent.parent.parent / "build" / "libtinyusdz_c.so",
    ]

    for path in local_paths:
        if path.exists():
            return str(path)

    return None

_lib_path = _find_library()
if _lib_path is None:
    raise RuntimeError("Cannot find libtinyusdz_c")

_lib = ctypes.CDLL(_lib_path)

# ============================================================================
# Result & Type Codes
# ============================================================================

class Result:
    SUCCESS = 0
    ERROR_FILE_NOT_FOUND = -1
    ERROR_PARSE_FAILED = -2
    ERROR_OUT_OF_MEMORY = -3
    ERROR_INVALID_ARGUMENT = -4
    ERROR_NOT_SUPPORTED = -5
    ERROR_COMPOSITION_FAILED = -6
    ERROR_INVALID_FORMAT = -7
    ERROR_IO_ERROR = -8
    ERROR_INTERNAL = -99

    @staticmethod
    def to_string(result: int) -> str:
        _lib.tusdz_result_to_string.restype = ctypes.c_char_p
        return _lib.tusdz_result_to_string(result).decode('utf-8')

class Format:
    AUTO = 0
    USDA = 1
    USDC = 2
    USDZ = 3

class PrimType:
    UNKNOWN = 0
    XFORM = 1
    MESH = 2
    MATERIAL = 3
    SHADER = 4
    CAMERA = 5
    DISTANT_LIGHT = 6
    SPHERE_LIGHT = 7
    RECT_LIGHT = 8
    DISK_LIGHT = 9
    CYLINDER_LIGHT = 10
    DOME_LIGHT = 11
    SKELETON = 12
    SKELROOT = 13
    SKELANIMATION = 14
    SCOPE = 15
    GEOMSUBSET = 16
    SPHERE = 17
    CUBE = 18
    CYLINDER = 19
    CAPSULE = 20
    CONE = 21

    @staticmethod
    def to_string(prim_type: int) -> str:
        _lib.tusdz_prim_type_to_string.restype = ctypes.c_char_p
        return _lib.tusdz_prim_type_to_string(prim_type).decode('utf-8')

class ValueType:
    NONE = 0
    BOOL = 1
    INT = 2
    UINT = 3
    FLOAT = 5
    DOUBLE = 6
    STRING = 7
    FLOAT2 = 13
    FLOAT3 = 14
    FLOAT4 = 15
    DOUBLE2 = 16
    DOUBLE3 = 17
    DOUBLE4 = 18
    MATRIX3D = 22
    MATRIX4D = 23
    QUATF = 24
    QUATD = 25
    COLOR3F = 26
    NORMAL3F = 29
    POINT3F = 31
    TEXCOORD2F = 33
    ARRAY = 41
    TIME_SAMPLES = 43

    @staticmethod
    def to_string(value_type: int) -> str:
        _lib.tusdz_value_type_to_string.restype = ctypes.c_char_p
        return _lib.tusdz_value_type_to_string(value_type).decode('utf-8')

class LoadOptions(ctypes.Structure):
    _fields_ = [
        ("max_memory_limit_mb", ctypes.c_size_t),
        ("max_depth", ctypes.c_int),
        ("enable_composition", ctypes.c_int),
        ("strict_mode", ctypes.c_int),
        ("structure_only", ctypes.c_int),
        ("asset_resolver", ctypes.c_void_p),
        ("asset_resolver_data", ctypes.c_void_p),
    ]

# ============================================================================
# Data Classes for Results
# ============================================================================

@dataclass
class MeshData:
    """Mesh geometry data"""
    points: Optional[np.ndarray] = None
    indices: Optional[np.ndarray] = None
    face_counts: Optional[np.ndarray] = None
    normals: Optional[np.ndarray] = None
    uvs: Optional[np.ndarray] = None
    vertex_count: int = 0
    face_count: int = 0
    index_count: int = 0

@dataclass
class Transform:
    """4x4 transformation matrix (column-major)"""
    matrix: np.ndarray  # 4x4 matrix

# ============================================================================
# Wrapper Classes
# ============================================================================

class ValueWrapper:
    """Wrapper for USD Value"""

    def __init__(self, value_handle):
        self._handle = value_handle

    @property
    def value_type(self) -> int:
        _lib.tusdz_value_get_type.restype = ctypes.c_int
        _lib.tusdz_value_get_type.argtypes = [ctypes.c_void_p]
        return _lib.tusdz_value_get_type(self._handle)

    @property
    def type_name(self) -> str:
        return ValueType.to_string(self.value_type)

    @property
    def is_array(self) -> bool:
        _lib.tusdz_value_is_array.restype = ctypes.c_int
        return bool(_lib.tusdz_value_is_array(self._handle))

    @property
    def array_size(self) -> int:
        _lib.tusdz_value_get_array_size.restype = ctypes.c_size_t
        return _lib.tusdz_value_get_array_size(self._handle)

    def get_bool(self) -> Optional[bool]:
        value = ctypes.c_int()
        _lib.tusdz_value_get_bool.restype = ctypes.c_int
        if _lib.tusdz_value_get_bool(self._handle, ctypes.byref(value)) == Result.SUCCESS:
            return bool(value.value)
        return None

    def get_int(self) -> Optional[int]:
        value = ctypes.c_int()
        _lib.tusdz_value_get_int.restype = ctypes.c_int
        if _lib.tusdz_value_get_int(self._handle, ctypes.byref(value)) == Result.SUCCESS:
            return int(value.value)
        return None

    def get_float(self) -> Optional[float]:
        value = ctypes.c_float()
        _lib.tusdz_value_get_float.restype = ctypes.c_int
        if _lib.tusdz_value_get_float(self._handle, ctypes.byref(value)) == Result.SUCCESS:
            return float(value.value)
        return None

    def get_double(self) -> Optional[float]:
        value = ctypes.c_double()
        _lib.tusdz_value_get_double.restype = ctypes.c_int
        if _lib.tusdz_value_get_double(self._handle, ctypes.byref(value)) == Result.SUCCESS:
            return float(value.value)
        return None

    def get_string(self) -> Optional[str]:
        value = ctypes.c_char_p()
        _lib.tusdz_value_get_string.restype = ctypes.c_int
        if _lib.tusdz_value_get_string(self._handle, ctypes.byref(value)) == Result.SUCCESS:
            return value.value.decode('utf-8') if value.value else None
        return None

    def get_float2(self) -> Optional[Tuple[float, float]]:
        values = (ctypes.c_float * 2)()
        _lib.tusdz_value_get_float2.restype = ctypes.c_int
        if _lib.tusdz_value_get_float2(self._handle, values) == Result.SUCCESS:
            return tuple(float(v) for v in values)
        return None

    def get_float3(self) -> Optional[Tuple[float, float, float]]:
        values = (ctypes.c_float * 3)()
        _lib.tusdz_value_get_float3.restype = ctypes.c_int
        if _lib.tusdz_value_get_float3(self._handle, values) == Result.SUCCESS:
            return tuple(float(v) for v in values)
        return None

    def get_float4(self) -> Optional[Tuple[float, float, float, float]]:
        values = (ctypes.c_float * 4)()
        _lib.tusdz_value_get_float4.restype = ctypes.c_int
        if _lib.tusdz_value_get_float4(self._handle, values) == Result.SUCCESS:
            return tuple(float(v) for v in values)
        return None

    def get_matrix4d(self) -> Optional[np.ndarray]:
        values = (ctypes.c_double * 16)()
        _lib.tusdz_value_get_matrix4d.restype = ctypes.c_int
        if _lib.tusdz_value_get_matrix4d(self._handle, values) == Result.SUCCESS:
            return np.array(values, dtype=np.float64).reshape(4, 4)
        return None

    def is_animated(self) -> bool:
        _lib.tusdz_value_is_animated.restype = ctypes.c_int
        return bool(_lib.tusdz_value_is_animated(self._handle))

    def get_time_samples(self) -> Optional[Tuple[List[float], int]]:
        """Get time samples for animated value"""
        times_ptr = ctypes.POINTER(ctypes.c_double)()
        count = ctypes.c_size_t()
        _lib.tusdz_value_get_time_samples.restype = ctypes.c_int
        if _lib.tusdz_value_get_time_samples(self._handle, ctypes.byref(times_ptr), ctypes.byref(count)) == Result.SUCCESS:
            if times_ptr and count.value > 0:
                return ([float(times_ptr[i]) for i in range(count.value)], count.value)
        return None

    def __del__(self):
        if self._handle:
            _lib.tusdz_value_free(self._handle)

class PrimWrapper:
    """Wrapper for USD Prim"""

    def __init__(self, prim_handle, stage=None):
        self._handle = prim_handle
        self._stage = stage

    @property
    def name(self) -> str:
        _lib.tusdz_prim_get_name.restype = ctypes.c_char_p
        name = _lib.tusdz_prim_get_name(self._handle)
        return name.decode('utf-8') if name else ""

    @property
    def path(self) -> str:
        _lib.tusdz_prim_get_path.restype = ctypes.c_char_p
        path = _lib.tusdz_prim_get_path(self._handle)
        return path.decode('utf-8') if path else ""

    @property
    def prim_type(self) -> int:
        _lib.tusdz_prim_get_type.restype = ctypes.c_int
        return _lib.tusdz_prim_get_type(self._handle)

    @property
    def type_name(self) -> str:
        _lib.tusdz_prim_get_type_name.restype = ctypes.c_char_p
        name = _lib.tusdz_prim_get_type_name(self._handle)
        return name.decode('utf-8') if name else "Unknown"

    def is_type(self, prim_type: int) -> bool:
        _lib.tusdz_prim_is_type.restype = ctypes.c_int
        return bool(_lib.tusdz_prim_is_type(self._handle, prim_type))

    def is_mesh(self) -> bool:
        return self.is_type(PrimType.MESH)

    def is_xform(self) -> bool:
        return self.is_type(PrimType.XFORM)

    @property
    def child_count(self) -> int:
        _lib.tusdz_prim_get_child_count.restype = ctypes.c_size_t
        return _lib.tusdz_prim_get_child_count(self._handle)

    def get_child(self, index: int) -> Optional['PrimWrapper']:
        _lib.tusdz_prim_get_child_at.restype = ctypes.c_void_p
        child = _lib.tusdz_prim_get_child_at(self._handle, index)
        return PrimWrapper(child, self._stage) if child else None

    def get_children(self) -> List['PrimWrapper']:
        return [self.get_child(i) for i in range(self.child_count)]

    @property
    def property_count(self) -> int:
        _lib.tusdz_prim_get_property_count.restype = ctypes.c_size_t
        return _lib.tusdz_prim_get_property_count(self._handle)

    def get_property_name(self, index: int) -> str:
        _lib.tusdz_prim_get_property_name_at.restype = ctypes.c_char_p
        name = _lib.tusdz_prim_get_property_name_at(self._handle, index)
        return name.decode('utf-8') if name else ""

    def get_property(self, name: str) -> Optional[ValueWrapper]:
        _lib.tusdz_prim_get_property.restype = ctypes.c_void_p
        value = _lib.tusdz_prim_get_property(self._handle, name.encode('utf-8'))
        return ValueWrapper(value) if value else None

    # ---- MESH OPERATIONS ----

    def get_mesh_data(self) -> Optional[MeshData]:
        """Extract all mesh data at once"""
        if not self.is_mesh():
            return None

        mesh_data = MeshData()

        # Points
        points_ptr = ctypes.POINTER(ctypes.c_float)()
        point_count = ctypes.c_size_t()
        _lib.tusdz_mesh_get_points.restype = ctypes.c_int
        if _lib.tusdz_mesh_get_points(self._handle, ctypes.byref(points_ptr), ctypes.byref(point_count)) == Result.SUCCESS:
            if point_count.value > 0:
                mesh_data.points = np.ctypeslib.as_array(points_ptr, shape=(point_count.value,)).copy()
                mesh_data.vertex_count = point_count.value // 3

        # Face counts
        counts_ptr = ctypes.POINTER(ctypes.c_int)()
        count_count = ctypes.c_size_t()
        _lib.tusdz_mesh_get_face_counts.restype = ctypes.c_int
        if _lib.tusdz_mesh_get_face_counts(self._handle, ctypes.byref(counts_ptr), ctypes.byref(count_count)) == Result.SUCCESS:
            if count_count.value > 0:
                mesh_data.face_counts = np.ctypeslib.as_array(counts_ptr, shape=(count_count.value,)).copy()
                mesh_data.face_count = count_count.value

        # Indices
        indices_ptr = ctypes.POINTER(ctypes.c_int)()
        index_count = ctypes.c_size_t()
        _lib.tusdz_mesh_get_indices.restype = ctypes.c_int
        if _lib.tusdz_mesh_get_indices(self._handle, ctypes.byref(indices_ptr), ctypes.byref(index_count)) == Result.SUCCESS:
            if index_count.value > 0:
                mesh_data.indices = np.ctypeslib.as_array(indices_ptr, shape=(index_count.value,)).copy()
                mesh_data.index_count = index_count.value

        # Normals
        normals_ptr = ctypes.POINTER(ctypes.c_float)()
        normal_count = ctypes.c_size_t()
        _lib.tusdz_mesh_get_normals.restype = ctypes.c_int
        if _lib.tusdz_mesh_get_normals(self._handle, ctypes.byref(normals_ptr), ctypes.byref(normal_count)) == Result.SUCCESS:
            if normal_count.value > 0:
                mesh_data.normals = np.ctypeslib.as_array(normals_ptr, shape=(normal_count.value,)).copy()

        # UVs
        uvs_ptr = ctypes.POINTER(ctypes.c_float)()
        uv_count = ctypes.c_size_t()
        _lib.tusdz_mesh_get_uvs.restype = ctypes.c_int
        if _lib.tusdz_mesh_get_uvs(self._handle, ctypes.byref(uvs_ptr), ctypes.byref(uv_count), 0) == Result.SUCCESS:
            if uv_count.value > 0:
                mesh_data.uvs = np.ctypeslib.as_array(uvs_ptr, shape=(uv_count.value,)).copy()

        return mesh_data

    def get_subdivision_scheme(self) -> Optional[str]:
        """Get mesh subdivision scheme"""
        _lib.tusdz_mesh_get_subdivision_scheme.restype = ctypes.c_char_p
        scheme = _lib.tusdz_mesh_get_subdivision_scheme(self._handle)
        return scheme.decode('utf-8') if scheme else None

    # ---- TRANSFORM OPERATIONS ----

    def get_local_matrix(self, time: float = 0.0) -> Optional[Transform]:
        """Get local transformation matrix"""
        if not self.is_xform():
            return None

        matrix = (ctypes.c_double * 16)()
        _lib.tusdz_xform_get_local_matrix.restype = ctypes.c_int
        if _lib.tusdz_xform_get_local_matrix(self._handle, time, matrix) == Result.SUCCESS:
            mat_array = np.array(matrix, dtype=np.float64).reshape(4, 4)
            return Transform(matrix=mat_array)
        return None

    def get_world_matrix(self, time: float = 0.0) -> Optional[Transform]:
        """Get world transformation matrix"""
        matrix = (ctypes.c_double * 16)()
        _lib.tusdz_prim_get_world_matrix.restype = ctypes.c_int
        if _lib.tusdz_prim_get_world_matrix(self._handle, time, matrix) == Result.SUCCESS:
            mat_array = np.array(matrix, dtype=np.float64).reshape(4, 4)
            return Transform(matrix=mat_array)
        return None

    # ---- MATERIAL OPERATIONS ----

    def get_bound_material(self) -> Optional['PrimWrapper']:
        """Get material bound to this prim"""
        _lib.tusdz_prim_get_bound_material.restype = ctypes.c_void_p
        mat = _lib.tusdz_prim_get_bound_material(self._handle)
        return PrimWrapper(mat, self._stage) if mat else None

    def get_surface_shader(self) -> Optional['PrimWrapper']:
        """Get surface shader (for Material prims)"""
        _lib.tusdz_material_get_surface_shader.restype = ctypes.c_void_p
        shader = _lib.tusdz_material_get_surface_shader(self._handle)
        return PrimWrapper(shader, self._stage) if shader else None

    def get_shader_input(self, name: str) -> Optional[ValueWrapper]:
        """Get shader input (for Shader prims)"""
        _lib.tusdz_shader_get_input.restype = ctypes.c_void_p
        value = _lib.tusdz_shader_get_input(self._handle, name.encode('utf-8'))
        return ValueWrapper(value) if value else None

    def get_shader_type(self) -> Optional[str]:
        """Get shader type ID"""
        _lib.tusdz_shader_get_type_id.restype = ctypes.c_char_p
        type_id = _lib.tusdz_shader_get_type_id(self._handle)
        return type_id.decode('utf-8') if type_id else None

    def print_hierarchy(self, max_depth: int = -1):
        """Print hierarchy to stdout"""
        _lib.tusdz_stage_print_hierarchy.argtypes = [ctypes.c_void_p, ctypes.c_int]
        _lib.tusdz_stage_print_hierarchy(self._handle, max_depth)

class StageWrapper:
    """Wrapper for USD Stage"""

    def __init__(self, stage_handle):
        self._handle = stage_handle

    @property
    def root_prim(self) -> PrimWrapper:
        _lib.tusdz_stage_get_root_prim.restype = ctypes.c_void_p
        root = _lib.tusdz_stage_get_root_prim(self._handle)
        return PrimWrapper(root, self) if root else None

    def get_prim_at_path(self, path: str) -> Optional[PrimWrapper]:
        _lib.tusdz_stage_get_prim_at_path.restype = ctypes.c_void_p
        prim = _lib.tusdz_stage_get_prim_at_path(self._handle, path.encode('utf-8'))
        return PrimWrapper(prim, self) if prim else None

    @property
    def has_animation(self) -> bool:
        _lib.tusdz_stage_has_animation.restype = ctypes.c_int
        return bool(_lib.tusdz_stage_has_animation(self._handle))

    def get_time_range(self) -> Optional[Tuple[float, float, float]]:
        """Get time range (start, end, fps)"""
        start = ctypes.c_double()
        end = ctypes.c_double()
        fps = ctypes.c_double()
        _lib.tusdz_stage_get_time_range.restype = ctypes.c_int
        if _lib.tusdz_stage_get_time_range(self._handle, ctypes.byref(start), ctypes.byref(end), ctypes.byref(fps)) == Result.SUCCESS:
            return (float(start.value), float(end.value), float(fps.value))
        return None

    def get_memory_stats(self) -> Tuple[int, int]:
        """Get memory usage (bytes_used, bytes_peak)"""
        used = ctypes.c_size_t()
        peak = ctypes.c_size_t()
        _lib.tusdz_get_memory_stats.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_size_t)]
        _lib.tusdz_get_memory_stats(self._handle, ctypes.byref(used), ctypes.byref(peak))
        return (used.value, peak.value)

    def __del__(self):
        if self._handle:
            _lib.tusdz_stage_free(self._handle)

# ============================================================================
# Global Functions
# ============================================================================

def init() -> bool:
    """Initialize TinyUSDZ library"""
    _lib.tusdz_init.restype = ctypes.c_int
    return _lib.tusdz_init() == Result.SUCCESS

def shutdown():
    """Shutdown TinyUSDZ library"""
    _lib.tusdz_shutdown()

def get_version() -> str:
    """Get TinyUSDZ version"""
    _lib.tusdz_get_version.restype = ctypes.c_char_p
    version = _lib.tusdz_get_version()
    return version.decode('utf-8') if version else "unknown"

def load_from_file(filepath: str, options: Optional[LoadOptions] = None) -> Optional[StageWrapper]:
    """Load USD from file"""
    error_buf = ctypes.create_string_buffer(1024)
    stage = ctypes.c_void_p()

    _lib.tusdz_load_from_file.restype = ctypes.c_int
    result = _lib.tusdz_load_from_file(
        filepath.encode('utf-8'),
        ctypes.byref(options) if options else None,
        ctypes.byref(stage),
        error_buf,
        len(error_buf),
    )

    if result != Result.SUCCESS:
        error_msg = error_buf.value.decode('utf-8') if error_buf.value else "Unknown error"
        raise RuntimeError(f"Failed to load USD: {error_msg}")

    return StageWrapper(stage.value) if stage.value else None

def load_from_memory(data: bytes, format: int = Format.AUTO) -> Optional[StageWrapper]:
    """Load USD from memory"""
    error_buf = ctypes.create_string_buffer(1024)
    stage = ctypes.c_void_p()

    _lib.tusdz_load_from_memory.restype = ctypes.c_int
    result = _lib.tusdz_load_from_memory(
        ctypes.c_char_p(data),
        len(data),
        format,
        None,
        ctypes.byref(stage),
        error_buf,
        len(error_buf),
    )

    if result != Result.SUCCESS:
        error_msg = error_buf.value.decode('utf-8') if error_buf.value else "Unknown error"
        raise RuntimeError(f"Failed to load USD: {error_msg}")

    return StageWrapper(stage.value) if stage.value else None

def detect_format(filepath: str) -> int:
    """Detect USD file format"""
    _lib.tusdz_detect_format.restype = ctypes.c_int
    return _lib.tusdz_detect_format(filepath.encode('utf-8'))

# ============================================================================
# Auto-initialization
# ============================================================================

def _auto_init():
    try:
        init()
    except Exception:
        pass

_auto_init()

import atexit
atexit.register(shutdown)