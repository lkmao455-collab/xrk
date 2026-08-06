#!/bin/bash
# XRK Build Script for macOS
# Supports Apple Silicon (arm64) and Intel (x86_64)
# Usage: ./build_mac.sh [--clean] [--release|--debug] [--universal] [--arch arm64|x86_64]

set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default values
BUILD_DIR="build_macos"
BUILD_TYPE="Release"
TARGET="xrk"
CLEAN=0
UNIVERSAL=0
ARCH=""

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --clean)
            CLEAN=1
            shift
            ;;
        --release)
            BUILD_TYPE="Release"
            shift
            ;;
        --debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        --universal)
            UNIVERSAL=1
            shift
            ;;
        --arch)
            ARCH="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [options]"
            echo "Options:"
            echo "  --clean       Clean build directory before building"
            echo "  --release     Build in Release mode (default)"
            echo "  --debug       Build in Debug mode"
            echo "  --universal   Build universal binary (arm64 + x86_64)"
            echo "  --arch ARCH   Target architecture: arm64 or x86_64"
            echo "  -h, --help    Show this help message"
            exit 0
            ;;
        *)
            echo -e "${RED}[ERROR] Unknown option: $1${NC}"
            exit 1
            ;;
    esac
done

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  XRK macOS Build Script${NC}"
echo -e "${BLUE}========================================${NC}"

# Detect host architecture
HOST_ARCH=$(uname -m)
echo -e "${BLUE}[INFO] Host architecture: ${HOST_ARCH}${NC}"

# Determine target architecture
if [[ -n "$ARCH" ]]; then
    TARGET_ARCH="$ARCH"
    echo -e "${BLUE}[INFO] Using specified architecture: ${TARGET_ARCH}${NC}"
elif [[ $UNIVERSAL -eq 1 ]]; then
    TARGET_ARCH="arm64;x86_64"
    echo -e "${BLUE}[INFO] Building universal binary (arm64 + x86_64)${NC}"
elif [[ "$HOST_ARCH" == "arm64" ]]; then
    TARGET_ARCH="arm64"
    echo -e "${BLUE}[INFO] Detected Apple Silicon, building for arm64${NC}"
else
    TARGET_ARCH="x86_64"
    echo -e "${BLUE}[INFO] Detected Intel, building for x86_64${NC}"
fi

# Find Qt6
QT_DIR=""
QT_PATHS=(
    "/opt/homebrew/opt/qt6"           # Apple Silicon Homebrew
    "/usr/local/opt/qt6"              # Intel Homebrew
    "/Applications/Qt"                # Qt Online Installer
    "$HOME/Qt"                        # User Qt Online Installer
)

for path in "${QT_PATHS[@]}"; do
    if [[ -d "$path" ]]; then
        # Find the latest Qt6 version
        QT_VERSION_DIR=$(find "$path" -maxdepth 2 -name "6.*" -type d 2>/dev/null | head -1)
        if [[ -n "$QT_VERSION_DIR" && -f "$QT_VERSION_DIR/bin/qmake" ]]; then
            QT_DIR="$QT_VERSION_DIR"
            break
        fi
    fi
done

if [[ -z "$QT_DIR" ]]; then
    # Try to find via qmake in PATH
    if command -v qmake6 &> /dev/null; then
        QT_DIR=$(qmake6 -query QT_INSTALL_PREFIX)
    elif command -v qmake &> /dev/null; then
        QT_DIR=$(qmake -query QT_INSTALL_PREFIX)
    fi
fi

if [[ -z "$QT_DIR" || ! -f "$QT_DIR/bin/qmake" ]]; then
    echo -e "${RED}[ERROR] Qt6 not found. Please install Qt6:${NC}"
    echo -e "  Apple Silicon: brew install qt6"
    echo -e "  Intel:         arch -x86_64 brew install qt6"
    echo -e "  Or download from: https://www.qt.io/download"
    exit 1
fi

echo -e "${GREEN}[INFO] Found Qt6 at: ${QT_DIR}${NC}"
QT_VERSION=$("$QT_DIR/bin/qmake" -query QT_VERSION)
echo -e "${GREEN}[INFO] Qt version: ${QT_VERSION}${NC}"

# Verify architecture compatibility
if [[ "$TARGET_ARCH" == *"arm64"* && "$HOST_ARCH" == "x86_64" ]]; then
    echo -e "${YELLOW}[WARNING] Cross-compiling arm64 on Intel requires Xcode with arm64 SDK${NC}"
fi

if [[ "$TARGET_ARCH" == *"x86_64"* && "$HOST_ARCH" == "arm64" ]]; then
    echo -e "${YELLOW}[WARNING] Cross-compiling x86_64 on Apple Silicon requires Rosetta 2${NC}"
    if ! /usr/bin/pgrep -q oahd; then
        echo -e "${YELLOW}[WARNING] Rosetta 2 not detected. Install with: softwareupdate --install-rosetta${NC}"
    fi
fi

# Clean build directory if requested
if [[ $CLEAN -eq 1 ]]; then
    echo -e "${YELLOW}[INFO] Cleaning build directory...${NC}"
    rm -rf "$BUILD_DIR"
fi

# Create build directory
mkdir -p "$BUILD_DIR"

# Configure CMake
CMAKE_ARGS=(
    -DCMAKE_PREFIX_PATH="$QT_DIR"
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DCMAKE_OSX_ARCHITECTURES="$TARGET_ARCH"
)

if [[ $UNIVERSAL -eq 1 ]]; then
    CMAKE_ARGS+=(-DXRK_BUILD_UNIVERSAL=ON)
fi

echo -e "${BLUE}[INFO] Configuring CMake...${NC}"
cd "$BUILD_DIR"
if [[ ! -f "CMakeCache.txt" ]] || [[ $CLEAN -eq 1 ]]; then
    cmake .. "${CMAKE_ARGS[@]}"
    if [[ $? -ne 0 ]]; then
        echo -e "${RED}[ERROR] CMake configure failed${NC}"
        cd ..
        exit 1
    fi
else
    echo -e "${BLUE}[INFO] Using cached CMake config...${NC}"
fi

# Build
echo -e "${BLUE}[INFO] Building ${TARGET} (${BUILD_TYPE})...${NC}"
cmake --build . --config "$BUILD_TYPE" --target "$TARGET" -- -j$(sysctl -n hw.ncpu)
if [[ $? -ne 0 ]]; then
    echo -e "${RED}[ERROR] Build failed${NC}"
    cd ..
    exit 1
fi
cd ..

# Success
echo -e "${GREEN}"
echo -e "========================================="
echo -e "Build succeeded!"
if [[ "$TARGET_ARCH" == *";"* ]]; then
    echo -e "Universal binary created"
else
    echo -e "Architecture: ${TARGET_ARCH}"
fi
echo -e "Build type: ${BUILD_TYPE}"
echo -e "Output: ${BUILD_DIR}/src/${TARGET}.app"
echo -e "========================================="
echo -e "${NC}"

# Verify the binary architecture
if [[ -f "${BUILD_DIR}/src/${TARGET}.app/Contents/MacOS/${TARGET}" ]]; then
    echo -e "${BLUE}[INFO] Binary architecture:${NC}"
    lipo -archs "${BUILD_DIR}/src/${TARGET}.app/Contents/MacOS/${TARGET}"
fi