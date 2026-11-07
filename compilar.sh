#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf 2)}"

if ! command -v cmake >/dev/null 2>&1; then
    printf '%s\n' 'Falta CMake. Ejecuta primero ./instalar_dependencias.sh' >&2
    exit 1
fi

cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$BUILD_DIR" --parallel "$JOBS"
printf '\nBinario generado: %s/StellarStore\n' "$BUILD_DIR"
