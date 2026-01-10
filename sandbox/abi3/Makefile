# SPDX-License-Identifier: Apache 2.0
#
# Makefile for TinyUSDZ ABI3 binding

.PHONY: all build install clean test examples env help

PYTHON := python3
UV := uv

all: build

# Create virtual environment with uv
env:
	@echo "Creating virtual environment with uv..."
	$(UV) venv .venv
	@echo "Installing dependencies..."
	$(UV) pip install numpy setuptools wheel
	@echo ""
	@echo "✓ Environment ready!"
	@echo ""
	@echo "Activate with: source .venv/bin/activate"

# Install dependencies only
deps:
	@if [ ! -d ".venv" ]; then \
		echo "Creating virtual environment..."; \
		$(UV) venv .venv; \
	fi
	@echo "Installing dependencies..."
	$(UV) pip install numpy setuptools wheel

# Build extension module in-place
build:
	@echo "Building extension module..."
	$(PYTHON) setup.py build_ext --inplace
	@echo "✓ Build complete"

# Build wheel for distribution
wheel:
	@echo "Building wheel..."
	$(PYTHON) setup.py bdist_wheel
	@echo "✓ Wheel created in dist/"

# Install in development mode
install:
	@echo "Installing in development mode..."
	$(PYTHON) setup.py develop

# Run tests
test: build
	@echo "Running tests..."
	$(PYTHON) tests/test_basic.py

# Run examples
examples: build
	@echo "Running basic example..."
	$(PYTHON) examples/example_basic.py
	@echo ""
	@echo "Running numpy example..."
	$(PYTHON) examples/example_numpy.py

# Run mesh example with test file
mesh-example: build
	@if [ -f "../../models/suzanne.usdc" ]; then \
		echo "Running mesh example with suzanne.usdc..."; \
		$(PYTHON) examples/example_mesh_to_numpy.py ../../models/suzanne.usdc; \
	elif [ -f "../../models/cube.usda" ]; then \
		echo "Running mesh example with cube.usda..."; \
		$(PYTHON) examples/example_mesh_to_numpy.py ../../models/cube.usda; \
	else \
		echo "Running mesh example with synthetic data..."; \
		$(PYTHON) examples/example_mesh_to_numpy.py; \
	fi

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -rf build dist *.egg-info
	rm -f *.so *.pyd
	find . -type d -name __pycache__ -exec rm -rf {} + 2>/dev/null || true
	find . -type f -name "*.pyc" -delete 2>/dev/null || true
	@echo "✓ Clean complete"

# Clean everything including venv
distclean: clean
	@echo "Removing virtual environment..."
	rm -rf .venv
	@echo "✓ Complete clean"

# Help
help:
	@echo "TinyUSDZ ABI3 Binding - Available targets:"
	@echo ""
	@echo "  make env           - Create virtual environment with uv"
	@echo "  make deps          - Install dependencies with uv"
	@echo "  make build         - Build extension module"
	@echo "  make wheel         - Build wheel for distribution"
	@echo "  make install       - Install in development mode"
	@echo "  make test          - Run tests"
	@echo "  make examples      - Run example scripts"
	@echo "  make mesh-example  - Run mesh example with test data"
	@echo "  make clean         - Remove build artifacts"
	@echo "  make distclean     - Remove everything including venv"
	@echo "  make help          - Show this help"
	@echo ""
	@echo "Quick start:"
	@echo "  1. make env        # Create environment and install deps"
	@echo "  2. source .venv/bin/activate"
	@echo "  3. make build      # Build the module"
	@echo "  4. make test       # Run tests"
	@echo ""
	@echo "Or use the convenience scripts:"
	@echo "  ./setup_env.sh     # Complete setup and build"
	@echo "  ./build.sh setup   # Just build"
