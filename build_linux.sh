#!/bin/bash
# XRK Build Script for Linux
# Usage: ./build_linux.sh [--clean] [--release|--debug]

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

BUILD_DIR="build_linux"
BUILD_TYPE="Release"
TARGET="xrk"
CLEAN=0

while [[ $# -gt 0 ]]; do
    case $1 in
        --clean) CLEAN=1; shift ;;
        --release) BUILD_TYPE="Release"; shift ;;
        --debug) BUILD_TYPE="Debug"; shift ;;
        -h|--help)
            echo "Usage: $0 [--clean] [--release|--debug]"
            exit 0
            ;;
        *) echo -e "${RED}[ERROR] Unknown option: $1${NC}"; exit 1 ;;
    esac
done

echo -e "${BLUE}=========================================${NC}"
echo -e "${BLUE}  XRK Linux Build Script${NC}"
echo -e "${BLUE}=========================================${NC}"

# Check dependencies
echo -e "${BLUE}[INFO] Checking dependencies...${NC}"
DEPS=("cmake" "ninja-build" "g++")
MISSING=()

for dep in "${DEPS[@]}"; do
    if ! command -v "$dep" &>/dev/null; then
        MISSING+=("$dep")
    fi
done

if [[ ${#MISSING[@]} -gt 0 ]]; then
    echo -e "${YELLOW}[WARNING] Missing dependencies: ${MISSING[*]}${NC}"
    echo -e "${YELLOW}[INFO] Installing via apt...${NC}"
    sudo apt update
    sudo apt install -y cmake ninja-build g++
fi

# Check Qt6
if ! pkg-config --exists Qt6Core 2>/dev/null; then
    echo -e "${YELLOW}[WARNING] Qt6 not found via pkg-config, checking cmake...${NC}"
    if ! cmake --find-package -DNAME=Qt6 -DCOMPILER_ID=GNU -DLANGUAGE=CXX -DMODE=EXIST &>/dev/null; then
        echo -e "${RED}[ERROR] Qt6 not found. Please install:${NC}"
        echo -e "  sudo apt install qt6-base-dev qt6-tools-dev qt6-base-dev-tools"
        echo -e "  sudo apt install qt6-multimedia-dev libqt6websockets6-dev"
        exit 1
    fi
fi

echo -e "${GREEN}[INFO] Dependencies OK${NC}"

# Clean
if [[ $CLEAN -eq 1 ]]; then
    echo -e "${YELLOW}[INFO] Cleaning build directory...${NC}"
    rm -rf "$BUILD_DIR"
fi

# Configure
mkdir -p "$BUILD_DIR"
echo -e "${BLUE}[INFO] Configuring CMake...${NC}"
cmake -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX=/usr

if [[ $? -ne 0 ]]; then
    echo -e "${RED}[ERROR] CMake configure failed${NC}"
    exit 1
fi

# Build
echo -e "${BLUE}[INFO] Building ${TARGET}...${NC}"
cmake --build "$BUILD_DIR" -j$(nproc)

if [[ $? -ne 0 ]]; then
    echo -e "${RED}[ERROR] Build failed${NC}"
    exit 1
fi

# Success
echo -e "${GREEN}"
echo -e "========================================="
echo -e "Build succeeded!"
echo -e "Build type: ${BUILD_TYPE}"
echo -e "Output: ${BUILD_DIR}/src/${TARGET}"
echo -e "========================================="
echo -e "${NC}"

# Verify
file "${BUILD_DIR}/src/${TARGET}" 2>/dev/null || true