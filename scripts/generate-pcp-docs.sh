#!/bin/bash

################################################################################
# PCP API Documentation Generation Script
#
# Generates HTML documentation from markdown source using md2html.py converter
#
# Usage: ./generate-pcp-docs.sh [options]
#
# Options:
#   -i, --input FILE      Input markdown file (default: ../doc/pcp.md)
#   -o, --output FILE     Output HTML file (default: ../doc/pcp.html)
#   -t, --title TITLE     HTML document title
#   -h, --help            Show this help message
#   -v, --verbose         Enable verbose output
#   -w, --watch           Watch for changes and regenerate (requires inotify-tools)
#
# Examples:
#   ./generate-pcp-docs.sh
#   ./generate-pcp-docs.sh -i ../doc/pcp.md -o ../doc/pcp.html
#   ./generate-pcp-docs.sh --watch
#
################################################################################

set -o pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Default values
INPUT_FILE="${PROJECT_ROOT}/doc/pcp.md"
OUTPUT_FILE="${PROJECT_ROOT}/doc/static/pcp.html"
TITLE="PCP API Documentation"
VERBOSE=0
WATCH_MODE=0

# Functions
print_help() {
    cat << 'EOF'
PCP API Documentation Generation Script

Usage: generate-pcp-docs.sh [options]

Options:
  -i, --input FILE      Input markdown file (default: doc/pcp.md)
  -o, --output FILE     Output HTML file (default: doc/pcp.html)
  -t, --title TITLE     HTML document title (default: "PCP API Documentation")
  -h, --help            Show this help message
  -v, --verbose         Enable verbose output
  -w, --watch           Watch for changes and regenerate

Examples:
  generate-pcp-docs.sh
  generate-pcp-docs.sh -i doc/pcp.md -o doc/pcp.html
  generate-pcp-docs.sh --watch --verbose

EOF
}

