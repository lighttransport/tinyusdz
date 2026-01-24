#!/bin/bash
#
# MaterialX Texture Downloader
# Downloads texture images from MaterialX GitHub repository
#
# Usage:
#   ./download_textures.sh              # Download all textures
#   ./download_textures.sh --minimal    # Download only textures for simple materials
#   ./download_textures.sh --clean      # Remove all downloaded textures
#

set -e

# Configuration
MATERIALX_REPO="https://raw.githubusercontent.com/AcademySoftwareFoundation/MaterialX/main"
IMAGES_BASE="${MATERIALX_REPO}/resources/Images"
IMAGES_DIR="./images"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Helper functions
print_header() {
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}  MaterialX Texture Downloader${NC}"
    echo -e "${BLUE}========================================${NC}"
}

print_success() {
    echo -e "${GREEN}✓${NC} $1"
}

print_error() {
    echo -e "${RED}✗${NC} $1"
}

print_info() {
    echo -e "${YELLOW}→${NC} $1"
}

# Download a single file
download_file() {
    local file="$1"
    local url="${IMAGES_BASE}/${file}"
    local output="${IMAGES_DIR}/${file}"
    local dir=$(dirname "$output")

    # Create directory if it doesn't exist
    mkdir -p "$dir"

    # Download file
    if curl -sfL "$url" -o "$output"; then
        print_success "Downloaded: $file"
        return 0
    else
        print_error "Failed: $file (file may not exist in repo)"
        return 1
    fi
}

# Clean all downloaded textures
clean_textures() {
    print_header
    print_info "Cleaning downloaded textures..."

    if [ -d "$IMAGES_DIR" ]; then
        rm -rf "$IMAGES_DIR"
        print_success "Removed $IMAGES_DIR directory"
    else
        print_info "No textures to clean"
    fi

    echo ""
    echo "Done!"
}

# Download minimal set (for simple materials)
download_minimal() {
    print_header
    print_info "Downloading minimal texture set..."
    echo ""

    local files=(
        "brass_color.jpg"
        "brass_roughness.jpg"
        "wood_color.jpg"
        "wood_roughness.jpg"
        "greysphere_calibration.png"
    )

    local success=0
    local failed=0

    for file in "${files[@]}"; do
        if download_file "$file"; then
            success=$((success + 1))
        else
            failed=$((failed + 1))
        fi
    done

    echo ""
    echo -e "${GREEN}Downloaded: $success files${NC}"
    if [ $failed -gt 0 ]; then
        echo -e "${RED}Failed: $failed files${NC}"
    fi
}

# Download all textures
download_all() {
    print_header
    print_info "Downloading all textures..."
    echo ""

    # Brass materials
    print_info "Brass materials..."
    download_file "brass_color.jpg"
    download_file "brass_roughness.jpg"

    echo ""

    # Brick materials
    print_info "Brick materials..."
    download_file "brick_base_gray.jpg"
    download_file "brick_dirt_mask.jpg"
    download_file "brick_mask.jpg"
    download_file "brick_normal.jpg"
    download_file "brick_roughness.jpg"
    download_file "brick_variation_mask.jpg"

    echo ""

    # Wood materials
    print_info "Wood materials..."
    download_file "wood_color.jpg"
    download_file "wood_roughness.jpg"

    echo ""

    # Calibration
    print_info "Calibration..."
    download_file "greysphere_calibration.png"

    echo ""

    # Chess set (large number of textures)
    print_info "Chess set materials (this will take a while)..."

    local chess_files=(
        "chess_set/bishop_black_base_color.jpg"
        "chess_set/bishop_black_normal.jpg"
        "chess_set/bishop_black_roughness.jpg"
        "chess_set/bishop_shared_metallic.jpg"
        "chess_set/bishop_white_base_color.jpg"
        "chess_set/bishop_white_normal.jpg"
        "chess_set/bishop_white_roughness.jpg"
        "chess_set/castle_black_base_color.jpg"
        "chess_set/castle_shared_metallic.jpg"
        "chess_set/castle_shared_normal.jpg"
        "chess_set/castle_shared_roughness.jpg"
        "chess_set/castle_white_base_color.jpg"
        "chess_set/chessboard_base_color.jpg"
        "chess_set/chessboard_metallic.jpg"
        "chess_set/chessboard_normal.jpg"
        "chess_set/chessboard_roughness.jpg"
        "chess_set/king_black_base_color.jpg"
        "chess_set/king_black_normal.jpg"
        "chess_set/king_black_roughness.jpg"
        "chess_set/king_shared_metallic.jpg"
        "chess_set/king_shared_scattering.jpg"
        "chess_set/king_white_base_color.jpg"
        "chess_set/king_white_normal.jpg"
        "chess_set/king_white_roughness.jpg"
        "chess_set/knight_black_base_color.jpg"
        "chess_set/knight_black_normal.jpg"
        "chess_set/knight_black_roughness.jpg"
        "chess_set/knight_white_base_color.jpg"
        "chess_set/knight_white_normal.jpg"
        "chess_set/knight_white_roughness.jpg"
        "chess_set/pawn_black_base_color.jpg"
        "chess_set/pawn_shared_metallic.jpg"
        "chess_set/pawn_shared_normal.jpg"
        "chess_set/pawn_shared_roughness.jpg"
        "chess_set/pawn_white_base_color.jpg"
        "chess_set/queen_black_base_color.jpg"
        "chess_set/queen_black_normal.jpg"
        "chess_set/queen_black_roughness.jpg"
        "chess_set/queen_shared_metallic.jpg"
        "chess_set/queen_shared_scattering.jpg"
        "chess_set/queen_white_base_color.jpg"
        "chess_set/queen_white_normal.jpg"
        "chess_set/queen_white_roughness.jpg"
    )

    local success=0
    local failed=0

    for file in "${chess_files[@]}"; do
        if download_file "$file"; then
            success=$((success + 1))
        else
            failed=$((failed + 1))
        fi
    done

    echo ""
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}Download Complete!${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo ""
    echo "Statistics:"
    echo "  Successfully downloaded: $success files"
    if [ $failed -gt 0 ]; then
        echo "  Failed: $failed files"
    fi
    echo ""
    echo "Images are located in: $IMAGES_DIR"
    echo ""
    echo "Disk usage:"
    du -sh "$IMAGES_DIR" 2>/dev/null || echo "  <not calculated>"
}

# Show usage
show_usage() {
    echo "MaterialX Texture Downloader"
    echo ""
    echo "Usage:"
    echo "  $0                Download all textures (~50+ files)"
    echo "  $0 --minimal      Download minimal set (5 files for simple materials)"
    echo "  $0 --clean        Remove all downloaded textures"
    echo "  $0 --help         Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                # Download everything"
    echo "  $0 --minimal      # Just brass, wood, calibration textures"
    echo "  $0 --clean        # Remove images/ directory"
    echo ""
    echo "Notes:"
    echo "  - Images are downloaded to: $IMAGES_DIR"
    echo "  - Source: MaterialX GitHub repository"
    echo "  - The .mtlx files reference '../../../Images/' which resolves to this directory"
    echo "  - Some files may fail if they don't exist in the MaterialX repo"
    echo ""
}

# Main script
main() {
    case "${1:-}" in
        --clean)
            clean_textures
            ;;
        --minimal)
            download_minimal
            ;;
        --help|-h)
            show_usage
            ;;
        "")
            download_all
            ;;
        *)
            echo "Unknown option: $1"
            echo ""
            show_usage
            exit 1
            ;;
    esac
}

# Run main
main "$@"
