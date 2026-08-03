#!/usr/bin/env python3
"""Decode two PNGs and report the fraction of differing 3D-viewport pixels.

Compares only the render viewport. Three bands of UI chrome are excluded,
because each of them legitimately differs between two runs and would otherwise
show up as a rendering difference:

  left pane   file list -- differs whenever the two renders are of files in
              different directories
  toolbar     shows the current directory, same problem
  status bar  prints live process RSS, which differs run to run

usage: pngdiff.py <a.png> <b.png> [viewport_x_fraction] [status_bar_px]
                  [toolbar_px]
"""
import sys, zlib, struct

def load(path):
    with open(path, 'rb') as f:
        data = f.read()
    pos, idat, w, h, bpp = 8, b'', 0, 0, 4
    while pos < len(data):
        ln = struct.unpack('>I', data[pos:pos+4])[0]
        typ = data[pos+4:pos+8]
        body = data[pos+8:pos+8+ln]
        if typ == b'IHDR':
            w, h, depth, color = struct.unpack('>IIBB', body[:10])
            if depth != 8:
                raise SystemExit('unsupported bit depth')
            bpp = {0: 1, 2: 3, 4: 2, 6: 4}.get(color, 0)
            if bpp == 0:
                raise SystemExit('unsupported color type')
        elif typ == b'IDAT':
            idat += body
        elif typ == b'IEND':
            break
        pos += 12 + ln
    raw = zlib.decompress(idat)
    stride = w * bpp
    prev = bytearray(stride)
    rows = []
    i = 0
    for _ in range(h):
        ft = raw[i]; i += 1
        line = bytearray(raw[i:i+stride]); i += stride
        if ft == 1:
            for x in range(bpp, stride): line[x] = (line[x] + line[x-bpp]) & 0xFF
        elif ft == 2:
            for x in range(stride): line[x] = (line[x] + prev[x]) & 0xFF
        elif ft == 3:
            for x in range(stride):
                a = line[x-bpp] if x >= bpp else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 0xFF
        elif ft == 4:
            for x in range(stride):
                a = line[x-bpp] if x >= bpp else 0
                b = prev[x]
                c = prev[x-bpp] if x >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 0xFF
        rows.append(bytes(line)); prev = line
    return w, h, bpp, rows

wa, ha, ba, ra = load(sys.argv[1])
wb, hb, bb, rb = load(sys.argv[2])
if (wa, ha) != (wb, hb):
    raise SystemExit('size mismatch')
x_frac = float(sys.argv[3]) if len(sys.argv) > 3 else 0.55
status_px = int(sys.argv[4]) if len(sys.argv) > 4 else 30
# Theme toolbar_h is 26; round up so a caption descender cannot leak in.
toolbar_px = int(sys.argv[5]) if len(sys.argv) > 5 else 28
x0 = int(wa * x_frac)
y0 = min(toolbar_px, max(0, ha - 1))
y1 = max(y0 + 1, ha - status_px)
diff = total = 0
for y in range(y0, y1, 2):
    A, B = ra[y], rb[y]
    for x in range(x0, wa, 2):
        oa, ob = x * ba, x * bb
        total += 1
        if abs(A[oa]-B[ob]) + abs(A[oa+1]-B[ob+1]) + abs(A[oa+2]-B[ob+2]) > 12:
            diff += 1
print(f'{diff/max(total,1):.4f}')
