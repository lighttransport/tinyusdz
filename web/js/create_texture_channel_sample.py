#!/usr/bin/env python3
"""
Create texture channel extraction sample without external dependencies.
Generates synthetic PNG textures and a USDA file with MaterialX node graphs.

Usage:
    python3 create_texture_channel_sample.py

Output:
    assets/texture_channel_sample.usda
    assets/textures/channel_test_rgb.png
    assets/textures/channel_mask.png
"""

import os
import math
import zlib
import struct
from pathlib import Path

# Output paths
SCRIPT_DIR = Path(__file__).parent
ASSETS_DIR = SCRIPT_DIR / "assets"
TEXTURES_DIR = ASSETS_DIR / "textures"
OUTPUT_FILE = ASSETS_DIR / "texture_channel_sample.usda"

def ensure_dirs():
    """Create output directories."""
    ASSETS_DIR.mkdir(exist_ok=True)
    TEXTURES_DIR.mkdir(exist_ok=True)

def create_png(width, height, pixels, filename):
    """
    Create a PNG file from pixel data using pure Python.
    pixels is a list of (r, g, b) tuples with values 0-255.
    """
    def make_chunk(chunk_type, data):
        chunk = chunk_type + data
        return struct.pack('>I', len(data)) + chunk + struct.pack('>I', zlib.crc32(chunk) & 0xffffffff)

    # PNG signature
    signature = b'\x89PNG\r\n\x1a\n'

    # IHDR chunk
    ihdr_data = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)  # 8-bit RGB
    ihdr = make_chunk(b'IHDR', ihdr_data)

    # IDAT chunk (image data)
    raw_data = b''
    for y in range(height):
        raw_data += b'\x00'  # Filter byte (none)
        for x in range(width):
            idx = y * width + x
            r, g, b = pixels[idx]
            raw_data += bytes([r, g, b])

    compressed = zlib.compress(raw_data, 9)
    idat = make_chunk(b'IDAT', compressed)

    # IEND chunk
    iend = make_chunk(b'IEND', b'')

    # Write PNG
    with open(filename, 'wb') as f:
        f.write(signature + ihdr + idat + iend)

def create_synthetic_rgb_texture(width=256, height=256):
    """
    Create RGB texture: R=horizontal gradient, G=vertical gradient, B=radial gradient.
    """
    pixels = []

    for y in range(height):
        for x in range(width):
            u = x / (width - 1)
            v = y / (height - 1)

            # R: Horizontal gradient
            r = int(u * 255)

            # G: Vertical gradient (flip Y)
            g = int((1 - v) * 255)

            # B: Radial gradient from center
            cx, cy = 0.5, 0.5
            dist = math.sqrt((u - cx)**2 + (v - cy)**2)
            b = int((1.0 - min(dist * 2, 1.0)) * 255)

            pixels.append((r, g, b))

    output_path = TEXTURES_DIR / "channel_test_rgb.png"
    create_png(width, height, pixels, str(output_path))
    print(f"Created: {output_path}")
    return "textures/channel_test_rgb.png"

