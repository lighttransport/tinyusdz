"""
TinyUSDZ Python Bindings

Pure Python ctypes bindings for the TinyUSDZ C99 API.
No compilation or external dependencies required.

Usage:
    >>> import tinyusdz
    >>> tinyusdz.init()
    >>> stage = tinyusdz.load_from_file("model.usd")
    >>> root = stage.get_root_prim()
    >>> print(root.name)
    >>> root.print_hierarchy()
    >>> tinyusdz.shutdown()
"""

import ctypes
import ctypes.util
from pathlib import Path
from typing import Optional, Tuple, List, Union
import sys

# ============================================================================
# Load C Library
# ============================================================================

def _find_library():
    """Find the TinyUSDZ C library"""
    # Try different naming conventions
    names = [
        "tinyusdz_c",
        "libtinyusdz_c",
        "libtinyusdz_c.so",
        "libtinyusdz_c.so.1",
        "libtinyusdz_c.dylib",
        "tinyusdz_c.dll",
    ]

    for name in names:
        lib = ctypes.util.find_library(name)
        if lib:
            return lib

    # Try local paths
    local_paths = [
        Path(__file__).parent / "libtinyusdz_c.so",
        Path(__file__).parent / "libtinyusdz_c.a",
        Path(__file__).parent / "build" / "libtinyusdz_c.so",
        Path(__file__).parent.parent.parent / "build" / "libtinyusdz_c.so",
    ]

    for path in local_paths:
        if path.exists():
            return str(path)

    return None


# Load the library
_lib_path = _find_library()
if _lib_path is None:
    raise RuntimeError(
        "Cannot find libtinyusdz_c. Make sure to build the C API first:\n"
        "  mkdir build && cd build && cmake .. && make"
    )

_lib = ctypes.CDLL(_lib_path)

# ============================================================================
# Type Definitions
# ============================================================================

# Opaque handle types
class Stage:
    """USD Stage handle"""
    pass


class Prim:
    """USD Prim handle"""
    pass


class Value:
    """USD Value handle"""
    pass


class Layer:
    """USD Layer handle"""
    pass


# Result codes
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
        """Convert result code to string"""
        _lib.tusdz_result_to_string.restype = ctypes.c_char_p
        _lib.tusdz_result_to_string.argtypes = [ctypes.c_int]
        return _lib.tusdz_result_to_string(result).decode('utf-8')


# Format types
class Format:
    AUTO = 0
    USDA = 1  # ASCII
    USDC = 2  # Binary/Crate
    USDZ = 3  # Zip archive


# Prim types
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
    NURBS_PATCH = 22
    NURBS_CURVE = 23
    BASIS_CURVES = 24
    POINT_INSTANCER = 25
    VOLUME = 26

    @staticmethod
    def to_string(prim_type: int) -> str:
        """Convert prim type to string"""
        _lib.tusdz_prim_type_to_string.restype = ctypes.c_char_p
        _lib.tusdz_prim_type_to_string.argtypes = [ctypes.c_int]
        return _lib.tusdz_prim_type_to_string(prim_type).decode('utf-8')


# Value types
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
    COLOR3D = 27
    NORMAL3F = 29
    NORMAL3D = 30
    POINT3F = 31
    POINT3D = 32
    TEXCOORD2F = 33
    TEXCOORD2D = 34
    ARRAY = 41
    TIME_SAMPLES = 43

    @staticmethod
    def to_string(value_type: int) -> str:
        """Convert value type to string"""
        _lib.tusdz_value_type_to_string.restype = ctypes.c_char_p
        _lib.tusdz_value_type_to_string.argtypes = [ctypes.c_int]
        return _lib.tusdz_value_type_to_string(value_type).decode('utf-8')


class LoadOptions(ctypes.Structure):
    """Load options for USD files"""
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
# Wrapper Classes
# ============================================================================

