/// <summary>
/// TinyUSDZ C# P/Invoke Bindings
///
/// C# bindings for the TinyUSDZ C99 API using P/Invoke.
///
/// Usage:
///     TinyUSDZ.Init();
///     var stage = TinyUSDZ.LoadFromFile("model.usd");
///     var root = stage.RootPrim;
///     Console.WriteLine($"Root: {root.Name}");
///     TinyUSDZ.Shutdown();
/// </summary>

using System;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using System.IO;

public class TinyUSDZ
{
    private const string LibraryName = "tinyusdz_c";

    // ========================================================================
    // Result Codes
    // ========================================================================

    public enum ResultCode
    {
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

    // ========================================================================
    // Type Enums
    // ========================================================================

    public enum Format
    {
        Auto = 0,
        Usda = 1,
        Usdc = 2,
        Usdz = 3,
    }

    public enum PrimType
    {
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

    public enum ValueType
    {
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

    // ========================================================================
    // Load Options
    // ========================================================================

    [StructLayout(LayoutKind.Sequential)]
    public struct LoadOptions
    {
        public UIntPtr MaxMemoryLimitMb;
        public int MaxDepth;
        public int EnableComposition;
        public int StrictMode;
        public int StructureOnly;
        public IntPtr AssetResolver;
        public IntPtr AssetResolverData;

        public static LoadOptions Default => new LoadOptions
        {
            MaxMemoryLimitMb = UIntPtr.Zero,
            MaxDepth = 0,
            EnableComposition = 1,
            StrictMode = 0,
            StructureOnly = 0,
            AssetResolver = IntPtr.Zero,
            AssetResolverData = IntPtr.Zero,
        };
    }

    // ========================================================================
    // P/Invoke Declarations
    // ========================================================================

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tusdz_init();

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern void tusdz_shutdown();

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr tusdz_get_version();

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tusdz_load_from_file(
        [MarshalAs(UnmanagedType.LPStr)] string filepath,
        IntPtr options,
        out IntPtr outStage,
        IntPtr errorBuf,
        UIntPtr errorBufSize);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tusdz_load_from_memory(
        [MarshalAs(UnmanagedType.LPArray)] byte[] data,
        UIntPtr size,
        int format,
        IntPtr options,
        out IntPtr outStage,
        IntPtr errorBuf,
        UIntPtr errorBufSize);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern void tusdz_stage_free(IntPtr stage);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr tusdz_stage_get_root_prim(IntPtr stage);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr tusdz_prim_get_name(IntPtr prim);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr tusdz_prim_get_path(IntPtr prim);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tusdz_prim_get_type(IntPtr prim);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr tusdz_prim_get_type_name(IntPtr prim);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tusdz_prim_is_type(IntPtr prim, int primType);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern UIntPtr tusdz_prim_get_child_count(IntPtr prim);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr tusdz_prim_get_child_at(IntPtr prim, UIntPtr index);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern UIntPtr tusdz_prim_get_property_count(IntPtr prim);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr tusdz_prim_get_property_name_at(IntPtr prim, UIntPtr index);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr tusdz_prim_get_property(
        IntPtr prim,
        [MarshalAs(UnmanagedType.LPStr)] string name);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern void tusdz_value_free(IntPtr value);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tusdz_value_get_type(IntPtr value);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tusdz_value_is_array(IntPtr value);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern UIntPtr tusdz_value_get_array_size(IntPtr value);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tusdz_value_get_float(IntPtr value, out float outVal);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tusdz_value_get_double(IntPtr value, out double outVal);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tusdz_value_get_int(IntPtr value, out int outVal);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tusdz_value_get_string(IntPtr value, out IntPtr outStr);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tusdz_value_get_float3(IntPtr value, [Out] float[] outXyz);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tusdz_value_get_matrix4d(IntPtr value, [Out] double[] outMatrix);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tusdz_mesh_get_points(
        IntPtr mesh,
        out IntPtr outPoints,
        out UIntPtr outCount);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tusdz_mesh_get_indices(
        IntPtr mesh,
        out IntPtr outIndices,
        out UIntPtr outCount);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tusdz_stage_has_animation(IntPtr stage);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tusdz_stage_get_time_range(
        IntPtr stage,
        out double outStart,
        out double outEnd,
        out double outFps);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr tusdz_result_to_string(int result);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr tusdz_prim_type_to_string(int primType);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr tusdz_value_type_to_string(int valueType);

