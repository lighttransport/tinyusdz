"""
TinyUSDZ Improved Python Bindings

Enhanced, Pythonic bindings for the TinyUSDZ C99 API with:
  • Comprehensive type hints
  • Custom exception types
  • Context managers
  • Generator-based iteration
  • Query and search utilities
  • Better error messages
  • Batch operations
  • Logging support
  • Performance optimizations

Usage:
    >>> from tinyusdz_improved import TinyUSDZ
    >>>
    >>> with TinyUSDZ() as tz:
    ...     stage = tz.load_file("model.usd")
    ...     for prim in stage.iter_all_prims():
    ...         if prim.is_mesh:
    ...             mesh = prim.mesh_data
    ...             print(f"{prim.path}: {mesh.vertex_count} vertices")
"""

import ctypes
import ctypes.util
import logging
import warnings
from pathlib import Path
from typing import Optional, Tuple, List, Union, Iterator, Dict, Any
from dataclasses import dataclass, field
from enum import IntEnum
from contextlib import contextmanager
import sys

# ============================================================================
# Logging Setup
# ============================================================================

logger = logging.getLogger("tinyusdz")
logger.addHandler(logging.NullHandler())

# ============================================================================
# Custom Exceptions
# ============================================================================

class TinyUSDZError(Exception):
    """Base exception for TinyUSDZ errors"""
    pass

class TinyUSDZLoadError(TinyUSDZError):
    """Error loading USD file"""
    pass

class TinyUSDZTypeError(TinyUSDZError):
    """Wrong type for operation"""
    pass

class TinyUSDZValueError(TinyUSDZError):
    """Invalid value"""
    pass

class TinyUSDZNotFoundError(TinyUSDZError):
    """Prim or property not found"""
    pass

# ============================================================================
# Type Definitions with Better Names
# ============================================================================

class Format(IntEnum):
    """USD file format"""
    AUTO = 0
    USDA = 1  # ASCII
    USDC = 2  # Binary/Crate
    USDZ = 3  # Zip archive

class PrimType(IntEnum):
    """USD primitive types"""
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

class ValueType(IntEnum):
    """USD value types"""
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

# ============================================================================
# Data Structures
# ============================================================================

@dataclass
class MeshData:
    """Mesh geometry data"""
    points: Optional['np.ndarray'] = None
    indices: Optional['np.ndarray'] = None
    face_counts: Optional['np.ndarray'] = None
    normals: Optional['np.ndarray'] = None
    uvs: Optional['np.ndarray'] = None
    vertex_count: int = 0
    face_count: int = 0
    index_count: int = 0

    @property
    def is_valid(self) -> bool:
        """Check if mesh data is valid"""
        return self.points is not None and len(self.points) > 0

    @property
    def triangle_count(self) -> int:
        """Estimate triangle count (assumes triangulated or quads)"""
        if self.face_counts is None:
            return 0
        return sum(max(0, count - 2) for count in self.face_counts)

@dataclass
class Transform:
    """4x4 transformation matrix"""
    matrix: 'np.ndarray'  # 4x4 matrix

    @property
    def translation(self) -> Tuple[float, float, float]:
        """Extract translation from matrix"""
        return tuple(self.matrix[3, :3].tolist())

    @property
    def scale(self) -> Tuple[float, float, float]:
        """Extract scale from matrix (simplified)"""
        import numpy as np
        m = self.matrix[:3, :3]
        sx = np.linalg.norm(m[0, :])
        sy = np.linalg.norm(m[1, :])
        sz = np.linalg.norm(m[2, :])
        return (float(sx), float(sy), float(sz))

@dataclass
class TimeRange:
    """Animation time range"""
    start: float
    end: float
    fps: float

    @property
    def duration(self) -> float:
        """Duration in seconds"""
        return (self.end - self.start) / self.fps

    @property
    def frame_count(self) -> int:
        """Total frame count"""
        return int((self.end - self.start) * self.fps)

@dataclass
class PrimInfo:
    """Information about a prim"""
    name: str
    path: str
    type_name: str
    prim_type: PrimType
    child_count: int
    property_count: int

