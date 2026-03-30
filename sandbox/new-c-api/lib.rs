//! TinyUSDZ Rust FFI Bindings
//!
//! Safe Rust bindings for the TinyUSDZ C99 API.
//!
//! # Examples
//!
//! ```no_run
//! use tinyusdz::{init, shutdown, load_from_file, PrimType};
//!
//! fn main() -> Result<(), Box<dyn std::error::Error>> {
//!     init()?;
//!
//!     let stage = load_from_file("model.usd", None)?;
//!     let root = stage.root_prim();
//!
//!     if let Some(root) = root {
//!         println!("Root: {}", root.name());
//!         for child in root.children() {
//!             println!("  - {} [{}]", child.name(), child.type_name());
//!         }
//!     }
//!
//!     shutdown();
//!     Ok(())
//! }
//! ```

use std::ffi::{CStr, CString, c_void, c_int, c_uint, c_float, c_double, c_char};
use std::os::raw::*;
use std::ptr;
use std::path::Path;

// ============================================================================
// FFI Bindings
// ============================================================================

#[link(name = "tinyusdz_c")]
extern "C" {
    // Initialization
    fn tusdz_init() -> c_int;
    fn tusdz_shutdown();
    fn tusdz_get_version() -> *const c_char;

    // Loading
    fn tusdz_load_from_file(
        filepath: *const c_char,
        options: *const LoadOptionsC,
        out_stage: *mut *mut c_void,
        error_buf: *mut c_char,
        error_buf_size: usize,
    ) -> c_int;

    fn tusdz_load_from_memory(
        data: *const c_void,
        size: usize,
        format: c_int,
        options: *const LoadOptionsC,
        out_stage: *mut *mut c_void,
        error_buf: *mut c_char,
        error_buf_size: usize,
    ) -> c_int;

    fn tusdz_stage_free(stage: *mut c_void);

    // Prim operations
    fn tusdz_stage_get_root_prim(stage: *mut c_void) -> *mut c_void;
    fn tusdz_prim_get_name(prim: *mut c_void) -> *const c_char;
    fn tusdz_prim_get_path(prim: *mut c_void) -> *const c_char;
    fn tusdz_prim_get_type(prim: *mut c_void) -> c_int;
    fn tusdz_prim_get_type_name(prim: *mut c_void) -> *const c_char;
    fn tusdz_prim_is_type(prim: *mut c_void, prim_type: c_int) -> c_int;
    fn tusdz_prim_get_child_count(prim: *mut c_void) -> usize;
    fn tusdz_prim_get_child_at(prim: *mut c_void, index: usize) -> *mut c_void;
    fn tusdz_prim_get_property_count(prim: *mut c_void) -> usize;
    fn tusdz_prim_get_property_name_at(prim: *mut c_void, index: usize) -> *const c_char;
    fn tusdz_prim_get_property(prim: *mut c_void, name: *const c_char) -> *mut c_void;

    // Value operations
    fn tusdz_value_free(value: *mut c_void);
    fn tusdz_value_get_type(value: *mut c_void) -> c_int;
    fn tusdz_value_is_array(value: *mut c_void) -> c_int;
    fn tusdz_value_get_array_size(value: *mut c_void) -> usize;
    fn tusdz_value_get_float(value: *mut c_void, out: *mut c_float) -> c_int;
    fn tusdz_value_get_double(value: *mut c_void, out: *mut c_double) -> c_int;
    fn tusdz_value_get_int(value: *mut c_void, out: *mut c_int) -> c_int;
    fn tusdz_value_get_string(value: *mut c_void, out: *mut *const c_char) -> c_int;
    fn tusdz_value_get_float3(value: *mut c_void, out: *mut [c_float; 3]) -> c_int;
    fn tusdz_value_get_matrix4d(value: *mut c_void, out: *mut [c_double; 16]) -> c_int;

    // Mesh operations
    fn tusdz_mesh_get_points(
        mesh: *mut c_void,
        out_points: *mut *const c_float,
        out_count: *mut usize,
    ) -> c_int;
    fn tusdz_mesh_get_face_counts(
        mesh: *mut c_void,
        out_counts: *mut *const c_int,
        out_count: *mut usize,
    ) -> c_int;
    fn tusdz_mesh_get_indices(
        mesh: *mut c_void,
        out_indices: *mut *const c_int,
        out_count: *mut usize,
    ) -> c_int;

    // Transform operations
    fn tusdz_xform_get_local_matrix(
        xform: *mut c_void,
        time: c_double,
        out_matrix: *mut [c_double; 16],
    ) -> c_int;

    // Material operations
    fn tusdz_prim_get_bound_material(prim: *mut c_void) -> *mut c_void;
    fn tusdz_material_get_surface_shader(material: *mut c_void) -> *mut c_void;
    fn tusdz_shader_get_input(shader: *mut c_void, input_name: *const c_char) -> *mut c_void;
    fn tusdz_shader_get_type_id(shader: *mut c_void) -> *const c_char;

    // Animation operations
    fn tusdz_stage_has_animation(stage: *mut c_void) -> c_int;
    fn tusdz_stage_get_time_range(
        stage: *mut c_void,
        out_start: *mut c_double,
        out_end: *mut c_double,
        out_fps: *mut c_double,
    ) -> c_int;
    fn tusdz_value_is_animated(value: *mut c_void) -> c_int;

    // Utilities
    fn tusdz_result_to_string(result: c_int) -> *const c_char;
    fn tusdz_prim_type_to_string(prim_type: c_int) -> *const c_char;
    fn tusdz_value_type_to_string(value_type: c_int) -> *const c_char;
    fn tusdz_detect_format(filepath: *const c_char) -> c_int;
    fn tusdz_stage_print_hierarchy(stage: *mut c_void, max_depth: c_int);
    fn tusdz_get_memory_stats(
        stage: *mut c_void,
        out_bytes_used: *mut usize,
        out_bytes_peak: *mut usize,
    );
}

