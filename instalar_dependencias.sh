#!/usr/bin/env bash
set -euo pipefail

# Dependencias de compilación y ejecución en Debian/Ubuntu y derivados.
if ! command -v apt-get >/dev/null 2>&1; then
    printf '%s\n' 'Este instalador necesita APT (Debian/Ubuntu o derivados).' >&2
    exit 1
fi

if (( EUID == 0 )); then
    SUDO=()
elif command -v sudo >/dev/null 2>&1; then
    SUDO=(sudo)
else
    printf '%s\n' 'Se necesita sudo o ejecutar este script como administrador.' >&2
    exit 1
fi

printf '%s\n' 'Se actualizará el índice APT y se instalarán las dependencias de Stellar Store.'
printf '%s\n' 'Flatpak y Snap son opcionales y se gestionan desde Ajustes.'
"${SUDO[@]}" apt-get update
"${SUDO[@]}" apt-get install \
    build-essential cmake pkg-config python3 tar \
    qt6-base-dev qt6-base-dev-tools qt6-qpa-plugins \
    libappstreamqt-dev appstream policykit-1 libglib2.0-bin

printf '%s\n' 'Dependencias instaladas. Ahora puedes ejecutar ./compilar.sh'