@dataclass
class QueryResult:
    """Result of a prim query"""
    prims: List['Prim'] = field(default_factory=list)
    count: int = 0

    def __iter__(self):
        return iter(self.prims)

    def __len__(self):
        return len(self.prims)

    def first(self) -> Optional['Prim']:
        """Get first result"""
        return self.prims[0] if self.prims else None

    def filter(self, predicate) -> 'QueryResult':
        """Filter results"""
        return QueryResult(prims=[p for p in self.prims if predicate(p)])

# ============================================================================
# Library Loading
# ============================================================================

def _find_library() -> str:
    """Find TinyUSDZ C library"""
    names = [
        "tinyusdz_c", "libtinyusdz_c", "libtinyusdz_c.so",
        "libtinyusdz_c.so.1", "libtinyusdz_c.dylib", "tinyusdz_c.dll"
    ]

    for name in names:
        lib = ctypes.util.find_library(name)
        if lib:
            logger.debug(f"Found library: {lib}")
            return lib

    local_paths = [
        Path(__file__).parent / "libtinyusdz_c.so",
        Path(__file__).parent / "build" / "libtinyusdz_c.so",
        Path(__file__).parent.parent.parent / "build" / "libtinyusdz_c.so",
    ]

    for path in local_paths:
        if path.exists():
            logger.debug(f"Found local library: {path}")
            return str(path)

    raise TinyUSDZError(
        "Cannot find libtinyusdz_c. Install the C library first or set LD_LIBRARY_PATH"
    )

_lib_path = _find_library()
_lib = ctypes.CDLL(_lib_path)

# ============================================================================
# FFI Helper
# ============================================================================

class _FFI:
    """FFI helper for cleaner code"""

    @staticmethod
    def call(func_name: str, *args, restype=None):
        """Call a C function"""
        func = getattr(_lib, func_name)
        if restype is not None:
            func.restype = restype
        return func(*args)

    @staticmethod
    def string(func_name: str, *args) -> str:
        """Call function returning C string"""
        func = getattr(_lib, func_name)
        func.restype = ctypes.c_char_p
        result = func(*args)
        return result.decode('utf-8') if result else ""

# ============================================================================
# Value Wrapper
# ============================================================================