// ============================================================================
// C Structure Mapping
// ============================================================================

#[repr(C)]
struct LoadOptionsC {
    max_memory_limit_mb: usize,
    max_depth: c_int,
    enable_composition: c_int,
    strict_mode: c_int,
    structure_only: c_int,
    asset_resolver: *const c_void,
    asset_resolver_data: *const c_void,
}

// ============================================================================
// Result Codes
// ============================================================================

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Result {
    Success = 0,
    FileNotFound = -1,
    ParseFailed = -2,
    OutOfMemory = -3,
    InvalidArgument = -4,
    NotSupported = -5,
    CompositionFailed = -6,
    InvalidFormat = -7,
    IoError = -8,
    Internal = -99,
}

impl Result {
    pub fn to_string(&self) -> String {
        unsafe {
            let s = tusdz_result_to_string(*self as c_int);
            if !s.is_null() {
                CStr::from_ptr(s).to_string_lossy().to_string()
            } else {
                "Unknown".to_string()
            }
        }
    }
}

// ============================================================================
// Type Enums
// ============================================================================

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Format {
    Auto = 0,
    Usda = 1,
    Usdc = 2,
    Usdz = 3,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PrimType {
    Unknown = 0,
    Xform = 1,
    Mesh = 2,
    Material = 3,
    Shader = 4,
    Camera = 5,
    DistantLight = 6,
    SphereLight = 7,
    RectLight = 8,
    DiskLight = 9,
    CylinderLight = 10,
    DomeLight = 11,
    Skeleton = 12,
    SkelRoot = 13,
    SkelAnimation = 14,
    Scope = 15,
    GeomSubset = 16,
    Sphere = 17,
    Cube = 18,
    Cylinder = 19,
    Capsule = 20,
    Cone = 21,
}

impl PrimType {
    pub fn to_string(&self) -> String {
        unsafe {
            let s = tusdz_prim_type_to_string(*self as c_int);
            if !s.is_null() {
                CStr::from_ptr(s).to_string_lossy().to_string()
            } else {
                "Unknown".to_string()
            }
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ValueType {
    None = 0,
    Bool = 1,
    Int = 2,
    Uint = 3,
    Float = 5,
    Double = 6,
    String = 7,
    Float2 = 13,
    Float3 = 14,
    Float4 = 15,
    Double2 = 16,
    Double3 = 17,
    Double4 = 18,
    Matrix3D = 22,
    Matrix4D = 23,
    QuatF = 24,
    QuatD = 25,
    Color3F = 26,
    Normal3F = 29,
    Point3F = 31,
    TexCoord2F = 33,
    Array = 41,
    TimeSamples = 43,
}

impl ValueType {
    pub fn to_string(&self) -> String {
        unsafe {
            let s = tusdz_value_type_to_string(*self as c_int);
            if !s.is_null() {
                CStr::from_ptr(s).to_string_lossy().to_string()
            } else {
                "Unknown".to_string()
            }
        }
    }
}

// ============================================================================
// Load Options
// ============================================================================

#[derive(Debug, Clone)]
pub struct LoadOptions {
    pub max_memory_limit_mb: usize,
    pub max_depth: i32,
    pub enable_composition: bool,
    pub strict_mode: bool,
    pub structure_only: bool,
}

impl Default for LoadOptions {
    fn default() -> Self {
        Self {
            max_memory_limit_mb: 0,
            max_depth: 0,
            enable_composition: true,
            strict_mode: false,
            structure_only: false,
        }
    }
}

// ============================================================================
// Mesh Data
// ============================================================================

#[derive(Debug, Clone)]
pub struct MeshData {
    pub points: Option<Vec<f32>>,
    pub indices: Option<Vec<i32>>,
    pub face_counts: Option<Vec<i32>>,
    pub normals: Option<Vec<f32>>,
    pub uvs: Option<Vec<f32>>,
    pub vertex_count: usize,
    pub face_count: usize,
}

// ============================================================================
// Transform
// ============================================================================

#[derive(Debug, Clone)]
pub struct Transform {
    pub matrix: [[f64; 4]; 4],
}

// ============================================================================
// Value Wrapper
// ============================================================================

pub struct Value {
    handle: *mut c_void,
}

impl Value {
    unsafe fn from_raw(handle: *mut c_void) -> Option<Self> {
        if handle.is_null() {
            None
        } else {
            Some(Value { handle })
        }
    }

    pub fn value_type(&self) -> ValueType {
        unsafe { std::mem::transmute(tusdz_value_get_type(self.handle) as u32) }
    }

    pub fn is_array(&self) -> bool {
        unsafe { tusdz_value_is_array(self.handle) != 0 }
    }

    pub fn array_size(&self) -> usize {
        unsafe { tusdz_value_get_array_size(self.handle) }
    }

    pub fn get_float(&self) -> Option<f32> {
        unsafe {
            let mut val = 0.0f32;
            if tusdz_value_get_float(self.handle, &mut val) == 0 {
                Some(val)
            } else {
                None
            }
        }
    }

    pub fn get_double(&self) -> Option<f64> {
        unsafe {
            let mut val = 0.0f64;
            if tusdz_value_get_double(self.handle, &mut val) == 0 {
                Some(val)
            } else {
                None
            }
        }
    }

    pub fn get_string(&self) -> Option<String> {
        unsafe {
            let mut ptr: *const c_char = ptr::null();
            if tusdz_value_get_string(self.handle, &mut ptr) == 0 && !ptr.is_null() {
                Some(CStr::from_ptr(ptr).to_string_lossy().to_string())
            } else {
                None
            }
        }
    }

    pub fn get_float3(&self) -> Option<[f32; 3]> {
        unsafe {
            let mut val = [0.0f32; 3];
            if tusdz_value_get_float3(self.handle, &mut val) == 0 {
                Some(val)
            } else {
                None
            }
        }
    }
}

impl Drop for Value {
    fn drop(&mut self) {
        unsafe {
            tusdz_value_free(self.handle);
        }
    }
}

// ============================================================================
// Prim Wrapper
// ============================================================================

pub struct Prim {
    handle: *mut c_void,
}

impl Prim {
    unsafe fn from_raw(handle: *mut c_void) -> Option<Self> {
        if handle.is_null() {
            None
        } else {
            Some(Prim { handle })
        }
    }

    pub fn name(&self) -> String {
        unsafe {
            let s = tusdz_prim_get_name(self.handle);
            if !s.is_null() {
                CStr::from_ptr(s).to_string_lossy().to_string()
            } else {
                String::new()
            }
        }
    }

    pub fn path(&self) -> String {
        unsafe {
            let s = tusdz_prim_get_path(self.handle);
            if !s.is_null() {
                CStr::from_ptr(s).to_string_lossy().to_string()
            } else {
                String::new()
            }
        }
    }

    pub fn prim_type(&self) -> PrimType {
        unsafe { std::mem::transmute(tusdz_prim_get_type(self.handle) as u32) }
    }

    pub fn type_name(&self) -> String {
        unsafe {
            let s = tusdz_prim_get_type_name(self.handle);
            if !s.is_null() {
                CStr::from_ptr(s).to_string_lossy().to_string()
            } else {
                "Unknown".to_string()
            }
        }
    }

    pub fn is_type(&self, prim_type: PrimType) -> bool {
        unsafe { tusdz_prim_is_type(self.handle, prim_type as c_int) != 0 }
    }

    pub fn is_mesh(&self) -> bool {
        self.is_type(PrimType::Mesh)
    }

    pub fn is_xform(&self) -> bool {
        self.is_type(PrimType::Xform)
    }

    pub fn child_count(&self) -> usize {
        unsafe { tusdz_prim_get_child_count(self.handle) }
    }

    pub fn child(&self, index: usize) -> Option<Prim> {
        unsafe { Prim::from_raw(tusdz_prim_get_child_at(self.handle, index)) }
    }

    pub fn children(&self) -> Vec<Prim> {
        (0..self.child_count()).filter_map(|i| self.child(i)).collect()
    }

    pub fn property_count(&self) -> usize {
        unsafe { tusdz_prim_get_property_count(self.handle) }
    }

    pub fn property_name(&self, index: usize) -> String {
        unsafe {
            let s = tusdz_prim_get_property_name_at(self.handle, index);
            if !s.is_null() {
                CStr::from_ptr(s).to_string_lossy().to_string()
            } else {
                String::new()
            }
        }
    }

    pub fn property(&self, name: &str) -> Option<Value> {
        unsafe {
            let cname = CString::new(name).ok()?;
            Value::from_raw(tusdz_prim_get_property(self.handle, cname.as_ptr()))
        }
    }

    // Mesh operations
    pub fn get_mesh_data(&self) -> Option<MeshData> {
        if !self.is_mesh() {
            return None;
        }

        let mut mesh_data = MeshData {
            points: None,
            indices: None,
            face_counts: None,
            normals: None,
            uvs: None,
            vertex_count: 0,
            face_count: 0,
        };

        unsafe {
            // Points
            let mut ptr: *const c_float = ptr::null();
            let mut count = 0usize;
            if tusdz_mesh_get_points(self.handle, &mut ptr, &mut count) == 0 && !ptr.is_null() {
                mesh_data.points = Some(std::slice::from_raw_parts(ptr, count).to_vec());
                mesh_data.vertex_count = count / 3;
            }

            // Face counts
            let mut ptr: *const c_int = ptr::null();
            let mut count = 0usize;
            if tusdz_mesh_get_face_counts(self.handle, &mut ptr, &mut count) == 0 && !ptr.is_null() {
                mesh_data.face_counts = Some(std::slice::from_raw_parts(ptr, count).to_vec());
                mesh_data.face_count = count;
            }

            // Indices
            let mut ptr: *const c_int = ptr::null();
            let mut count = 0usize;
            if tusdz_mesh_get_indices(self.handle, &mut ptr, &mut count) == 0 && !ptr.is_null() {
                mesh_data.indices = Some(std::slice::from_raw_parts(ptr, count).to_vec());
            }
        }

        Some(mesh_data)
    }

    // Transform operations
    pub fn get_local_matrix(&self, time: f64) -> Option<Transform> {
        unsafe {
            let mut matrix = [[0.0f64; 4]; 4];
            if tusdz_xform_get_local_matrix(self.handle, time, &mut matrix) == 0 {
                Some(Transform { matrix })
            } else {
                None
            }
        }
    }
}

// ============================================================================
// Stage Wrapper
// ============================================================================

pub struct Stage {
    handle: *mut c_void,
}

impl Stage {
    unsafe fn from_raw(handle: *mut c_void) -> Option<Self> {
        if handle.is_null() {
            None
        } else {
            Some(Stage { handle })
        }
    }

    pub fn root_prim(&self) -> Option<Prim> {
        unsafe { Prim::from_raw(tusdz_stage_get_root_prim(self.handle)) }
    }

    pub fn has_animation(&self) -> bool {
        unsafe { tusdz_stage_has_animation(self.handle) != 0 }
    }

    pub fn get_time_range(&self) -> Option<(f64, f64, f64)> {
        unsafe {
            let mut start = 0.0f64;
            let mut end = 0.0f64;
            let mut fps = 0.0f64;
            if tusdz_stage_get_time_range(self.handle, &mut start, &mut end, &mut fps) == 0 {
                Some((start, end, fps))
            } else {
                None
            }
        }
    }

    pub fn print_hierarchy(&self, max_depth: i32) {
        unsafe {
            tusdz_stage_print_hierarchy(self.handle, max_depth);
        }
    }
}

impl Drop for Stage {
    fn drop(&mut self) {
        unsafe {
            tusdz_stage_free(self.handle);
        }
    }
}

// ============================================================================
// Global Functions
// ============================================================================

pub fn init() -> Result<()> {
    unsafe {
        match tusdz_init() {
            0 => Ok(()),
            code => Err(format!("Initialization failed: {}", code)),
        }
    }
}

pub fn shutdown() {
    unsafe {
        tusdz_shutdown();
    }
}

pub fn get_version() -> String {
    unsafe {
        let s = tusdz_get_version();
        if !s.is_null() {
            CStr::from_ptr(s).to_string_lossy().to_string()
        } else {
            "unknown".to_string()
        }
    }
}

pub fn load_from_file<P: AsRef<Path>>(
    filepath: P,
    options: Option<LoadOptions>,
) -> Result<Stage, String> {
    let path_str = filepath
        .as_ref()
        .to_str()
        .ok_or("Invalid path")?;
    let cpath = CString::new(path_str).map_err(|e| e.to_string())?;

    unsafe {
        let mut stage: *mut c_void = ptr::null_mut();
        let mut error_buf = vec![0u8; 1024];

        let result = tusdz_load_from_file(
            cpath.as_ptr(),
            ptr::null(),
            &mut stage,
            error_buf.as_mut_ptr() as *mut c_char,
            error_buf.len(),
        );

        if result == 0 {
            Stage::from_raw(stage).ok_or_else(|| "Failed to load stage".to_string())
        } else {
            let error_cstr = CStr::from_ptr(error_buf.as_ptr() as *const c_char);
            Err(error_cstr.to_string_lossy().to_string())
        }
    }
}

pub fn detect_format<P: AsRef<Path>>(filepath: P) -> Format {
    let path_str = filepath.as_ref().to_str().unwrap_or("");
    let cpath = CString::new(path_str).unwrap();
    unsafe {
        match tusdz_detect_format(cpath.as_ptr()) {
            1 => Format::Usda,
            2 => Format::Usdc,
            3 => Format::Usdz,
            _ => Format::Auto,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_init_shutdown() {
        assert!(init().is_ok());
        shutdown();
    }

    #[test]
    fn test_version() {
        init().ok();
        let version = get_version();
        assert!(!version.is_empty());
        shutdown();
    }
}