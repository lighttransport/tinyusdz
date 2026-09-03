#!/usr/bin/env python3
"""Regression checks for byte-level USDZ package validation."""

import pathlib
import struct
import subprocess
import sys
import tempfile
import zlib


def stored_package(name: str, payload: bytes, aligned: bool) -> bytes:
    name_bytes = name.encode("utf-8")
    base = 30 + len(name_bytes)
    extra_size = ((-base) % 64) if aligned else 0
    header = struct.pack(
        "<IHHHHHIIIHH",
        0x04034B50,
        20,
        0,
        0,
        0,
        0,
        zlib.crc32(payload),
        len(payload),
        len(payload),
        len(name_bytes),
        extra_size,
    )
    return header + name_bytes + (b"\0" * extra_size) + payload


def complete_package(
    name: str,
    payload: bytes,
    *,
    crc_override=None,
    central_name=None,
) -> bytes:
    name_bytes = name.encode("utf-8")
    shown_name = (central_name or name).encode("utf-8")
    base = 30 + len(name_bytes)
    extra_size = (-base) % 64
    crc = zlib.crc32(payload) if crc_override is None else crc_override
    local = struct.pack(
        "<IHHHHHIIIHH",
        0x04034B50,
        20,
        0,
        0,
        0,
        0,
        crc,
        len(payload),
        len(payload),
        len(name_bytes),
        extra_size,
    ) + name_bytes + (b"\0" * extra_size) + payload
    central = struct.pack(
        "<IHHHHHHIIIHHHHHII",
        0x02014B50,
        20,
        20,
        0,
        0,
        0,
        0,
        crc,
        len(payload),
        len(payload),
        len(shown_name),
        0,
        0,
        0,
        0,
        0,
        0,
    ) + shown_name
    eocd = struct.pack(
        "<IHHHHIIH", 0x06054B50, 0, 0, 1, 1, len(central), len(local), 0
    )
    return local + central + eocd


def expect_rule(checker: str, package: pathlib.Path, rule: str) -> None:
    run = subprocess.run(
        [checker, "--groups", "package", str(package)],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if run.returncode != 1 or rule not in run.stdout:
        raise RuntimeError(
            f"expected exit 1 and {rule!r}, got {run.returncode}:\n{run.stdout}"
        )


def main() -> int:
    checker = sys.argv[1]
    with tempfile.TemporaryDirectory(prefix="lusdchecker-package-") as tmp:
        root = pathlib.Path(tmp)
        unaligned = root / "unaligned.usdz"
        unaligned.write_bytes(stored_package("root.usda", b"#usda 1.0\n", False))
        expect_rule(checker, unaligned, "package.entry.alignment")

        missing = root / "missing.usdz"
        source = b'#usda 1.0\ndef Xform "Root" (references = @missing.usda@) {}\n'
        missing.write_bytes(stored_package("root.usda", source, True))
        expect_rule(checker, missing, "package.dependency.missing")

        bad_crc = root / "bad-crc.usdz"
        bad_crc.write_bytes(
            complete_package("root.usda", b"#usda 1.0\n", crc_override=0)
        )
        expect_rule(checker, bad_crc, "package.entry.crc")

        bad_directory = root / "bad-directory.usdz"
        bad_directory.write_bytes(
            complete_package(
                "root.usda", b"#usda 1.0\n", central_name="other.usda"
            )
        )
        expect_rule(
            checker, bad_directory, "package.centralDirectory.mismatch"
        )

        nested_dependency = root / "nested-dependency.usdz"
        nested_source = (
            b"#usda 1.0\n( customLayerData = { dictionary nested = "
            b"{ asset texture = @textures/missing.png@ } } )\n"
        )
        nested_dependency.write_bytes(
            complete_package("root.usda", nested_source)
        )
        expect_rule(
            checker, nested_dependency, "package.dependency.missing"
        )

        valid = root / "valid.usdz"
        valid.write_bytes(complete_package("root.usda", b"#usda 1.0\n"))
        run = subprocess.run(
            [checker, "--groups", "package", str(valid)],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        if run.returncode != 0:
            raise RuntimeError(f"valid package was rejected:\n{run.stdout.decode()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