    // ========================================================================
    // Global Functions
    // ========================================================================

    public static void Init()
    {
        int result = tusdz_init();
        if (result != 0)
        {
            throw new Exception($"Failed to initialize TinyUSDZ: {ResultToString(result)}");
        }
    }

    public static void Shutdown()
    {
        tusdz_shutdown();
    }

    public static string GetVersion()
    {
        IntPtr ptr = tusdz_get_version();
        return Marshal.PtrToStringAnsi(ptr) ?? "unknown";
    }

    public static Stage LoadFromFile(string filepath)
    {
        int result = tusdz_load_from_file(filepath, IntPtr.Zero, out IntPtr stage, IntPtr.Zero, UIntPtr.Zero);
        if (result != 0)
        {
            throw new Exception($"Failed to load USD: {ResultToString(result)}");
        }
        return new Stage(stage);
    }

    public static Stage LoadFromMemory(byte[] data, Format format = Format.Auto)
    {
        int result = tusdz_load_from_memory(data, (UIntPtr)data.Length, (int)format, IntPtr.Zero, out IntPtr stage, IntPtr.Zero, UIntPtr.Zero);
        if (result != 0)
        {
            throw new Exception($"Failed to load USD from memory: {ResultToString(result)}");
        }
        return new Stage(stage);
    }

    public static string ResultToString(int result) => Marshal.PtrToStringAnsi(tusdz_result_to_string(result)) ?? "Unknown";
    public static string PrimTypeToString(PrimType type) => Marshal.PtrToStringAnsi(tusdz_prim_type_to_string((int)type)) ?? "Unknown";
    public static string ValueTypeToString(ValueType type) => Marshal.PtrToStringAnsi(tusdz_value_type_to_string((int)type)) ?? "Unknown";

    // ========================================================================
    // Value Wrapper
    // ========================================================================

    public class Value : IDisposable
    {
        private IntPtr _handle;
        private bool _disposed;

        internal Value(IntPtr handle)
        {
            _handle = handle;
        }

        public ValueType Type
        {
            get => (ValueType)tusdz_value_get_type(_handle);
        }

        public bool IsArray => tusdz_value_is_array(_handle) != 0;
        public UIntPtr ArraySize => tusdz_value_get_array_size(_handle);

        public float? GetFloat()
        {
            if (tusdz_value_get_float(_handle, out float val) == 0)
                return val;
            return null;
        }

        public double? GetDouble()
        {
            if (tusdz_value_get_double(_handle, out double val) == 0)
                return val;
            return null;
        }

        public int? GetInt()
        {
            if (tusdz_value_get_int(_handle, out int val) == 0)
                return val;
            return null;
        }

        public string GetString()
        {
            if (tusdz_value_get_string(_handle, out IntPtr val) == 0)
                return Marshal.PtrToStringAnsi(val) ?? "";
            return null;
        }

        public float[] GetFloat3()
        {
            float[] result = new float[3];
            if (tusdz_value_get_float3(_handle, result) == 0)
                return result;
            return null;
        }

        public double[] GetMatrix4d()
        {
            double[] result = new double[16];
            if (tusdz_value_get_matrix4d(_handle, result) == 0)
                return result;
            return null;
        }

        public void Dispose()
        {
            if (!_disposed && _handle != IntPtr.Zero)
            {
                tusdz_value_free(_handle);
                _handle = IntPtr.Zero;
                _disposed = true;
            }
            GC.SuppressFinalize(this);
        }

        ~Value()
        {
            Dispose();
        }
    }

    // ========================================================================
    // Prim Wrapper
    // ========================================================================