class Value:
    """USD value wrapper with enhanced methods"""

    def __init__(self, handle: ctypes.c_void_p):
        if not handle:
            raise TinyUSDZValueError("Invalid value handle")
        self._handle = handle

    @property
    def type(self) -> ValueType:
        """Get value type"""
        result = _FFI.call("tusdz_value_get_type", self._handle, restype=ctypes.c_int)
        return ValueType(result)

    @property
    def type_name(self) -> str:
        """Get value type name"""
        return ValueType.to_string(self.type)

    @property
    def is_array(self) -> bool:
        """Check if value is array"""
        return _FFI.call("tusdz_value_is_array", self._handle, restype=ctypes.c_int) != 0

    @property
    def array_size(self) -> int:
        """Get array size"""
        return _FFI.call("tusdz_value_get_array_size", self._handle, restype=ctypes.c_size_t)

    @property
    def is_animated(self) -> bool:
        """Check if value is animated"""
        return _FFI.call("tusdz_value_is_animated", self._handle, restype=ctypes.c_int) != 0

    def get(self) -> Any:
        """Get value as appropriate Python type"""
        if self.type == ValueType.BOOL:
            return self.get_bool()
        elif self.type == ValueType.INT:
            return self.get_int()
        elif self.type == ValueType.FLOAT:
            return self.get_float()
        elif self.type == ValueType.DOUBLE:
            return self.get_double()
        elif self.type in (ValueType.STRING, ValueType.TOKEN):
            return self.get_string()
        elif self.type == ValueType.FLOAT3:
            return self.get_float3()
        elif self.type == ValueType.MATRIX4D:
            return self.get_matrix4d()
        else:
            logger.warning(f"Unsupported type for automatic conversion: {self.type_name}")
            return None

    def get_bool(self) -> Optional[bool]:
        """Extract as boolean"""
        val = ctypes.c_int()
        if _FFI.call("tusdz_value_get_bool", self._handle, ctypes.byref(val), restype=ctypes.c_int) == 0:
            return bool(val.value)
        return None

    def get_int(self) -> Optional[int]:
        """Extract as integer"""
        val = ctypes.c_int()
        if _FFI.call("tusdz_value_get_int", self._handle, ctypes.byref(val), restype=ctypes.c_int) == 0:
            return int(val.value)
        return None

    def get_float(self) -> Optional[float]:
        """Extract as float"""
        val = ctypes.c_float()
        if _FFI.call("tusdz_value_get_float", self._handle, ctypes.byref(val), restype=ctypes.c_int) == 0:
            return float(val.value)
        return None

    def get_double(self) -> Optional[float]:
        """Extract as double"""
        val = ctypes.c_double()
        if _FFI.call("tusdz_value_get_double", self._handle, ctypes.byref(val), restype=ctypes.c_int) == 0:
            return float(val.value)
        return None

    def get_string(self) -> Optional[str]:
        """Extract as string"""
        val = ctypes.c_char_p()
        if _FFI.call("tusdz_value_get_string", self._handle, ctypes.byref(val), restype=ctypes.c_int) == 0:
            return val.value.decode('utf-8') if val.value else None
        return None

    def get_float3(self) -> Optional[Tuple[float, float, float]]:
        """Extract as float3 tuple"""
        vals = (ctypes.c_float * 3)()
        if _FFI.call("tusdz_value_get_float3", self._handle, vals, restype=ctypes.c_int) == 0:
            return tuple(float(v) for v in vals)
        return None

    def get_matrix4d(self) -> Optional['np.ndarray']:
        """Extract as 4x4 matrix"""
        try:
            import numpy as np
        except ImportError:
            logger.warning("NumPy required for matrix extraction")
            return None

        vals = (ctypes.c_double * 16)()
        if _FFI.call("tusdz_value_get_matrix4d", self._handle, vals, restype=ctypes.c_int) == 0:
            return np.array(vals, dtype=np.float64).reshape(4, 4)
        return None

    def __del__(self):
        if hasattr(self, '_handle') and self._handle:
            _FFI.call("tusdz_value_free", self._handle)

    def __repr__(self) -> str:
        return f"Value(type={self.type_name})"

# ============================================================================
# Prim Wrapper
# ============================================================================

