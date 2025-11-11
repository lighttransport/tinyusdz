#!/bin/bash
# SPDX-License-Identifier: Apache 2.0
#
# Quick script to install dependencies using uv

set -e

echo "Installing dependencies with uv..."

# Check if uv is available
if ! command -v uv &> /dev/null; then
    echo "Error: uv not found"
    echo
    echo "Install uv with one of these methods:"
    echo "  curl -LsSf https://astral.sh/uv/install.sh | sh"
    echo "  pip install uv"
    echo "  cargo install uv"
    exit 1
fi

echo "Using: $(uv --version)"

# If .venv doesn't exist, create it
if [ ! -d ".venv" ]; then
    echo "Creating virtual environment..."
    uv venv .venv
fi

# Install packages
echo "Installing numpy..."
uv pip install numpy

echo "Installing build tools..."
uv pip install setuptools wheel

echo
echo "✓ Dependencies installed!"
echo
echo "Activate the environment with:"
echo "  source .venv/bin/activate"