def create_synthetic_mask_texture(width=256, height=256):
    """
    Create mask texture: R=checkerboard, G=stripes, B=smooth pattern.
    """
    pixels = []

    for y in range(height):
        for x in range(width):
            # R: Checkerboard (32x32 cells)
            cell_size = 32
            checker = ((x // cell_size) + (y // cell_size)) % 2
            r = checker * 255

            # G: Horizontal stripes
            stripe_size = 16
            g = ((y // stripe_size) % 2) * 255

            # B: Smooth pattern
            b = int((math.sin(x * 0.1) * math.cos(y * 0.1) + 1) * 0.5 * 255)

            pixels.append((r, g, b))

    output_path = TEXTURES_DIR / "channel_mask.png"
    create_png(width, height, pixels, str(output_path))
    print(f"Created: {output_path}")
    return "textures/channel_mask.png"

def create_usda_file(rgb_texture_path, mask_texture_path):
    """Create USDA file with MaterialX node graphs for channel extraction."""

    usda_content = f'''#usda 1.0
(
    defaultPrim = "World"
    metersPerUnit = 1
    upAxis = "Y"
)

def Xform "World"
{{
    # =========================================================================
    # Material 1: Channel Extraction (R->roughness, G->metallic, B->color mix)
    # =========================================================================
    def Sphere "Sphere_ChannelExtraction" (
        prepend apiSchemas = ["MaterialBindingAPI"]
    )
    {{
        rel material:binding = </World/Materials/ChannelExtraction>
        double3 xformOp:translate = (-3, 0, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
        double radius = 0.8
    }}

    # =========================================================================
    # Material 2: Channel Swizzle (RGB -> BGR)
    # =========================================================================
    def Sphere "Sphere_Swizzle" (
        prepend apiSchemas = ["MaterialBindingAPI"]
    )
    {{
        rel material:binding = </World/Materials/ChannelSwizzle>
        double3 xformOp:translate = (-1.5, 0, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
        double radius = 0.8
    }}

    # =========================================================================
    # Material 3: Grayscale from R channel
    # =========================================================================
    def Sphere "Sphere_Grayscale" (
        prepend apiSchemas = ["MaterialBindingAPI"]
    )
    {{
        rel material:binding = </World/Materials/GrayscaleFromR>
        double3 xformOp:translate = (0, 0, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
        double radius = 0.8
    }}

    # =========================================================================
    # Material 4: Single Channel Modification (invert G)
    # =========================================================================
    def Sphere "Sphere_SingleMod" (
        prepend apiSchemas = ["MaterialBindingAPI"]
    )
    {{
        rel material:binding = </World/Materials/SingleChannelMod>
        double3 xformOp:translate = (1.5, 0, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
        double radius = 0.8
    }}

    # =========================================================================
    # Material 5: Mask-based Blending
    # =========================================================================
    def Sphere "Sphere_MaskBlend" (
        prepend apiSchemas = ["MaterialBindingAPI"]
    )
    {{
        rel material:binding = </World/Materials/MaskBlend>
        double3 xformOp:translate = (3, 0, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
        double radius = 0.8
    }}

    # =========================================================================
    # Materials Scope
    # =========================================================================
    def Scope "Materials"
    {{
        # ---------------------------------------------------------------------
        # Material 1: Channel Extraction
        # Demonstrates extracting RGB channels for different material properties
        # R -> roughness, G -> metalness, B -> mix factor for color
        # ---------------------------------------------------------------------
        def Material "ChannelExtraction"
        {{
            token outputs:mtlx:surface.connect = </World/Materials/ChannelExtraction/mtlx_surface.outputs:out>

            def Shader "mtlx_surface"
            {{
                uniform token info:id = "ND_standard_surface_surfaceshader"
                color3f inputs:base_color.connect = </World/Materials/ChannelExtraction/NG_extraction/mix_color.outputs:out>
                float inputs:base = 1.0
                float inputs:metalness.connect = </World/Materials/ChannelExtraction/NG_extraction/extract_g.outputs:out>
                float inputs:specular_roughness.connect = </World/Materials/ChannelExtraction/NG_extraction/extract_r.outputs:out>
                token outputs:out
            }}

            def NodeGraph "NG_extraction"
            {{
                def Shader "texcoord"
                {{
                    uniform token info:id = "ND_texcoord_vector2"
                    int inputs:index = 0
                    vector2f outputs:out
                }}

                def Shader "image_rgb"
                {{
                    uniform token info:id = "ND_image_color3"
                    asset inputs:file = @{rgb_texture_path}@
                    vector2f inputs:texcoord.connect = </World/Materials/ChannelExtraction/NG_extraction/texcoord.outputs:out>
                    color3f outputs:out
                }}

                def Shader "extract_r"
                {{
                    uniform token info:id = "ND_extract_color3"
                    color3f inputs:in.connect = </World/Materials/ChannelExtraction/NG_extraction/image_rgb.outputs:out>
                    int inputs:index = 0
                    float outputs:out
                }}

                def Shader "extract_g"
                {{
                    uniform token info:id = "ND_extract_color3"
                    color3f inputs:in.connect = </World/Materials/ChannelExtraction/NG_extraction/image_rgb.outputs:out>
                    int inputs:index = 1
                    float outputs:out
                }}

                def Shader "extract_b"
                {{
                    uniform token info:id = "ND_extract_color3"
                    color3f inputs:in.connect = </World/Materials/ChannelExtraction/NG_extraction/image_rgb.outputs:out>
                    int inputs:index = 2
                    float outputs:out
                }}

                def Shader "color1"
                {{
                    uniform token info:id = "ND_constant_color3"
                    color3f inputs:value = (0.8, 0.4, 0.2)
                    color3f outputs:out
                }}

                def Shader "color2"
                {{
                    uniform token info:id = "ND_constant_color3"
                    color3f inputs:value = (0.2, 0.5, 0.9)
                    color3f outputs:out
                }}

                def Shader "mix_color"
                {{
                    uniform token info:id = "ND_mix_color3"
                    color3f inputs:bg.connect = </World/Materials/ChannelExtraction/NG_extraction/color1.outputs:out>
                    color3f inputs:fg.connect = </World/Materials/ChannelExtraction/NG_extraction/color2.outputs:out>
                    float inputs:mix.connect = </World/Materials/ChannelExtraction/NG_extraction/extract_b.outputs:out>
                    color3f outputs:out
                }}
            }}
        }}

        # ---------------------------------------------------------------------
        # Material 2: Channel Swizzle (RGB -> BGR)
        # ---------------------------------------------------------------------
        def Material "ChannelSwizzle"
        {{
            token outputs:mtlx:surface.connect = </World/Materials/ChannelSwizzle/mtlx_surface.outputs:out>

            def Shader "mtlx_surface"
            {{
                uniform token info:id = "ND_standard_surface_surfaceshader"
                color3f inputs:base_color.connect = </World/Materials/ChannelSwizzle/NG_swizzle/combine_bgr.outputs:out>
                float inputs:base = 1.0
                float inputs:specular_roughness = 0.4
                token outputs:out
            }}

            def NodeGraph "NG_swizzle"
            {{
                def Shader "texcoord"
                {{
                    uniform token info:id = "ND_texcoord_vector2"
                    int inputs:index = 0
                    vector2f outputs:out
                }}

                def Shader "image_rgb"
                {{
                    uniform token info:id = "ND_image_color3"
                    asset inputs:file = @{rgb_texture_path}@
                    vector2f inputs:texcoord.connect = </World/Materials/ChannelSwizzle/NG_swizzle/texcoord.outputs:out>
                    color3f outputs:out
                }}

                def Shader "extract_r"
                {{
                    uniform token info:id = "ND_extract_color3"
                    color3f inputs:in.connect = </World/Materials/ChannelSwizzle/NG_swizzle/image_rgb.outputs:out>
                    int inputs:index = 0
                    float outputs:out
                }}

                def Shader "extract_g"
                {{
                    uniform token info:id = "ND_extract_color3"
                    color3f inputs:in.connect = </World/Materials/ChannelSwizzle/NG_swizzle/image_rgb.outputs:out>
                    int inputs:index = 1
                    float outputs:out
                }}

                def Shader "extract_b"
                {{
                    uniform token info:id = "ND_extract_color3"
                    color3f inputs:in.connect = </World/Materials/ChannelSwizzle/NG_swizzle/image_rgb.outputs:out>
                    int inputs:index = 2
                    float outputs:out
                }}

                def Shader "combine_bgr"
                {{
                    uniform token info:id = "ND_combine3_color3"
                    float inputs:in1.connect = </World/Materials/ChannelSwizzle/NG_swizzle/extract_b.outputs:out>
                    float inputs:in2.connect = </World/Materials/ChannelSwizzle/NG_swizzle/extract_g.outputs:out>
                    float inputs:in3.connect = </World/Materials/ChannelSwizzle/NG_swizzle/extract_r.outputs:out>
                    color3f outputs:out
                }}
            }}
        }}

        # ---------------------------------------------------------------------
        # Material 3: Grayscale from R channel
        # ---------------------------------------------------------------------
        def Material "GrayscaleFromR"
        {{
            token outputs:mtlx:surface.connect = </World/Materials/GrayscaleFromR/mtlx_surface.outputs:out>

            def Shader "mtlx_surface"
            {{
                uniform token info:id = "ND_standard_surface_surfaceshader"
                color3f inputs:base_color.connect = </World/Materials/GrayscaleFromR/NG_grayscale/combine_rrr.outputs:out>
                float inputs:base = 1.0
                float inputs:specular_roughness = 0.5
                token outputs:out
            }}

            def NodeGraph "NG_grayscale"
            {{
                def Shader "texcoord"
                {{
                    uniform token info:id = "ND_texcoord_vector2"
                    int inputs:index = 0
                    vector2f outputs:out
                }}

                def Shader "image_rgb"
                {{
                    uniform token info:id = "ND_image_color3"
                    asset inputs:file = @{rgb_texture_path}@
                    vector2f inputs:texcoord.connect = </World/Materials/GrayscaleFromR/NG_grayscale/texcoord.outputs:out>
                    color3f outputs:out
                }}

                def Shader "extract_r"
                {{
                    uniform token info:id = "ND_extract_color3"
                    color3f inputs:in.connect = </World/Materials/GrayscaleFromR/NG_grayscale/image_rgb.outputs:out>
                    int inputs:index = 0
                    float outputs:out
                }}

                def Shader "combine_rrr"
                {{
                    uniform token info:id = "ND_combine3_color3"
                    float inputs:in1.connect = </World/Materials/GrayscaleFromR/NG_grayscale/extract_r.outputs:out>
                    float inputs:in2.connect = </World/Materials/GrayscaleFromR/NG_grayscale/extract_r.outputs:out>
                    float inputs:in3.connect = </World/Materials/GrayscaleFromR/NG_grayscale/extract_r.outputs:out>
                    color3f outputs:out
                }}
            }}
        }}

        # ---------------------------------------------------------------------
        # Material 4: Single Channel Modification (invert G only)
        # ---------------------------------------------------------------------
        def Material "SingleChannelMod"
        {{
            token outputs:mtlx:surface.connect = </World/Materials/SingleChannelMod/mtlx_surface.outputs:out>

            def Shader "mtlx_surface"
            {{
                uniform token info:id = "ND_standard_surface_surfaceshader"
                color3f inputs:base_color.connect = </World/Materials/SingleChannelMod/NG_single_mod/combine_modified.outputs:out>
                float inputs:base = 1.0
                float inputs:specular_roughness = 0.3
                token outputs:out
            }}

            def NodeGraph "NG_single_mod"
            {{
                def Shader "texcoord"
                {{
                    uniform token info:id = "ND_texcoord_vector2"
                    int inputs:index = 0
                    vector2f outputs:out
                }}

                def Shader "image_rgb"
                {{
                    uniform token info:id = "ND_image_color3"
                    asset inputs:file = @{rgb_texture_path}@
                    vector2f inputs:texcoord.connect = </World/Materials/SingleChannelMod/NG_single_mod/texcoord.outputs:out>
                    color3f outputs:out
                }}

                def Shader "extract_r"
                {{
                    uniform token info:id = "ND_extract_color3"
                    color3f inputs:in.connect = </World/Materials/SingleChannelMod/NG_single_mod/image_rgb.outputs:out>
                    int inputs:index = 0
                    float outputs:out
                }}

                def Shader "extract_g"
                {{
                    uniform token info:id = "ND_extract_color3"
                    color3f inputs:in.connect = </World/Materials/SingleChannelMod/NG_single_mod/image_rgb.outputs:out>
                    int inputs:index = 1
                    float outputs:out
                }}

                def Shader "extract_b"
                {{
                    uniform token info:id = "ND_extract_color3"
                    color3f inputs:in.connect = </World/Materials/SingleChannelMod/NG_single_mod/image_rgb.outputs:out>
                    int inputs:index = 2
                    float outputs:out
                }}

                def Shader "const_one"
                {{
                    uniform token info:id = "ND_constant_float"
                    float inputs:value = 1.0
                    float outputs:out
                }}

                def Shader "invert_g"
                {{
                    uniform token info:id = "ND_subtract_float"
                    float inputs:in1.connect = </World/Materials/SingleChannelMod/NG_single_mod/const_one.outputs:out>
                    float inputs:in2.connect = </World/Materials/SingleChannelMod/NG_single_mod/extract_g.outputs:out>
                    float outputs:out
                }}

                def Shader "combine_modified"
                {{
                    uniform token info:id = "ND_combine3_color3"
                    float inputs:in1.connect = </World/Materials/SingleChannelMod/NG_single_mod/extract_r.outputs:out>
                    float inputs:in2.connect = </World/Materials/SingleChannelMod/NG_single_mod/invert_g.outputs:out>
                    float inputs:in3.connect = </World/Materials/SingleChannelMod/NG_single_mod/extract_b.outputs:out>
                    color3f outputs:out
                }}
            }}
        }}

        # ---------------------------------------------------------------------
        # Material 5: Mask-based Blending
        # Uses mask texture channels to control blending
        # R (checkerboard) -> mix red/green, G (stripes) -> mix with texture
        # B (smooth) -> roughness
        # ---------------------------------------------------------------------
        def Material "MaskBlend"
        {{
            token outputs:mtlx:surface.connect = </World/Materials/MaskBlend/mtlx_surface.outputs:out>

            def Shader "mtlx_surface"
            {{
                uniform token info:id = "ND_standard_surface_surfaceshader"
                color3f inputs:base_color.connect = </World/Materials/MaskBlend/NG_mask_blend/final_mix.outputs:out>
                float inputs:base = 1.0
                float inputs:specular_roughness.connect = </World/Materials/MaskBlend/NG_mask_blend/extract_mask_b.outputs:out>
                token outputs:out
            }}

            def NodeGraph "NG_mask_blend"
            {{
                def Shader "texcoord"
                {{
                    uniform token info:id = "ND_texcoord_vector2"
                    int inputs:index = 0
                    vector2f outputs:out
                }}

                def Shader "image_color"
                {{
                    uniform token info:id = "ND_image_color3"
                    asset inputs:file = @{rgb_texture_path}@
                    vector2f inputs:texcoord.connect = </World/Materials/MaskBlend/NG_mask_blend/texcoord.outputs:out>
                    color3f outputs:out
                }}

                def Shader "image_mask"
                {{
                    uniform token info:id = "ND_image_color3"
                    asset inputs:file = @{mask_texture_path}@
                    vector2f inputs:texcoord.connect = </World/Materials/MaskBlend/NG_mask_blend/texcoord.outputs:out>
                    color3f outputs:out
                }}

                def Shader "extract_mask_r"
                {{
                    uniform token info:id = "ND_extract_color3"
                    color3f inputs:in.connect = </World/Materials/MaskBlend/NG_mask_blend/image_mask.outputs:out>
                    int inputs:index = 0
                    float outputs:out
                }}

                def Shader "extract_mask_g"
                {{
                    uniform token info:id = "ND_extract_color3"
                    color3f inputs:in.connect = </World/Materials/MaskBlend/NG_mask_blend/image_mask.outputs:out>
                    int inputs:index = 1
                    float outputs:out
                }}

                def Shader "extract_mask_b"
                {{
                    uniform token info:id = "ND_extract_color3"
                    color3f inputs:in.connect = </World/Materials/MaskBlend/NG_mask_blend/image_mask.outputs:out>
                    int inputs:index = 2
                    float outputs:out
                }}

                def Shader "color_red"
                {{
                    uniform token info:id = "ND_constant_color3"
                    color3f inputs:value = (1.0, 0.2, 0.2)
                    color3f outputs:out
                }}

                def Shader "color_green"
                {{
                    uniform token info:id = "ND_constant_color3"
                    color3f inputs:value = (0.2, 1.0, 0.2)
                    color3f outputs:out
                }}

                def Shader "mix1"
                {{
                    uniform token info:id = "ND_mix_color3"
                    color3f inputs:bg.connect = </World/Materials/MaskBlend/NG_mask_blend/color_red.outputs:out>
                    color3f inputs:fg.connect = </World/Materials/MaskBlend/NG_mask_blend/color_green.outputs:out>
                    float inputs:mix.connect = </World/Materials/MaskBlend/NG_mask_blend/extract_mask_r.outputs:out>
                    color3f outputs:out
                }}

                def Shader "final_mix"
                {{
                    uniform token info:id = "ND_mix_color3"
                    color3f inputs:bg.connect = </World/Materials/MaskBlend/NG_mask_blend/mix1.outputs:out>
                    color3f inputs:fg.connect = </World/Materials/MaskBlend/NG_mask_blend/image_color.outputs:out>
                    float inputs:mix.connect = </World/Materials/MaskBlend/NG_mask_blend/extract_mask_g.outputs:out>
                    color3f outputs:out
                }}
            }}
        }}
    }}
}}
'''

    with open(OUTPUT_FILE, 'w') as f:
        f.write(usda_content)

    print(f"Created: {OUTPUT_FILE}")

def main():
    print("Creating texture channel extraction sample...")

    ensure_dirs()

    # Create synthetic textures
    print("Creating synthetic textures...")
    rgb_path = create_synthetic_rgb_texture(256, 256)
    mask_path = create_synthetic_mask_texture(256, 256)

    # Create USDA file
    print("Creating USDA file with MaterialX node graphs...")
    create_usda_file(rgb_path, mask_path)

    print("\nDone!")
    print(f"\nOutput files:")
    print(f"  - {OUTPUT_FILE}")
    print(f"  - {TEXTURES_DIR / 'channel_test_rgb.png'}")
    print(f"  - {TEXTURES_DIR / 'channel_mask.png'}")
    print(f"\nMaterials in the USDA file:")
    print(f"  1. ChannelExtraction - R->roughness, G->metallic, B->color mix")
    print(f"  2. ChannelSwizzle - RGB -> BGR swap")
    print(f"  3. GrayscaleFromR - R channel to grayscale")
    print(f"  4. SingleChannelMod - Invert G channel only")
    print(f"  5. MaskBlend - Multi-mask texture blending")

if __name__ == "__main__":
    main()