class Prim:
    """USD Prim with enhanced functionality"""

    def __init__(self, handle: ctypes.c_void_p, stage: 'Stage' = None):
        if not handle:
            raise TinyUSDZValueError("Invalid prim handle")
        self._handle = handle
        self._stage = stage
        self._info_cache: Optional[PrimInfo] = None

    @property
    def name(self) -> str:
        """Get prim name"""
        return _FFI.string("tusdz_prim_get_name", self._handle)

    @property
    def path(self) -> str:
        """Get full path"""
        return _FFI.string("tusdz_prim_get_path", self._handle)

    @property
    def type(self) -> PrimType:
        """Get prim type"""
        return PrimType(_FFI.call("tusdz_prim_get_type", self._handle, restype=ctypes.c_int))

    @property
    def type_name(self) -> str:
        """Get type name"""
        return _FFI.string("tusdz_prim_get_type_name", self._handle)

    @property
    def child_count(self) -> int:
        """Number of children"""
        return _FFI.call("tusdz_prim_get_child_count", self._handle, restype=ctypes.c_size_t)

    @property
    def property_count(self) -> int:
        """Number of properties"""
        return _FFI.call("tusdz_prim_get_property_count", self._handle, restype=ctypes.c_size_t)

    # ---- Type Checking ----

    def is_type(self, prim_type: PrimType) -> bool:
        """Check if specific type"""
        return _FFI.call("tusdz_prim_is_type", self._handle, int(prim_type), restype=ctypes.c_int) != 0

    @property
    def is_mesh(self) -> bool:
        return self.is_type(PrimType.MESH)

    @property
    def is_xform(self) -> bool:
        return self.is_type(PrimType.XFORM)

    @property
    def is_material(self) -> bool:
        return self.is_type(PrimType.MATERIAL)

    @property
    def is_shader(self) -> bool:
        return self.is_type(PrimType.SHADER)

    @property
    def is_light(self) -> bool:
        return self.type in (
            PrimType.DISTANT_LIGHT, PrimType.SPHERE_LIGHT,
            PrimType.RECT_LIGHT, PrimType.DISK_LIGHT,
            PrimType.CYLINDER_LIGHT, PrimType.DOME_LIGHT
        )

    # ---- Navigation ----

    def get_child(self, index: int) -> Optional['Prim']:
        """Get child by index"""
        handle = _FFI.call("tusdz_prim_get_child_at", self._handle, index, restype=ctypes.c_void_p)
        return Prim(handle, self._stage) if handle else None

    def children(self) -> Iterator['Prim']:
        """Iterate over children"""
        for i in range(self.child_count):
            child = self.get_child(i)
            if child:
                yield child

    def iter_all_prims(self, depth: int = 0, max_depth: Optional[int] = None) -> Iterator['Prim']:
        """Recursively iterate all prims (DFS)"""
        if max_depth is None or depth < max_depth:
            yield self
            for child in self.children():
                yield from child.iter_all_prims(depth + 1, max_depth)

    def iter_all_prims_bfs(self) -> Iterator['Prim']:
        """Breadth-first iteration"""
        queue = [self]
        while queue:
            prim = queue.pop(0)
            yield prim
            queue.extend(prim.children())

    def iter_all_meshes(self) -> Iterator['Prim']:
        """Iterate all mesh prims"""
        for prim in self.iter_all_prims():
            if prim.is_mesh:
                yield prim

    # ---- Properties ----

    def get_property(self, name: str) -> Optional[Value]:
        """Get property by name"""
        handle = _FFI.call("tusdz_prim_get_property", self._handle, name.encode('utf-8'),
                          restype=ctypes.c_void_p)
        return Value(handle) if handle else None

    def properties(self) -> Dict[str, Value]:
        """Get all properties as dict"""
        result = {}
        for i in range(self.property_count):
            name = _FFI.string("tusdz_prim_get_property_name_at", self._handle, i)
            prop = self.get_property(name)
            if prop:
                result[name] = prop
        return result

    def iter_properties(self) -> Iterator[Tuple[str, Value]]:
        """Iterate over properties"""
        for i in range(self.property_count):
            name = _FFI.string("tusdz_prim_get_property_name_at", self._handle, i)
            prop = self.get_property(name)
            if prop:
                yield (name, prop)

    # ---- Mesh Operations ----

    @property
    def mesh_data(self) -> Optional[MeshData]:
        """Get mesh data (None if not mesh)"""
        if not self.is_mesh:
            return None

        try:
            import numpy as np
        except ImportError:
            logger.warning("NumPy required for mesh data")
            return None

        mesh_data = MeshData()

        # Points
        pts_ptr = ctypes.POINTER(ctypes.c_float)()
        pt_count = ctypes.c_size_t()
        if _FFI.call("tusdz_mesh_get_points", self._handle, ctypes.byref(pts_ptr),
                    ctypes.byref(pt_count), restype=ctypes.c_int) == 0 and pt_count.value > 0:
            mesh_data.points = np.ctypeslib.as_array(pts_ptr, shape=(pt_count.value,)).copy()
            mesh_data.vertex_count = pt_count.value // 3

        # Face counts
        cnt_ptr = ctypes.POINTER(ctypes.c_int)()
        cnt_count = ctypes.c_size_t()
        if _FFI.call("tusdz_mesh_get_face_counts", self._handle, ctypes.byref(cnt_ptr),
                    ctypes.byref(cnt_count), restype=ctypes.c_int) == 0 and cnt_count.value > 0:
            mesh_data.face_counts = np.ctypeslib.as_array(cnt_ptr, shape=(cnt_count.value,)).copy()
            mesh_data.face_count = cnt_count.value

        # Indices
        idx_ptr = ctypes.POINTER(ctypes.c_int)()
        idx_count = ctypes.c_size_t()
        if _FFI.call("tusdz_mesh_get_indices", self._handle, ctypes.byref(idx_ptr),
                    ctypes.byref(idx_count), restype=ctypes.c_int) == 0 and idx_count.value > 0:
            mesh_data.indices = np.ctypeslib.as_array(idx_ptr, shape=(idx_count.value,)).copy()
            mesh_data.index_count = idx_count.value

        # Normals
        norm_ptr = ctypes.POINTER(ctypes.c_float)()
        norm_count = ctypes.c_size_t()
        if _FFI.call("tusdz_mesh_get_normals", self._handle, ctypes.byref(norm_ptr),
                    ctypes.byref(norm_count), restype=ctypes.c_int) == 0 and norm_count.value > 0:
            mesh_data.normals = np.ctypeslib.as_array(norm_ptr, shape=(norm_count.value,)).copy()

        # UVs
        uv_ptr = ctypes.POINTER(ctypes.c_float)()
        uv_count = ctypes.c_size_t()
        if _FFI.call("tusdz_mesh_get_uvs", self._handle, ctypes.byref(uv_ptr),
                    ctypes.byref(uv_count), 0, restype=ctypes.c_int) == 0 and uv_count.value > 0:
            mesh_data.uvs = np.ctypeslib.as_array(uv_ptr, shape=(uv_count.value,)).copy()

        return mesh_data

    # ---- Transform Operations ----

    def get_local_matrix(self, time: float = 0.0) -> Optional[Transform]:
        """Get local transformation matrix"""
        if not self.is_xform:
            return None

        try:
            import numpy as np
        except ImportError:
            return None

        matrix = (ctypes.c_double * 16)()
        if _FFI.call("tusdz_xform_get_local_matrix", self._handle, time, matrix,
                    restype=ctypes.c_int) == 0:
            mat_array = np.array(matrix, dtype=np.float64).reshape(4, 4)
            return Transform(matrix=mat_array)
        return None

    # ---- Material Operations ----

    def get_bound_material(self) -> Optional['Prim']:
        """Get bound material"""
        handle = _FFI.call("tusdz_prim_get_bound_material", self._handle, restype=ctypes.c_void_p)
        return Prim(handle, self._stage) if handle else None

    def get_surface_shader(self) -> Optional['Prim']:
        """Get surface shader (for Material prims)"""
        handle = _FFI.call("tusdz_material_get_surface_shader", self._handle, restype=ctypes.c_void_p)
        return Prim(handle, self._stage) if handle else None

    def get_shader_input(self, name: str) -> Optional[Value]:
        """Get shader input"""
        handle = _FFI.call("tusdz_shader_get_input", self._handle, name.encode('utf-8'),
                          restype=ctypes.c_void_p)
        return Value(handle) if handle else None

    def get_shader_type(self) -> Optional[str]:
        """Get shader type ID"""
        return _FFI.string("tusdz_shader_get_type_id", self._handle) or None

    # ---- Info ----

    @property
    def info(self) -> PrimInfo:
        """Get prim information"""
        if self._info_cache is None:
            self._info_cache = PrimInfo(
                name=self.name,
                path=self.path,
                type_name=self.type_name,
                prim_type=self.type,
                child_count=self.child_count,
                property_count=self.property_count,
            )
        return self._info_cache

    def __repr__(self) -> str:
        return f"Prim(name={self.name!r}, type={self.type_name}, children={self.child_count})"