class PrimWrapper:
    """Wrapper for USD Prim"""

    def __init__(self, prim_handle):
        self._handle = prim_handle

    @property
    def name(self) -> str:
        """Get prim name"""
        _lib.tusdz_prim_get_name.restype = ctypes.c_char_p
        _lib.tusdz_prim_get_name.argtypes = [ctypes.c_void_p]
        name = _lib.tusdz_prim_get_name(self._handle)
        return name.decode('utf-8') if name else ""

    @property
    def path(self) -> str:
        """Get prim path"""
        _lib.tusdz_prim_get_path.restype = ctypes.c_char_p
        _lib.tusdz_prim_get_path.argtypes = [ctypes.c_void_p]
        path = _lib.tusdz_prim_get_path(self._handle)
        return path.decode('utf-8') if path else ""

    @property
    def prim_type(self) -> int:
        """Get prim type"""
        _lib.tusdz_prim_get_type.restype = ctypes.c_int
        _lib.tusdz_prim_get_type.argtypes = [ctypes.c_void_p]
        return _lib.tusdz_prim_get_type(self._handle)

    @property
    def type_name(self) -> str:
        """Get prim type name"""
        _lib.tusdz_prim_get_type_name.restype = ctypes.c_char_p
        _lib.tusdz_prim_get_type_name.argtypes = [ctypes.c_void_p]
        name = _lib.tusdz_prim_get_type_name(self._handle)
        return name.decode('utf-8') if name else "Unknown"

    def is_type(self, prim_type: int) -> bool:
        """Check if prim is specific type"""
        _lib.tusdz_prim_is_type.restype = ctypes.c_int
        _lib.tusdz_prim_is_type.argtypes = [ctypes.c_void_p, ctypes.c_int]
        return bool(_lib.tusdz_prim_is_type(self._handle, prim_type))

    @property
    def child_count(self) -> int:
        """Get number of child prims"""
        _lib.tusdz_prim_get_child_count.restype = ctypes.c_size_t
        _lib.tusdz_prim_get_child_count.argtypes = [ctypes.c_void_p]
        return _lib.tusdz_prim_get_child_count(self._handle)

    def get_child(self, index: int) -> Optional['PrimWrapper']:
        """Get child prim at index"""
        _lib.tusdz_prim_get_child_at.restype = ctypes.c_void_p
        _lib.tusdz_prim_get_child_at.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        child = _lib.tusdz_prim_get_child_at(self._handle, index)
        return PrimWrapper(child) if child else None

    def get_children(self) -> List['PrimWrapper']:
        """Get all child prims"""
        return [self.get_child(i) for i in range(self.child_count)]

    @property
    def property_count(self) -> int:
        """Get number of properties"""
        _lib.tusdz_prim_get_property_count.restype = ctypes.c_size_t
        _lib.tusdz_prim_get_property_count.argtypes = [ctypes.c_void_p]
        return _lib.tusdz_prim_get_property_count(self._handle)

    def get_property_name(self, index: int) -> str:
        """Get property name at index"""
        _lib.tusdz_prim_get_property_name_at.restype = ctypes.c_char_p
        _lib.tusdz_prim_get_property_name_at.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        name = _lib.tusdz_prim_get_property_name_at(self._handle, index)
        return name.decode('utf-8') if name else ""

    def get_property(self, name: str) -> Optional['ValueWrapper']:
        """Get property by name"""
        _lib.tusdz_prim_get_property.restype = ctypes.c_void_p
        _lib.tusdz_prim_get_property.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        value = _lib.tusdz_prim_get_property(self._handle, name.encode('utf-8'))
        return ValueWrapper(value) if value else None

    @property
    def properties(self) -> dict:
        """Get all properties as dict"""
        result = {}
        for i in range(self.property_count):
            name = self.get_property_name(i)
            result[name] = self.get_property(name)
        return result

    def is_mesh(self) -> bool:
        """Check if this is a mesh prim"""
        return self.is_type(PrimType.MESH)

    def is_xform(self) -> bool:
        """Check if this is a transform prim"""
        return self.is_type(PrimType.XFORM)

    def print_hierarchy(self, max_depth: int = -1):
        """Print prim hierarchy to stdout"""
        _lib.tusdz_stage_print_hierarchy.argtypes = [ctypes.c_void_p, ctypes.c_int]
        _lib.tusdz_stage_print_hierarchy(self._handle, max_depth)

    def __repr__(self) -> str:
        return f"PrimWrapper(name='{self.name}', type='{self.type_name}', children={self.child_count})"


