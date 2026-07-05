#!/usr/bin/env python3
"""Coarse, driver-tolerant image fingerprint for the usd-assets smoke harness.

The usd-assets render harness classifies each asset into buckets (rendered,
no_renderable, ...). "rendered" only means "non-blank", so it cannot catch a
render that turned the wrong color, lost geometry, or changed silhouette. This
helper adds a curated-golden layer: it reduces an image to a small quantized
grid so exact per-pixel differences (anti-aliasing, minor driver variation) are
absorbed, while gross regressions (blank, color shift, missing geometry) change
the fingerprint.

Fingerprints are therefore a per-machine baseline: stable enough to catch
regressions when re-run on the same GPU/driver, not portable across GPUs. The
harness gates golden checking behind an explicit opt-in for that reason.

Usage:
  asset_fingerprint.py hash <image>            -> prints hex fingerprint (exit 0)
                                                  exit 2 if the image can't be read
  asset_fingerprint.py compare <a> <b> <tol>   -> exit 0 match / 1 mismatch
                                                  prints the L1 distance

Supports binary PPM (P6, maxval 255) natively; PNG/other via Pillow if present.
"""

import sys

GRID = 12       # GRID x GRID cells
LEVELS = 16     # per-channel quantization (4 bits / nibble)


def _read_ppm(path):
    """Return (w, h, rgb-bytes) for a binary P6 PPM, or None."""
    try:
        data = open(path, "rb").read()
    except OSError:
        return None
    if not data.startswith(b"P6"):
        return None
    i = 2
    tokens = []
    while len(tokens) < 3 and i < len(data):
        c = data[i]
        if c == 35:  # '#comment'
            while i < len(data) and data[i] not in (10, 13):
                i += 1
        elif chr(c).isspace():
            i += 1
        else:
            start = i
            while i < len(data) and not chr(data[i]).isspace():
                i += 1
            tokens.append(data[start:i])
    if len(tokens) != 3:
        return None
    # Exactly one whitespace byte separates the maxval from pixel data.
    if i < len(data) and chr(data[i]).isspace():
        i += 1
    try:
        w, h, maxv = (int(t) for t in tokens)
    except ValueError:
        return None
    if maxv != 255 or w <= 0 or h <= 0:
        return None
    px = data[i:i + w * h * 3]
    if len(px) < w * h * 3:
        return None
    return w, h, px


def _read_via_pillow(path):
    try:
        from PIL import Image
    except Exception:
        return None
    try:
        im = Image.open(path).convert("RGB")
    except Exception:
        return None
    w, h = im.size
    return w, h, im.tobytes()