# ============================================================================
# Stage Wrapper
# ============================================================================

class Stage:
    """USD Stage with enhanced methods"""

    def __init__(self, handle: ctypes.c_void_p):
        if not handle:
            raise TinyUSDZLoadError("Invalid stage handle")
        self._handle = handle

    @property
    def root_prim(self) -> Optional[Prim]:
        """Get root prim"""
        handle = _FFI.call("tusdz_stage_get_root_prim", self._handle, restype=ctypes.c_void_p)
        return Prim(handle, self) if handle else None

    @property
    def has_animation(self) -> bool:
        """Check if stage has animation"""
        return _FFI.call("tusdz_stage_has_animation", self._handle, restype=ctypes.c_int) != 0

    def get_time_range(self) -> Optional[TimeRange]:
        """Get animation time range"""
        start = ctypes.c_double()
        end = ctypes.c_double()
        fps = ctypes.c_double()
        if _FFI.call("tusdz_stage_get_time_range", self._handle, ctypes.byref(start),
                    ctypes.byref(end), ctypes.byref(fps), restype=ctypes.c_int) == 0:
            return TimeRange(float(start.value), float(end.value), float(fps.value))
        return None

    def get_prim_at_path(self, path: str) -> Optional[Prim]:
        """Find prim by path"""
        handle = _FFI.call("tusdz_stage_get_prim_at_path", self._handle, path.encode('utf-8'),
                          restype=ctypes.c_void_p)
        return Prim(handle, self) if handle else None

    # ---- Iteration ----

    def iter_all_prims(self, depth: Optional[int] = None) -> Iterator[Prim]:
        """Iterate all prims in stage"""
        if self.root_prim:
            yield from self.root_prim.iter_all_prims(max_depth=depth)

    def iter_all_meshes(self) -> Iterator[Prim]:
        """Iterate all mesh prims"""
        for prim in self.iter_all_prims():
            if prim.is_mesh:
                yield prim

    def iter_all_xforms(self) -> Iterator[Prim]:
        """Iterate all transform prims"""
        for prim in self.iter_all_prims():
            if prim.is_xform:
                yield prim

    def iter_all_lights(self) -> Iterator[Prim]:
        """Iterate all light prims"""
        for prim in self.iter_all_prims():
            if prim.is_light:
                yield prim

    def iter_all_materials(self) -> Iterator[Prim]:
        """Iterate all material prims"""
        for prim in self.iter_all_prims():
            if prim.is_material:
                yield prim

    # ---- Query ----

    def find_by_name(self, name: str) -> QueryResult:
        """Find all prims with given name"""
        prims = [p for p in self.iter_all_prims() if p.name == name]
        return QueryResult(prims=prims)

    def find_by_type(self, prim_type: PrimType) -> QueryResult:
        """Find all prims of given type"""
        prims = [p for p in self.iter_all_prims() if p.type == prim_type]
        return QueryResult(prims=prims)

    def find_by_path(self, pattern: Union[str, 'Path']) -> QueryResult:
        """Find prims by path pattern"""
        import fnmatch
        path_str = str(pattern)
        prims = [p for p in self.iter_all_prims() if fnmatch.fnmatch(p.path, path_str)]
        return QueryResult(prims=prims)

    def find_by_predicate(self, predicate) -> QueryResult:
        """Find prims matching predicate"""
        prims = [p for p in self.iter_all_prims() if predicate(p)]
        return QueryResult(prims=prims)

    # ---- Statistics ----

    def get_statistics(self) -> Dict[str, Any]:
        """Get scene statistics"""
        stats = {
            "total_prims": 0,
            "meshes": 0,
            "transforms": 0,
            "lights": 0,
            "materials": 0,
            "shaders": 0,
            "max_depth": 0,
            "has_animation": self.has_animation,
        }

        max_depth = 0
        for prim in self.iter_all_prims():
            stats["total_prims"] += 1
            if prim.is_mesh:
                stats["meshes"] += 1
            elif prim.is_xform:
                stats["transforms"] += 1
            elif prim.is_light:
                stats["lights"] += 1
            elif prim.is_material:
                stats["materials"] += 1
            elif prim.is_shader:
                stats["shaders"] += 1

            depth = len(prim.path.split('/'))
            max_depth = max(max_depth, depth)

        stats["max_depth"] = max_depth
        return stats

    def print_info(self):
        """Print scene information"""
        stats = self.get_statistics()
        print(f"Scene Statistics:")
        print(f"  Total Prims: {stats['total_prims']}")
        print(f"  Meshes: {stats['meshes']}")
        print(f"  Transforms: {stats['transforms']}")
        print(f"  Lights: {stats['lights']}")
        print(f"  Materials: {stats['materials']}")
        print(f"  Shaders: {stats['shaders']}")
        print(f"  Max Depth: {stats['max_depth']}")
        print(f"  Has Animation: {stats['has_animation']}")

    def __del__(self):
        if hasattr(self, '_handle') and self._handle:
            _FFI.call("tusdz_stage_free", self._handle)

    def __repr__(self) -> str:
        root = self.root_prim
        return f"Stage(root={root.name if root else 'None'!r})"

