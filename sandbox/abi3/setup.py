#!/usr/bin/env python3
"""
SPDX-License-Identifier: Apache 2.0

Setup script for TinyUSDZ Python ABI3 binding

This builds a Python extension module using the stable ABI (limited API)
for Python 3.10+. The resulting wheel is compatible with all Python versions
3.10 and later without recompilation.

Usage:
    python setup.py build_ext --inplace
    python setup.py bdist_wheel
"""

import os
import sys
import glob
from pathlib import Path

try:
    from setuptools import setup, Extension
    from setuptools.command.build_ext import build_ext
except ImportError:
    print("Error: setuptools is required. Install with: pip install setuptools")
    sys.exit(1)


class BuildExt(build_ext):
    """Custom build extension to set ABI3 flags"""

    def build_extensions(self):
        # Set C++17 standard
        if self.compiler.compiler_type == 'unix':
            for ext in self.extensions:
                ext.extra_compile_args.append('-std=c++17')
                # Enable ABI3 limited API
                ext.define_macros.append(('Py_LIMITED_API', '0x030a0000'))
        elif self.compiler.compiler_type == 'msvc':
            for ext in self.extensions:
                ext.extra_compile_args.append('/std:c++17')
                # Enable ABI3 limited API
                ext.define_macros.append(('Py_LIMITED_API', '0x030a0000'))

        super().build_extensions()


# Paths
root_dir = Path(__file__).parent.resolve()
tinyusdz_root = root_dir.parent.parent
src_dir = tinyusdz_root / "src"

# TinyUSDZ C++ sources (minimal set for basic functionality)
tinyusdz_sources = [
    str(src_dir / "c-tinyusd.cc"),
    str(src_dir / "tinyusdz.cc"),
    str(src_dir / "stage.cc"),
    str(src_dir / "prim-types.cc"),
    str(src_dir / "value-types.cc"),
    str(src_dir / "usda-reader.cc"),
    str(src_dir / "usdc-reader.cc"),
    str(src_dir / "ascii-parser.cc"),
    str(src_dir / "crate-reader.cc"),
    str(src_dir / "io-util.cc"),
    str(src_dir / "pprinter.cc"),
    str(src_dir / "prim-reconstruct.cc"),
    str(src_dir / "path-util.cc"),
    str(src_dir / "str-util.cc"),
    str(src_dir / "value-pprint.cc"),
]

# Include directories
include_dirs = [
    str(root_dir / "include"),
    str(src_dir),
    str(src_dir / "external"),
]

# Define macros
define_macros = [
    ('Py_LIMITED_API', '0x030a0000'),
    ('TINYUSDZ_PRODUCTION_BUILD', '1'),
]

# Extension module
ext_modules = [
    Extension(
        name='tinyusdz_abi3',
        sources=['src/tinyusdz_abi3.c'] + tinyusdz_sources,
        include_dirs=include_dirs,
        define_macros=define_macros,
        py_limited_api=True,  # Enable stable ABI
        language='c++',
    )
]

# Read README if available
long_description = ""
readme_path = root_dir / "README.md"
if readme_path.exists():
    long_description = readme_path.read_text(encoding='utf-8')

setup(
    name='tinyusdz-abi3',
    version='0.1.0',
    author='TinyUSDZ Contributors',
    author_email='',
    description='TinyUSDZ Python bindings using stable ABI (Python 3.10+)',
    long_description=long_description,
    long_description_content_type='text/markdown',
    url='https://github.com/syoyo/tinyusdz',
    license='Apache-2.0',
    classifiers=[
        'Development Status :: 3 - Alpha',
        'Intended Audience :: Developers',
        'License :: OSI Approved :: Apache Software License',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.10',
        'Programming Language :: Python :: 3.11',
        'Programming Language :: Python :: 3.12',
        'Programming Language :: Python :: 3.13',
        'Programming Language :: C',
        'Programming Language :: C++',
        'Topic :: Software Development :: Libraries',
        'Topic :: Multimedia :: Graphics :: 3D Modeling',
    ],
    python_requires='>=3.10',
    ext_modules=ext_modules,
    cmdclass={'build_ext': BuildExt},
    zip_safe=False,
    options={
        'bdist_wheel': {
            'py_limited_api': 'cp310',  # Compatible with Python 3.10+
        }
    },
)
