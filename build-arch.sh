#!/bin/bash
# build-arch.sh - Build script for Arch Linux
#
# Dependencies (install with pacman):
#   sudo pacman -S wlroots0.19 wayland wayland-protocols qt6-base qt6-declarative \
#                  libxkbcommon pixman cmake ninja pkgconf

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

echo "=== wlroots-qt-compositor Build Script ==="
echo ""

# Check for required packages
check_package() {
    if ! pacman -Qi "$1" &>/dev/null; then
        echo "Missing package: $1"
        return 1
    fi
    return 0
}

echo "Checking dependencies..."
MISSING=0

# Core dependencies
for pkg in wlroots0.19 wayland wayland-protocols qt6-base qt6-declarative \
           libxkbcommon pixman cmake ninja pkgconf; do
    if ! check_package "$pkg"; then
        MISSING=1
    fi
done

if [ $MISSING -eq 1 ]; then
    echo ""
    echo "Install missing packages with:"
    echo "  sudo pacman -S wlroots0.19 wayland wayland-protocols qt6-base qt6-declarative \\"
    echo "                 libxkbcommon pixman cmake ninja pkgconf"
    echo ""
    read -p "Install now? [y/N] " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        sudo pacman -S wlroots0.19 wayland wayland-protocols qt6-base qt6-declarative \
                       libxkbcommon pixman cmake ninja pkgconf
    else
        echo "Aborting."
        exit 1
    fi
fi

echo "All dependencies found."
echo ""

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
echo "Configuring with CMake..."
cmake -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    "$SCRIPT_DIR"

# Build
echo ""
echo "Building..."
ninja

echo ""
echo "=== Build complete! ==="
echo ""
echo "Binary: ${BUILD_DIR}/wlroots-qt-compositor"
echo ""
echo "To run:"
echo "  ./run-nested.sh"
echo ""
echo "Or manually:"
echo "  cd ${BUILD_DIR}"
echo "  ./wlroots-qt-compositor"