# ============================================================================
# Main API
# ============================================================================

class TinyUSDZ:
    """Main TinyUSDZ API with context manager support"""

    def __init__(self, enable_logging: bool = False):
        """Initialize TinyUSDZ"""
        if enable_logging:
            logging.basicConfig(level=logging.DEBUG)

        result = _FFI.call("tusdz_init", restype=ctypes.c_int)
        if result != 0:
            raise TinyUSDZError("Failed to initialize TinyUSDZ")
        logger.debug("TinyUSDZ initialized")

    @staticmethod
    def get_version() -> str:
        """Get library version"""
        return _FFI.string("tusdz_get_version")

    def load_file(self, filepath: Union[str, Path], max_memory_mb: int = 0) -> Stage:
        """Load USD file"""
        filepath = str(filepath)
        logger.debug(f"Loading: {filepath}")

        error_buf = ctypes.create_string_buffer(1024)
        stage_ptr = ctypes.c_void_p()

        result = _FFI.call("tusdz_load_from_file",
                          filepath.encode('utf-8'),
                          None,
                          ctypes.byref(stage_ptr),
                          error_buf,
                          1024,
                          restype=ctypes.c_int)

        if result != 0:
            error_msg = error_buf.value.decode('utf-8', errors='ignore').strip()
            raise TinyUSDZLoadError(f"Failed to load '{filepath}': {error_msg}")

        logger.debug(f"Loaded successfully")
        return Stage(stage_ptr.value)

    def load_from_memory(self, data: bytes, format: Format = Format.AUTO) -> Stage:
        """Load USD from memory"""
        logger.debug(f"Loading from memory ({len(data)} bytes)")

        error_buf = ctypes.create_string_buffer(1024)
        stage_ptr = ctypes.c_void_p()

        result = _FFI.call("tusdz_load_from_memory",
                          ctypes.c_char_p(data),
                          len(data),
                          int(format),
                          None,
                          ctypes.byref(stage_ptr),
                          error_buf,
                          1024,
                          restype=ctypes.c_int)

        if result != 0:
            error_msg = error_buf.value.decode('utf-8', errors='ignore').strip()
            raise TinyUSDZLoadError(f"Failed to load from memory: {error_msg}")

        return Stage(stage_ptr.value)

    def detect_format(self, filepath: str) -> Format:
        """Detect USD format"""
        result = _FFI.call("tusdz_detect_format", filepath.encode('utf-8'), restype=ctypes.c_int)
        return Format(result)

    # ---- Context Manager ----

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.shutdown()

    def shutdown(self):
        """Shutdown TinyUSDZ"""
        _FFI.call("tusdz_shutdown")
        logger.debug("TinyUSDZ shutdown")

    def __repr__(self) -> str:
        return f"TinyUSDZ(version={self.get_version()})"

# ============================================================================
# Type String Methods
# ============================================================================

PrimType.to_string = lambda self: _FFI.string("tusdz_prim_type_to_string", int(self))
ValueType.to_string = lambda self: _FFI.string("tusdz_value_type_to_string", int(self))

# ============================================================================
# Auto-initialization on import (disabled by default)
# ============================================================================

__all__ = [
    "TinyUSDZ",
    "Stage",
    "Prim",
    "Value",
    "Format",
    "PrimType",
    "ValueType",
    "MeshData",
    "Transform",
    "TimeRange",
    "PrimInfo",
    "QueryResult",
    # Exceptions
    "TinyUSDZError",
    "TinyUSDZLoadError",
    "TinyUSDZTypeError",
    "TinyUSDZValueError",
    "TinyUSDZNotFoundError",
]