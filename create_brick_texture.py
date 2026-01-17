#!/usr/bin/env python3
"""Generate a simple 64x64 brick texture BMP"""
import struct

def create_brick_bmp(filename, width=64, height=64):
    # BMP header
    file_size = 54 + (width * height * 3)
    pixel_data_offset = 54
    dib_header_size = 40

    # BMP file header (14 bytes)
    bmp_header = struct.pack('<2sIHHI',
        b'BM',              # Signature
        file_size,          # File size
        0,                  # Reserved
        0,                  # Reserved
        pixel_data_offset   # Pixel data offset
    )

    # DIB header (BITMAPINFOHEADER - 40 bytes)
    dib_header = struct.pack('<IiiHHIIiiII',
        dib_header_size,    # Header size
        width,              # Width
        height,             # Height
        1,                  # Planes
        24,                 # Bits per pixel
        0,                  # Compression (none)
        0,                  # Image size (can be 0 for uncompressed)
        2835,               # X pixels per meter
        2835,               # Y pixels per meter
        0,                  # Colors in palette
        0                   # Important colors
    )

    # Create brick pattern
    pixel_data = bytearray()

    # Brick colors (BGR format for BMP)
    brick_red = (40, 60, 180)      # Red brick
    mortar_gray = (200, 200, 200)  # Gray mortar

    brick_height = 8
    brick_width = 16
    mortar_width = 2

    for y in range(height):
        row_data = bytearray()

        # Determine if this is a mortar row
        is_mortar_row = (y % (brick_height + mortar_width)) >= brick_height

        # Offset every other row of bricks
        offset = 0
        if ((y // (brick_height + mortar_width)) % 2) == 1:
            offset = brick_width // 2

        for x in range(width):
            x_offset = (x + offset) % width

            # Determine if this is a mortar column
            is_mortar_col = (x_offset % (brick_width + mortar_width)) >= brick_width

            if is_mortar_row or is_mortar_col:
                # Mortar
                row_data.extend(mortar_gray)
            else:
                # Brick
                row_data.extend(brick_red)

        # BMP rows are stored bottom-to-top
        pixel_data = row_data + pixel_data

    # Write BMP file
    with open(filename, 'wb') as f:
        f.write(bmp_header)
        f.write(dib_header)
        f.write(pixel_data)

if __name__ == '__main__':
    create_brick_bmp('models/textures/brick.bmp')
    print("Created brick.bmp (64x64)")