class ValueWrapper:
    """Wrapper for USD Value"""

    def __init__(self, value_handle):
        self._handle = value_handle

    @property
    def value_type(self) -> int:
        """Get value type"""
        _lib.tusdz_value_get_type.restype = ctypes.c_int
        _lib.tusdz_value_get_type.argtypes = [ctypes.c_void_p]
        return _lib.tusdz_value_get_type(self._handle)

    @property
    def type_name(self) -> str:
        """Get value type name"""
        return ValueType.to_string(self.value_type)

    @property
    def is_array(self) -> bool:
        """Check if value is an array"""
        _lib.tusdz_value_is_array.restype = ctypes.c_int
        _lib.tusdz_value_is_array.argtypes = [ctypes.c_void_p]
        return bool(_lib.tusdz_value_is_array(self._handle))

    @property
    def array_size(self) -> int:
        """Get array size"""
        _lib.tusdz_value_get_array_size.restype = ctypes.c_size_t
        _lib.tusdz_value_get_array_size.argtypes = [ctypes.c_void_p]
        return _lib.tusdz_value_get_array_size(self._handle)

    def get_float(self) -> Optional[float]:
        """Get as float"""
        value = ctypes.c_float()
        _lib.tusdz_value_get_float.restype = ctypes.c_int
        _lib.tusdz_value_get_float.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_float)]
        if _lib.tusdz_value_get_float(self._handle, ctypes.byref(value)) == Result.SUCCESS:
            return float(value.value)
        return None

    def get_double(self) -> Optional[float]:
        """Get as double"""
        value = ctypes.c_double()
        _lib.tusdz_value_get_double.restype = ctypes.c_int
        _lib.tusdz_value_get_double.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_double)]
        if _lib.tusdz_value_get_double(self._handle, ctypes.byref(value)) == Result.SUCCESS:
            return float(value.value)
        return None

    def get_int(self) -> Optional[int]:
        """Get as int"""
        value = ctypes.c_int()
        _lib.tusdz_value_get_int.restype = ctypes.c_int
        _lib.tusdz_value_get_int.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int)]
        if _lib.tusdz_value_get_int(self._handle, ctypes.byref(value)) == Result.SUCCESS:
            return int(value.value)
        return None

    def get_string(self) -> Optional[str]:
        """Get as string"""
        value = ctypes.c_char_p()
        _lib.tusdz_value_get_string.restype = ctypes.c_int
        _lib.tusdz_value_get_string.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_char_p)]
        if _lib.tusdz_value_get_string(self._handle, ctypes.byref(value)) == Result.SUCCESS:
            return value.value.decode('utf-8') if value.value else None
        return None

    def get_float3(self) -> Optional[Tuple[float, float, float]]:
        """Get as float3"""
        values = (ctypes.c_float * 3)()
        _lib.tusdz_value_get_float3.restype = ctypes.c_int
        _lib.tusdz_value_get_float3.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_float)]
        if _lib.tusdz_value_get_float3(self._handle, values) == Result.SUCCESS:
            return tuple(float(v) for v in values)
        return None

    def __repr__(self) -> str:
        return f"ValueWrapper(type='{self.type_name}')"


class StageWrapper:
    """Wrapper for USD Stage"""

    def __init__(self, stage_handle):
        self._handle = stage_handle

    @property
    def root_prim(self) -> PrimWrapper:
        """Get root prim"""
        _lib.tusdz_stage_get_root_prim.restype = ctypes.c_void_p
        _lib.tusdz_stage_get_root_prim.argtypes = [ctypes.c_void_p]
        root = _lib.tusdz_stage_get_root_prim(self._handle)
        return PrimWrapper(root) if root else None

    def get_prim_at_path(self, path: str) -> Optional[PrimWrapper]:
        """Get prim at path"""
        _lib.tusdz_stage_get_prim_at_path.restype = ctypes.c_void_p
        _lib.tusdz_stage_get_prim_at_path.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        prim = _lib.tusdz_stage_get_prim_at_path(self._handle, path.encode('utf-8'))
        return PrimWrapper(prim) if prim else None

    @property
    def has_animation(self) -> bool:
        """Check if stage has animation"""
        _lib.tusdz_stage_has_animation.restype = ctypes.c_int
        _lib.tusdz_stage_has_animation.argtypes = [ctypes.c_void_p]
        return bool(_lib.tusdz_stage_has_animation(self._handle))

    def get_time_range(self) -> Optional[Tuple[float, float, float]]:
        """Get time range (start, end, fps)"""
        start = ctypes.c_double()
        end = ctypes.c_double()
        fps = ctypes.c_double()
        _lib.tusdz_stage_get_time_range.restype = ctypes.c_int
        _lib.tusdz_stage_get_time_range.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_double),
        ]
        if _lib.tusdz_stage_get_time_range(
            self._handle, ctypes.byref(start), ctypes.byref(end), ctypes.byref(fps)
        ) == Result.SUCCESS:
            return (float(start.value), float(end.value), float(fps.value))
        return None

    def __del__(self):
        """Clean up stage"""
        if self._handle:
            _lib.tusdz_stage_free(self._handle)

    def __repr__(self) -> str:
        root = self.root_prim
        return f"StageWrapper(root='{root.name if root else 'None'}')"


