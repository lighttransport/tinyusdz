# LTE SpectralAPI Extension Proposal

> **STATUS: DRAFT PROPOSAL — NOT IMPLEMENTED.** This document specifies a *proposed* USD extension. As of this writing the `wavelength:` USD attributes described below are **not parsed or composed** by the tinyusdz reader/writer, and the standard-illuminant SPD tables do **not** exist. Partial in-memory scaffolding exists in Tydra render-data only (see [Implementation Status](#implementation-status)). Do not rely on this as a shipping feature.

## Revision History

| Version | Status | Date | Notes |
|---------|--------|------|-------|
| 0.9 | Draft (unimplemented) | 2024 | Initial proposal |

## Extension Name

`LTESpectralAPI`

## Overview

This proposal introduces spectral data support for USD, enabling physically-based rendering with wavelength-dependent material properties. The `wavelength:` namespace is reserved for all spectral attributes.

## Implementation Status

What exists in tinyusdz source today (data structures only, no USD attribute I/O):

| Item | Source | State |
|------|--------|-------|
| `SpectralData` / `SpectralIOR` / `SpectralEmission` structs + `evaluate()` | `src/tydra/render-data-shader.hh`, `render-data-pprint.cc` | In-memory render-data; linear/held/cubic interpolation and the Sellmeier IOR equation are implemented |
| `IlluminantPreset` enum (A, D50, D65, E, F1, F2, F7, F11), `WavelengthUnit` | `src/tydra/render-data-shader.hh` | Enum + string conversion only; **preset SPD tables are NOT implemented** (`SpectralEmission::evaluate()` returns a `1.0` placeholder for presets) |
| `LightAPI::spectralEmission` (`float2[] wavelength:emission`) | `src/usdLux.hh` | Struct field only; not read from / written to USD |
| `PreviewSurfaceShader::spd_reflectance`, `spd_ior` | `src/tydra/render-data-shader.hh` | Optional fields; never populated from a USD stage |
| `SpectralEmissionToJson()` | `src/tydra/materialx-to-json.cc` | Serializes the in-memory struct to JSON |

Not implemented: parsing/composition of any `wavelength:*` USD attribute or its metadata; illuminant preset SPD data; spectral textures; fluorescence; blackbody presets.

## Stage/Layer Metadata

| Metadata | Type | Default | Description |
|----------|------|---------|-------------|
| `unitForWavelength` | string | `"nanometers"` | Global unit for wavelength values in the USD Layer/Stage |

### Supported Units

- `"nanometers"` (nm) - Default, typical range [380, 780]
- `"micrometers"` (um) - Typical range [0.38, 0.78]

## Attributes

The `wavelength:` namespace is introduced for spectral data representation.

### wavelength:reflectance

| Property | Value |
|----------|-------|
| Type | `float2[]` |
| Description | Spectral reflectance as (wavelength, reflectance) pairs |

- **Wavelength range**: Typically `[380, 780]` nm (visible spectrum)
- **Reflectance range**: `[0.0, 1.0]`

**Example:**
```
float2[] wavelength:reflectance = [(450, 0.2), (550, 0.4), (650, 0.9)]
```

### wavelength:ior

| Property | Value |
|----------|-------|
| Type | `float2[]` |
| Description | Spectral index of refraction as (wavelength, IOR) pairs |

- **IOR range**: Typically `[1.0, 4.0]`
- **Fallback**: When only a scalar `ior` value exists, it is interpreted as the IOR at wavelength 550 nm (green light)

**Example:**
```
float2[] wavelength:ior = [(450, 1.52), (550, 1.50), (650, 1.48)]
```

### wavelength:emission

| Property | Value |
|----------|-------|
| Type | `float2[]` |
| Description | Spectral power distribution (SPD) for light sources as (wavelength, irradiance) pairs |

- **Wavelength range**: Typically `[380, 780]` nm (visible spectrum)
- **Irradiance unit**: `W m^-2 nm^-1` (watts per square metre per nanometre) when `unitForWavelength = "nanometers"`
- **Irradiance unit**: `W m^-2 um^-1` (watts per square metre per micrometre) when `unitForWavelength = "micrometers"`

Intended for UsdLux light primitives (DistantLight, RectLight, SphereLight, etc.) to define physically accurate spectral emission. SPD values may be given explicitly, or via the `illuminantPreset` metadata (see below).

**Example:**
```
def RectLight "SpectralLight" {
    float2[] wavelength:emission = [
        (400, 0.1), (450, 0.8), (500, 1.2), (550, 1.5),
        (600, 1.3), (650, 0.9), (700, 0.4)
    ]
}
```

## Attribute Metadata

### For wavelength:reflectance

| Metadata | Type | Description |
|----------|------|-------------|
| `unitForWavelength` | string | Per-attribute wavelength unit (overrides global setting) |

### For wavelength:ior

| Metadata | Type | Default | Description |
|----------|------|---------|-------------|
| `iorInterpolation` | string | `"linear"` | Interpolation method for IOR values |

#### Interpolation Methods

| Value | Description |
|-------|-------------|
| `"linear"` | Piecewise linear interpolation (default) |
| `"held"` | USD Held interpolation (step function) |
| `"cubic"` | Piecewise cubic interpolation (smooth) |
| `"sellmeier"` | Sellmeier equation interpolation |

**Sellmeier Interpolation:**

When `iorInterpolation = "sellmeier"`, the IOR values are interpreted as Sellmeier coefficients:

```
wavelength:ior = [(B1, C1), (B2, C2), (B3, C3)]
```

Where C1, C2, C3 have units of `[um^2]`. The Sellmeier equation:

```
n^2(lambda) = 1 + (B1 * lambda^2) / (lambda^2 - C1)
                + (B2 * lambda^2) / (lambda^2 - C2)
                + (B3 * lambda^2) / (lambda^2 - C3)
```

### For wavelength:emission

| Metadata | Type | Default | Description |
|----------|------|---------|-------------|
| `unitForWavelength` | string | `"nanometers"` | Per-attribute wavelength unit (overrides global setting) |
| `emissionInterpolation` | string | `"linear"` | Interpolation method for emission values |
| `illuminantPreset` | string | none | Standard illuminant preset name (use with empty attribute value) |

#### Standard Illuminant Presets

When `illuminantPreset` metadata is specified, the attribute value can be left empty. The renderer should use the built-in SPD data for the specified illuminant.

| Preset | Description |
|--------|-------------|
| `"a"` | CIE Standard Illuminant A (incandescent/tungsten, 2856K) |
| `"d50"` | CIE Standard Illuminant D50 (horizon daylight, 5003K) |
| `"d65"` | CIE Standard Illuminant D65 (noon daylight, 6504K) |
| `"e"` | CIE Standard Illuminant E (equal energy) |
| `"f1"` | CIE Fluorescent Illuminant F1 (daylight fluorescent) |
| `"f2"` | CIE Fluorescent Illuminant F2 (cool white fluorescent) |
| `"f7"` | CIE Fluorescent Illuminant F7 (D65 simulator) |
| `"f11"` | CIE Fluorescent Illuminant F11 (narrow-band cool white) |

**Example (using preset):**
```
def DistantLight "Sunlight" (
    float2[] wavelength:emission (
        illuminantPreset = "d65"
    )
)
{
    float2[] wavelength:emission = []
}
```

**Example (preset with intensity scale):**
```
def RectLight "StudioLight" (
    float2[] wavelength:emission (
        illuminantPreset = "d50"
    )
)
{
    float2[] wavelength:emission = []
    float inputs:intensity = 500.0
}
```

When both `illuminantPreset` and explicit SPD values are provided, the explicit values take precedence.

#### Irradiance Units by Wavelength Unit

| `unitForWavelength` | Irradiance Unit | Description |
|---------------------|-----------------|-------------|
| `"nanometers"` | W m^-2 nm^-1 | Watts per square metre per nanometre |
| `"micrometers"` | W m^-2 um^-1 | Watts per square metre per micrometre |

Interpolation methods are `linear` (default), `held`, and `cubic` (same as `wavelength:ior`, minus `sellmeier`).

### For Spectral Textures (assetInfo)

| Metadata | Type | Description |
|----------|------|-------------|
| `wavelengths` | `double[]` | Wavelength assignment for each channel/layer in multichannel textures |

Applicable to multichannel/multilayer texture formats (TIFF, EXR). This metadata is used when the image file does not contain embedded wavelength information.

**Example:**
```
asset inputs:spectralTexture = @spectral.exr@
asset inputs:spectralTexture.assetInfo = {
    double[] wavelengths = [450.0, 550.0, 650.0]
}
```

## Future Work

- Support for spectral textures using multiple single-channel images (similar to UDIM texture patterns)
- Fluorescence support (wavelength shifting materials)
- Blackbody radiation preset with color temperature parameter

## References

- [CIE Standard Illuminants](https://cie.co.at/)
- [Sellmeier Equation (Wikipedia)](https://en.wikipedia.org/wiki/Sellmeier_equation)
