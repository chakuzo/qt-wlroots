#!/bin/bash
# run-nested.sh - Run the compositor in nested mode
#
# This script sets up the environment and runs the compositor.
# The compositor will create its own window in your existing desktop.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
BINARY="${BUILD_DIR}/wlroots-qt-compositor"

# Check if built
if [ ! -x "$BINARY" ]; then
    echo "Compositor not built. Run ./build-arch.sh first."
    exit 1
fi

# Check for graphical environment
if [ -z "$WAYLAND_DISPLAY" ] && [ -z "$DISPLAY" ]; then
    echo "Error: No graphical environment detected."
    echo "This compositor must run inside Wayland or X11."
    exit 1
fi

echo "=== Running wlroots-qt-compositor ==="
echo ""

if [ -n "$WAYLAND_DISPLAY" ]; then
    echo "Running in Wayland nested mode (parent: $WAYLAND_DISPLAY)"
    # Force Wayland backend for wlroots
    export WLR_BACKENDS=wayland
else
    echo "Running in X11 nested mode (parent: $DISPLAY)"
    # Force X11 backend for wlroots  
    export WLR_BACKENDS=x11
fi

# Enable wlroots debug logging
export WLR_DEBUG=1

# Run the compositor
cd "$BUILD_DIR"
exec ./wlroots-qt-compositor "$@"
