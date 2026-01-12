#!/bin/bash

# Script to run batch comparisons of tusdcat vs usdcat output with detailed diffs
# Tests both USDA and USDC formats

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPARE_SCRIPT="$SCRIPT_DIR/compare-usda.js"
TUSDCAT_PATH="${TUSDCAT_PATH:-./build/tusdcat}"
USDCAT_PATH="${USDCAT_PATH:-~/local/USD/dist/bin/usdcat}"
TIMEOUT_MS="${TIMEOUT_MS:-60000}"
SHOW_DETAILED_DIFF="${SHOW_DETAILED_DIFF:-true}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print headers
print_header() {
  echo ""
  echo -e "${BLUE}════════════════════════════════════════════════════════${NC}"
  echo -e "${BLUE}$1${NC}"
  echo -e "${BLUE}════════════════════════════════════════════════════════${NC}"
  echo ""
}

# Function to print section
print_section() {
  echo ""
  echo -e "${YELLOW}→ $1${NC}"
  echo ""
}

# Function to check if file exists
check_executable() {
  local path="$1"
  local name="$2"

  # Expand ~ if present
  path="${path/#\~/$HOME}"

  if [ ! -x "$path" ]; then
    echo -e "${RED}Error: $name not found or not executable at: $path${NC}"
    return 1
  fi
  return 0
}

# Function to run comparison on a folder
run_folder_comparison() {
  local folder="$1"
  local folder_name="$2"
  local file_pattern="$3"

  if [ ! -d "$folder" ]; then
    echo -e "${RED}Error: Folder not found: $folder${NC}"
    return 1
  fi

  print_section "Testing $folder_name ($file_pattern files)"

  local detailed_flag=""
  if [ "$SHOW_DETAILED_DIFF" = "true" ]; then
    detailed_flag="--detailed-diff"
  fi

  node "$COMPARE_SCRIPT" \
    --tusdcat "$TUSDCAT_PATH" \
    --usdcat "$USDCAT_PATH" \
    --timeout "$TIMEOUT_MS" \
    --continue-on-error \
    $detailed_flag \
    "$folder/$file_pattern"
}

# Main execution
main() {
  # Expand tilde in paths
  TUSDCAT_PATH="${TUSDCAT_PATH/#\~/$HOME}"
  USDCAT_PATH="${USDCAT_PATH/#\~/$HOME}"

  print_header "USD File Format Comparison Suite"
  echo "Configuration:"
  echo "  Comparison Script: $COMPARE_SCRIPT"
  echo "  tusdcat Path: $TUSDCAT_PATH"
  echo "  usdcat Path: $USDCAT_PATH"
  echo "  Timeout: ${TIMEOUT_MS}ms"
  echo "  Detailed Diff: $SHOW_DETAILED_DIFF"
  echo ""

  # Check prerequisites
  echo "Checking prerequisites..."

  if [ ! -f "$COMPARE_SCRIPT" ]; then
    echo -e "${RED}Error: compare-usda.js not found at: $COMPARE_SCRIPT${NC}"
    exit 1
  fi

  if ! check_executable "$TUSDCAT_PATH" "tusdcat"; then
    exit 1
  fi

  if ! check_executable "$USDCAT_PATH" "usdcat"; then
    exit 1
  fi

  echo -e "${GREEN}✓ All prerequisites met${NC}"
  echo ""

  # Create results directory
  RESULTS_DIR="$SCRIPT_DIR/comparison-results"
  mkdir -p "$RESULTS_DIR"
  TIMESTAMP=$(date +%Y%m%d_%H%M%S)
  RESULTS_FILE="$RESULTS_DIR/results_${TIMESTAMP}.log"

  echo "Results will be saved to: $RESULTS_FILE"
  echo ""

  # Run comparisons and capture output
  {
    print_header "USD File Format Comparison Results - $TIMESTAMP"

    # Test USDA files
    print_section "USDA (ASCII) Format Tests"
    echo "Testing all .usda files in tests/usda directory..."
    echo ""
    run_folder_comparison "$SCRIPT_DIR/usda" "USDA Files" "*.usda" || true

    # Test USDC files
    print_section "USDC (Binary/Crate) Format Tests"
    echo "Testing all .usdc files in tests/usdc directory..."
    echo ""
    run_folder_comparison "$SCRIPT_DIR/usdc" "USDC Files" "*.usdc" || true

    print_header "Comparison Complete"
  } | tee "$RESULTS_FILE"

  echo ""
  echo -e "${GREEN}✓ Comparison complete!${NC}"
  echo "Full results saved to: $RESULTS_FILE"
  echo ""
  echo "Usage examples for custom runs:"
  echo "  # Test specific USDA file with detailed diff"
  echo "  SHOW_DETAILED_DIFF=true node $COMPARE_SCRIPT --detailed-diff tests/usda/cube.usda"
  echo ""
  echo "  # Test specific files with tusdcat/usdcat"
  echo "  TUSDCAT_PATH=./build_gcc/tusdcat USDCAT_PATH=~/local/USD/dist/bin/usdcat \\"
  echo "    node $COMPARE_SCRIPT --detailed-diff --tusdcat ./build_gcc/tusdcat --usdcat ~/local/USD/dist/bin/usdcat tests/usda/cube.usda"
  echo ""
}

# Show help
show_help() {
  cat << EOF
Usage: $0 [OPTIONS]

Run comprehensive batch comparisons of tusdcat vs usdcat outputs with detailed diffs.
Tests both USDA (ASCII) and USDC (Binary/Crate) file formats.

OPTIONS:
  -h, --help              Show this help message
  --tusdcat PATH          Path to tusdcat executable (default: ./build_gcc/tusdcat)
  --usdcat PATH           Path to usdcat executable (default: ~/local/USD/dist/bin/usdcat)
  --timeout MS            Timeout per file in milliseconds (default: 60000)
  --no-detailed-diff      Disable detailed diff output (shows summary only)

ENVIRONMENT VARIABLES:
  TUSDCAT_PATH            Override tusdcat path
  USDCAT_PATH             Override usdcat path
  TIMEOUT_MS              Override timeout
  SHOW_DETAILED_DIFF      Set to 'false' to disable detailed diffs (default: true)

EXAMPLES:
  # Run with default settings
  $0

  # Run without detailed diffs (faster, summary only)
  $0 --no-detailed-diff

  # Run with custom tool paths
  TUSDCAT_PATH=./build_asan/tusdcat USDCAT_PATH=~/USD/bin/usdcat $0

  # Run with longer timeout for slow systems
  $0 --timeout 120000

EOF
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
  case $1 in
    -h|--help)
      show_help
      exit 0
      ;;
    --tusdcat)
      TUSDCAT_PATH="$2"
      shift 2
      ;;
    --usdcat)
      USDCAT_PATH="$2"
      shift 2
      ;;
    --timeout)
      TIMEOUT_MS="$2"
      shift 2
      ;;
    --no-detailed-diff)
      SHOW_DETAILED_DIFF="false"
      shift
      ;;
    *)
      echo "Unknown option: $1"
      show_help
      exit 1
      ;;
  esac
done

# Run main function
main