# ============================================================================
# Global API Functions
# ============================================================================

def init() -> bool:
    """Initialize TinyUSDZ library"""
    _lib.tusdz_init.restype = ctypes.c_int
    return _lib.tusdz_init() == Result.SUCCESS


def shutdown():
    """Shutdown TinyUSDZ library"""
    _lib.tusdz_shutdown.argtypes = []
    _lib.tusdz_shutdown()


def get_version() -> str:
    """Get TinyUSDZ version"""
    _lib.tusdz_get_version.restype = ctypes.c_char_p
    version = _lib.tusdz_get_version()
    return version.decode('utf-8') if version else "unknown"


def load_from_file(
    filepath: str,
    options: Optional[LoadOptions] = None,
    capture_error: bool = True,
) -> Optional[StageWrapper]:
    """Load USD from file"""
    error_buf = ctypes.create_string_buffer(1024)
    stage = ctypes.c_void_p()

    _lib.tusdz_load_from_file.restype = ctypes.c_int
    _lib.tusdz_load_from_file.argtypes = [
        ctypes.c_char_p,
        ctypes.POINTER(LoadOptions),
        ctypes.POINTER(ctypes.c_void_p),
        ctypes.c_char_p,
        ctypes.c_size_t,
    ]

    result = _lib.tusdz_load_from_file(
        filepath.encode('utf-8'),
        ctypes.byref(options) if options else None,
        ctypes.byref(stage),
        error_buf,
        len(error_buf),
    )

    if result != Result.SUCCESS:
        error_msg = error_buf.value.decode('utf-8') if error_buf.value else "Unknown error"
        if capture_error:
            raise RuntimeError(f"Failed to load USD: {error_msg} (code: {result})")
        return None

    return StageWrapper(stage.value) if stage.value else None


def load_from_memory(
    data: bytes,
    format: int = Format.AUTO,
    options: Optional[LoadOptions] = None,
    capture_error: bool = True,
) -> Optional[StageWrapper]:
    """Load USD from memory"""
    error_buf = ctypes.create_string_buffer(1024)
    stage = ctypes.c_void_p()

    _lib.tusdz_load_from_memory.restype = ctypes.c_int
    _lib.tusdz_load_from_memory.argtypes = [
        ctypes.c_void_p,
        ctypes.c_size_t,
        ctypes.c_int,
        ctypes.POINTER(LoadOptions),
        ctypes.POINTER(ctypes.c_void_p),
        ctypes.c_char_p,
        ctypes.c_size_t,
    ]

    result = _lib.tusdz_load_from_memory(
        ctypes.c_char_p(data),
        len(data),
        format,
        ctypes.byref(options) if options else None,
        ctypes.byref(stage),
        error_buf,
        len(error_buf),
    )

    if result != Result.SUCCESS:
        error_msg = error_buf.value.decode('utf-8') if error_buf.value else "Unknown error"
        if capture_error:
            raise RuntimeError(f"Failed to load USD from memory: {error_msg}")
        return None

    return StageWrapper(stage.value) if stage.value else None


def detect_format(filepath: str) -> int:
    """Detect USD file format"""
    _lib.tusdz_detect_format.restype = ctypes.c_int
    _lib.tusdz_detect_format.argtypes = [ctypes.c_char_p]
    return _lib.tusdz_detect_format(filepath.encode('utf-8'))


# ============================================================================
# Auto-initialization
# ============================================================================

def _auto_init():
    """Auto-initialize library on import"""
    try:
        init()
    except Exception:
        pass  # Library might already be initialized


# Initialize on import
_auto_init()

# Cleanup on exit
import atexit
atexit.register(shutdown)