print_error() {
    echo -e "${RED}✗ Error: $1${NC}" >&2
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

print_info() {
    echo -e "${BLUE}ℹ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠ $1${NC}"
}

check_python() {
    if ! command -v python3 &> /dev/null; then
        print_error "Python3 is not installed or not in PATH"
        return 1
    fi

    if [ "$VERBOSE" -eq 1 ]; then
        local version=$(python3 --version 2>&1)
        print_info "Found $version"
    fi
    return 0
}

check_converter() {
    if [ ! -f "$SCRIPT_DIR/md2html.py" ]; then
        print_error "md2html.py not found in $SCRIPT_DIR"
        return 1
    fi

    if [ "$VERBOSE" -eq 1 ]; then
        print_info "Found md2html.py"
    fi
    return 0
}

check_input_file() {
    if [ ! -f "$INPUT_FILE" ]; then
        print_error "Input file not found: $INPUT_FILE"
        return 1
    fi

    if [ "$VERBOSE" -eq 1 ]; then
        local size=$(du -h "$INPUT_FILE" | cut -f1)
        local lines=$(wc -l < "$INPUT_FILE")
        print_info "Input: $INPUT_FILE ($size, $lines lines)"
    fi
    return 0
}

generate_docs() {
    print_info "Generating HTML documentation..."

    if [ "$VERBOSE" -eq 1 ]; then
        print_info "Input:  $INPUT_FILE"
        print_info "Output: $OUTPUT_FILE"
        print_info "Title:  $TITLE"
        print_info "Running: python3 $SCRIPT_DIR/md2html.py '$INPUT_FILE' '$OUTPUT_FILE'"
    fi

    # Run the converter
    if python3 "$SCRIPT_DIR/md2html.py" "$INPUT_FILE" "$OUTPUT_FILE"; then
        # Check if output file was created
        if [ ! -f "$OUTPUT_FILE" ]; then
            print_error "Output file was not created: $OUTPUT_FILE"
            return 1
        fi

        # Verify HTML structure
        if ! grep -q "<!DOCTYPE html>" "$OUTPUT_FILE"; then
            print_error "Invalid HTML structure in output file"
            return 1
        fi

        if ! grep -q "</html>" "$OUTPUT_FILE"; then
            print_error "Incomplete HTML structure in output file"
            return 1
        fi

        # Get file stats
        local size=$(du -h "$OUTPUT_FILE" | cut -f1)
        local lines=$(wc -l < "$OUTPUT_FILE")

        print_success "Documentation generated successfully"
        print_info "Output: $OUTPUT_FILE ($size, $lines lines)"
        return 0
    else
        print_error "Failed to generate HTML documentation"
        return 1
    fi
}

watch_for_changes() {
    if ! command -v inotifywait &> /dev/null; then
        print_error "inotify-tools not installed. Install with:"
        echo "  Ubuntu/Debian: sudo apt-get install inotify-tools"
        echo "  macOS: brew install fswatch"
        return 1
    fi

    print_info "Watching for changes in $INPUT_FILE"
    print_info "Press Ctrl+C to stop..."

    # Use different tool based on OS
    if [ "$(uname)" == "Darwin" ]; then
        # macOS - use fswatch
        if ! command -v fswatch &> /dev/null; then
            print_warning "fswatch not found on macOS. Install with: brew install fswatch"
            return 1
        fi
        fswatch -o "$INPUT_FILE" | while read; do
            echo ""
            generate_docs
            echo ""
        done
    else
        # Linux - use inotifywait
        inotifywait -m -e modify "$INPUT_FILE" | while read; do
            echo ""
            generate_docs
            echo ""
        done
    fi
}

validate_output() {
    if [ "$VERBOSE" -eq 0 ]; then
        return 0
    fi

    print_info "Validating output..."

    local warnings=0

    # Check for common issues
    if ! grep -q "<nav class=\"toc\">" "$OUTPUT_FILE"; then
        print_warning "Table of contents not found"
        ((warnings++))
    fi

    if ! grep -q "<main class=\"content\">" "$OUTPUT_FILE"; then
        print_warning "Main content container not found"
        ((warnings++))
    fi

    if ! grep -q "<style>" "$OUTPUT_FILE"; then
        print_warning "CSS styles not embedded"
        ((warnings++))
    fi

    if grep -q "<script>" "$OUTPUT_FILE"; then
        print_warning "JavaScript found (not recommended for offline documentation)"
        ((warnings++))
    fi

    # Count sections
    local sections=$(grep -c "^<h[1-6]" "$OUTPUT_FILE" || echo "0")
    print_info "Found $sections sections"

    # Count code blocks
    local code_blocks=$(grep -c "<pre><code" "$OUTPUT_FILE" || echo "0")
    print_info "Found $code_blocks code blocks"

    if [ "$warnings" -gt 0 ]; then
        print_warning "Validation completed with $warnings warnings"
    else
        print_success "Validation passed"
    fi
}

open_browser() {
    if command -v xdg-open &> /dev/null; then
        # Linux
        xdg-open "$OUTPUT_FILE"
    elif command -v open &> /dev/null; then
        # macOS
        open "$OUTPUT_FILE"
    elif command -v start &> /dev/null; then
        # Windows
        start "$OUTPUT_FILE"
    else
        print_warning "Could not automatically open file. Open manually: $OUTPUT_FILE"
        return 1
    fi
}

# Parse command-line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -i|--input)
            INPUT_FILE="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT_FILE="$2"
            shift 2
            ;;
        -t|--title)
            TITLE="$2"
            shift 2
            ;;
        -v|--verbose)
            VERBOSE=1
            shift
            ;;
        -w|--watch)
            WATCH_MODE=1
            shift
            ;;
        -h|--help)
            print_help
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            print_help
            exit 1
            ;;
    esac
done

# Convert relative paths to absolute
if [[ ! "$INPUT_FILE" = /* ]]; then
    INPUT_FILE="$(cd "$(dirname "$INPUT_FILE")" && pwd)/$(basename "$INPUT_FILE")"
fi

if [[ ! "$OUTPUT_FILE" = /* ]]; then
    OUTPUT_FILE="$(cd "$(dirname "$OUTPUT_FILE")" && pwd)/$(basename "$OUTPUT_FILE")"
fi

# Main execution
print_info "PCP API Documentation Generator"
echo ""

# Check prerequisites
check_python || exit 1
check_converter || exit 1
check_input_file || exit 1

echo ""

# Generate documentation
if [ "$WATCH_MODE" -eq 1 ]; then
    generate_docs || exit 1
    echo ""
    watch_for_changes
else
    generate_docs || exit 1
    validate_output

    echo ""
    read -p "Open documentation in browser? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        open_browser || true
    fi
fi

exit 0
