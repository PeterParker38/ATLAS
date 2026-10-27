#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
preset="${1:-debug}"
cmake --preset "$preset"
cmake --build --preset "$preset" --parallel "${BUILD_JOBS:-2}"
ctest --preset "$preset"