    public class Prim
    {
        private IntPtr _handle;

        internal Prim(IntPtr handle)
        {
            _handle = handle;
        }

        public string Name => Marshal.PtrToStringAnsi(tusdz_prim_get_name(_handle)) ?? "";
        public string Path => Marshal.PtrToStringAnsi(tusdz_prim_get_path(_handle)) ?? "";
        public PrimType Type => (PrimType)tusdz_prim_get_type(_handle);
        public string TypeName => Marshal.PtrToStringAnsi(tusdz_prim_get_type_name(_handle)) ?? "Unknown";

        public bool IsType(PrimType type) => tusdz_prim_is_type(_handle, (int)type) != 0;
        public bool IsMesh => IsType(PrimType.Mesh);
        public bool IsXform => IsType(PrimType.Xform);

        public int ChildCount => (int)tusdz_prim_get_child_count(_handle);

        public Prim GetChild(int index)
        {
            IntPtr child = tusdz_prim_get_child_at(_handle, (UIntPtr)index);
            return child != IntPtr.Zero ? new Prim(child) : null;
        }

        public IEnumerable<Prim> GetChildren()
        {
            int count = ChildCount;
            for (int i = 0; i < count; i++)
            {
                yield return GetChild(i);
            }
        }

        public int PropertyCount => (int)tusdz_prim_get_property_count(_handle);

        public string GetPropertyName(int index)
        {
            IntPtr ptr = tusdz_prim_get_property_name_at(_handle, (UIntPtr)index);
            return Marshal.PtrToStringAnsi(ptr) ?? "";
        }

        public Value GetProperty(string name)
        {
            IntPtr value = tusdz_prim_get_property(_handle, name);
            return value != IntPtr.Zero ? new Value(value) : null;
        }

        public IEnumerable<(string Name, Value Value)> GetProperties()
        {
            int count = PropertyCount;
            for (int i = 0; i < count; i++)
            {
                string name = GetPropertyName(i);
                Value value = GetProperty(name);
                if (value != null)
                    yield return (name, value);
            }
        }
    }

    // ========================================================================
    // Stage Wrapper
    // ========================================================================

    public class Stage : IDisposable
    {
        private IntPtr _handle;
        private bool _disposed;

        internal Stage(IntPtr handle)
        {
            _handle = handle;
        }

        public Prim RootPrim
        {
            get
            {
                IntPtr root = tusdz_stage_get_root_prim(_handle);
                return root != IntPtr.Zero ? new Prim(root) : null;
            }
        }

        public bool HasAnimation => tusdz_stage_has_animation(_handle) != 0;

        public (double Start, double End, double Fps)? GetTimeRange()
        {
            if (tusdz_stage_get_time_range(_handle, out double start, out double end, out double fps) == 0)
                return (start, end, fps);
            return null;
        }

        public void Dispose()
        {
            if (!_disposed && _handle != IntPtr.Zero)
            {
                tusdz_stage_free(_handle);
                _handle = IntPtr.Zero;
                _disposed = true;
            }
            GC.SuppressFinalize(this);
        }

        ~Stage()
        {
            Dispose();
        }
    }
}

// ============================================================================
// Example Usage
// ============================================================================

class Program
{
    static void Main(string[] args)
    {
        try
        {
            TinyUSDZ.Init();
            Console.WriteLine($"TinyUSDZ Version: {TinyUSDZ.GetVersion()}");

            if (args.Length > 0)
            {
                using (var stage = TinyUSDZ.LoadFromFile(args[0]))
                {
                    var root = stage.RootPrim;
                    if (root != null)
                    {
                        Console.WriteLine($"Root: {root.Name} [{root.TypeName}]");
                        Console.WriteLine($"Children: {root.ChildCount}");

                        foreach (var child in root.GetChildren())
                        {
                            Console.WriteLine($"  - {child.Name} [{child.TypeName}]");
                        }
                    }
                }
            }

            TinyUSDZ.Shutdown();
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Error: {ex.Message}");
            Environment.Exit(1);
        }
    }
}