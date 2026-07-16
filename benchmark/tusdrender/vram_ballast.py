#!/usr/bin/env python3
"""vram_ballast.py — hold N GiB of GPU device memory until killed.

Emulates a smaller-VRAM card on a big one: to test a "fits in 6 GiB" config on
a 16 GiB GPU, run `vram_ballast.py 10` in one shell (leaving ~6 GiB free) and
the render under test in another. Uses the CUDA driver API via ctypes
(libcuda.so.1) so it needs no toolkit; device memory allocated here is taken
from the same VRAM pool Vulkan allocates from.

Usage:  vram_ballast.py <GiB> [--device N]
Prints "ballast: held <bytes> ..." when the memory is pinned, then sleeps
until SIGINT/SIGTERM. Allocates in 256 MiB chunks so it can get as close to
the target as fragmentation allows; a shortfall is reported, not fatal.
"""
import ctypes
import signal
import sys
import time


def die(msg):
    print(f"ballast: {msg}", file=sys.stderr)
    sys.exit(1)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 1:
        die("usage: vram_ballast.py <GiB> [--device N]")
    target = int(float(args[0]) * (1 << 30))
    dev_index = 0
    for a in sys.argv[1:]:
        if a.startswith("--device"):
            dev_index = int(a.split("=", 1)[1]) if "=" in a else int(sys.argv[sys.argv.index(a) + 1])

    try:
        cuda = ctypes.CDLL("libcuda.so.1")
    except OSError:
        die("libcuda.so.1 not found (NVIDIA driver required)")

    def ck(rc, what):
        if rc != 0:
            die(f"{what} failed (CUresult {rc})")

    ck(cuda.cuInit(0), "cuInit")
    dev = ctypes.c_int()
    ck(cuda.cuDeviceGet(ctypes.byref(dev), dev_index), "cuDeviceGet")
    ctx = ctypes.c_void_p()
    ck(cuda.cuCtxCreate_v2(ctypes.byref(ctx), 0, dev), "cuCtxCreate")

    chunk = 256 << 20
    held = 0
    ptrs = []
    while held < target:
        want = min(chunk, target - held)
        p = ctypes.c_void_p()
        rc = cuda.cuMemAlloc_v2(ctypes.byref(p), ctypes.c_size_t(want))
        if rc != 0:
            # out of memory before reaching the target: report and hold what we got
            break
        ptrs.append(p)
        held += want
    short = target - held
    print(f"ballast: held {held} bytes ({held / (1<<30):.2f} GiB)"
          + (f", SHORT by {short / (1<<30):.2f} GiB" if short else "")
          + f" on device {dev_index}; Ctrl-C / SIGTERM to release", flush=True)

    stop = []
    signal.signal(signal.SIGINT, lambda *_: stop.append(1))
    signal.signal(signal.SIGTERM, lambda *_: stop.append(1))
    while not stop:
        time.sleep(0.5)
    # freed implicitly with the context/process
    print("ballast: released", flush=True)


if __name__ == "__main__":
    main()
