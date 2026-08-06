#!/bin/bash
# XRK Build Script for macOS - Intel (x86_64)
# This is a wrapper that calls build_mac.sh with --arch x86_64

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$SCRIPT_DIR/build_mac.sh" --arch x86_64 "$@"