def fingerprint(path):
    """Reduce an image to a GRID x GRID x 3 quantized-nibble hex string."""
    got = _read_ppm(path)
    if got is None:
        got = _read_via_pillow(path)
    if got is None:
        return None
    w, h, px = got
    if w <= 0 or h <= 0:
        return None

    # Average each grid cell, then quantize each channel to LEVELS.
    nibbles = []
    for gy in range(GRID):
        y0 = (gy * h) // GRID
        y1 = max(y0 + 1, ((gy + 1) * h) // GRID)
        for gx in range(GRID):
            x0 = (gx * w) // GRID
            x1 = max(x0 + 1, ((gx + 1) * w) // GRID)
            sr = sg = sb = n = 0
            for y in range(y0, y1):
                row = (y * w + x0) * 3
                for _ in range(x0, x1):
                    sr += px[row]
                    sg += px[row + 1]
                    sb += px[row + 2]
                    row += 3
                    n += 1
            if n == 0:
                n = 1
            for s in (sr, sg, sb):
                avg = s // n                       # 0..255
                q = (avg * LEVELS) // 256           # 0..LEVELS-1
                if q >= LEVELS:
                    q = LEVELS - 1
                nibbles.append(q)
    return "".join("%x" % q for q in nibbles)


def l1_distance(a, b):
    """Sum of absolute per-nibble differences. -1 if lengths differ."""
    if len(a) != len(b):
        return -1
    dist = 0
    for ca, cb in zip(a, b):
        dist += abs(int(ca, 16) - int(cb, 16))
    return dist


COVERAGE_FG_THRESHOLD = 40  # per-cell L1 vs background to count as foreground


def _cell_averages(w, h, px):
    """Return a GRID*GRID list of (r,g,b) cell averages."""
    cells = []
    for gy in range(GRID):
        y0 = (gy * h) // GRID
        y1 = max(y0 + 1, ((gy + 1) * h) // GRID)
        for gx in range(GRID):
            x0 = (gx * w) // GRID
            x1 = max(x0 + 1, ((gx + 1) * w) // GRID)
            sr = sg = sb = n = 0
            for y in range(y0, y1):
                row = (y * w + x0) * 3
                for _ in range(x0, x1):
                    sr += px[row]; sg += px[row + 1]; sb += px[row + 2]
                    row += 3; n += 1
            if n == 0:
                n = 1
            cells.append((sr // n, sg // n, sb // n))
    return cells


def coverage(path):
    """Brightness-invariant silhouette: a GRID*GRID foreground/background mask
    packed as a hex bitfield. A cell is foreground when its average color
    differs from the estimated background (the median border-cell color) by more
    than COVERAGE_FG_THRESHOLD. Robust to cross-backend shading/lighting
    differences while still catching geometry that is present in one backend and
    missing (or differently placed) in another."""
    got = _read_ppm(path) or _read_via_pillow(path)
    if got is None:
        return None
    w, h, px = got
    if w <= 0 or h <= 0:
        return None
    cells = _cell_averages(w, h, px)

    # Background estimate: median of the border cells (top/bottom rows + side
    # columns), which are usually background.
    border = []
    for i in range(GRID):
        border.append(cells[i])                         # top row
        border.append(cells[(GRID - 1) * GRID + i])     # bottom row
        border.append(cells[i * GRID])                  # left column
        border.append(cells[i * GRID + GRID - 1])       # right column
    bg = tuple(sorted(c[k] for c in border)[len(border) // 2] for k in range(3))

    bits = []
    for (r, g, b) in cells:
        fg = abs(r - bg[0]) + abs(g - bg[1]) + abs(b - bg[2]) > COVERAGE_FG_THRESHOLD
        bits.append(1 if fg else 0)
    # Pack bits into hex nibbles (GRID*GRID bits -> that/4 hex chars).
    out = []
    for i in range(0, len(bits), 4):
        nib = 0
        for j in range(4):
            if i + j < len(bits) and bits[i + j]:
                nib |= 1 << j
        out.append("%x" % nib)
    return "".join(out)


def hamming_distance(a, b):
    """Number of differing bits between two hex bitfields. -1 if lengths differ."""
    if len(a) != len(b):
        return -1
    dist = 0
    for ca, cb in zip(a, b):
        x = int(ca, 16) ^ int(cb, 16)
        dist += bin(x).count("1")
    return dist


def main(argv):
    if len(argv) >= 3 and argv[1] == "hash":
        fp = fingerprint(argv[2])
        if fp is None:
            sys.stderr.write("cannot read image: %s\n" % argv[2])
            return 2
        sys.stdout.write(fp + "\n")
        return 0
    if len(argv) >= 5 and argv[1] == "compare":
        a, b, tol = argv[2], argv[3], int(argv[4])
        d = l1_distance(a, b)
        sys.stdout.write("%d\n" % d)
        if d < 0:
            return 1  # different fingerprint shapes -> mismatch
        return 0 if d <= tol else 1
    if len(argv) >= 3 and argv[1] == "coverage":
        cov = coverage(argv[2])
        if cov is None:
            sys.stderr.write("cannot read image: %s\n" % argv[2])
            return 2
        sys.stdout.write(cov + "\n")
        return 0
    if len(argv) >= 5 and argv[1] == "coverage-compare":
        a, b, tol = argv[2], argv[3], int(argv[4])
        d = hamming_distance(a, b)
        sys.stdout.write("%d\n" % d)
        if d < 0:
            return 1
        return 0 if d <= tol else 1
    sys.stderr.write(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
