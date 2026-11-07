#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
OUTPUT_DIR="${OUTPUT_DIR:-$ROOT/dist}"
BINARY="$BUILD_DIR/StellarStore"
ARCH="$(uname -m)"
ARCHIVE="$OUTPUT_DIR/StellarStore-portable-$ARCH.tar.gz"

for command in cmake ldd tar python3; do
    if ! command -v "$command" >/dev/null 2>&1; then
        printf 'Falta la herramienta necesaria: %s\n' "$command" >&2
        exit 1
    fi
done

mkdir -p -- "$OUTPUT_DIR"
if [[ -e "$ARCHIVE" ]]; then
    printf 'El archivo ya existe; no se sobrescribirá: %s\n' "$ARCHIVE" >&2
    printf '%s\n' 'Cámbiale el nombre o selecciona otra carpeta con OUTPUT_DIR.' >&2
    exit 1
fi

BUILD_DIR="$BUILD_DIR" "$ROOT/compilar.sh"

# Los plugins deben corresponder a la misma instalación Qt que el binario.
QT_CORE="$(ldd "$BINARY" | awk '/libQt6Core\.so/{print $3; exit}')"
if [[ -z "$QT_CORE" || ! -f "$QT_CORE" ]]; then
    printf '%s\n' 'No se pudo localizar Qt6Core del binario.' >&2
    exit 1
fi
QT_LIB_DIR="$(dirname -- "$(readlink -f -- "$QT_CORE")")"
QT_PATHS="${QT_PATHS:-$(command -v qtpaths6 || true)}"
if [[ -z "$QT_PATHS" ]]; then
    printf '%s\n' 'Falta qtpaths6. Instala qt6-base-dev-tools o define QT_PATHS.' >&2
    exit 1
fi
QT_QUERY_LIBS="$($QT_PATHS --query QT_INSTALL_LIBS)"
if [[ "$(readlink -f -- "$QT_QUERY_LIBS")" != "$(readlink -f -- "$QT_LIB_DIR")" ]]; then
    printf '%s\n' 'qtpaths6 pertenece a otra instalación de Qt. Define QT_PATHS con el qtpaths correspondiente al binario.' >&2
    exit 1
fi
PLUGIN_DIR="$($QT_PATHS --query QT_INSTALL_PLUGINS)"
if [[ ! -f "$PLUGIN_DIR/platforms/libqxcb.so" ]]; then
    printf 'No se encontró el plugin X11 de Qt en %s\n' "$PLUGIN_DIR" >&2
    exit 1
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/stellarstore-portable.XXXXXXXX")"
trap 'rm -rf -- "$WORK"' EXIT
APP="$WORK/StellarStore-portable"
mkdir -p "$APP/usr/bin" "$APP/usr/lib" "$APP/usr/plugins"
cp -- "$BINARY" "$APP/usr/bin/StellarStore"

for group in platforms platformthemes xcbglintegrations wayland-decoration-client \
             wayland-graphics-integration-client imageformats iconengines tls \
             networkinformation styles; do
    if [[ -d "$PLUGIN_DIR/$group" ]]; then
        cp -a -- "$PLUGIN_DIR/$group" "$APP/usr/plugins/"
    fi
done

# ldd includes transitive dependencies. Exclude the dynamic loader and glibc:
# mixing those with the host system is unsafe. Check every copied Qt plugin.
python3 - "$APP" <<'PY'
import os
import pathlib
import shutil
import subprocess
import sys

app = pathlib.Path(sys.argv[1])
libdir = app / 'usr/lib'
targets = [app / 'usr/bin/StellarStore']
targets.extend((app / 'usr/plugins').rglob('*.so'))
excluded = ('libc.so.', 'libm.so.', 'libdl.so.', 'libpthread.so.',
            'librt.so.', 'libresolv.so.', 'libutil.so.', 'ld-linux', 'ld-musl')
for target in targets:
    result = subprocess.run(['ldd', str(target)], text=True, capture_output=True)
    if result.returncode:
        sys.exit(f'No se pudieron leer las dependencias de {target}: {result.stderr}')
    for line in result.stdout.splitlines():
        if '=> not found' in line:
            sys.exit(f'Dependencia no encontrada en {target}: {line.strip()}')
        parts = line.strip().split()
        if len(parts) < 3 or parts[1] != '=>':
            continue
        name, source = parts[0], pathlib.Path(parts[2])
        if name.startswith(excluded) or not source.is_absolute():
            continue
        dest = libdir / name
        if dest.exists():
            if not os.path.samefile(source, dest) and source.read_bytes() != dest.read_bytes():
                sys.exit(f'Bibliotecas incompatibles para {name}: {source}')
        else:
            shutil.copy2(source, dest, follow_symlinks=True)
PY

cat > "$APP/usr/bin/qt.conf" <<'EOF'
[Paths]
Prefix = ..
Plugins = plugins
EOF
cat > "$APP/AppRun" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd -- "$(dirname -- "$(readlink -f -- "${BASH_SOURCE[0]}")")" && pwd)"
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$HERE/usr/plugins"
exec "$HERE/usr/bin/StellarStore" "$@"
EOF
chmod +x "$APP/AppRun"
cat > "$APP/LEEME.txt" <<'EOF'
Ejecuta ./AppRun en Linux de la misma arquitectura.
El paquete contiene Qt, AppStreamQt y bibliotecas compartidas detectadas al
compilar. Necesita un sistema compatible con la glibc del equipo de origen.
Los gestores de paquetes, pkexec, appstreamcli, Flatpak y Snap se usan desde
el sistema anfitrión y no se instalan ni se incluyen automáticamente.
EOF
tar -C "$WORK" -czf "$WORK/portable.tar.gz" StellarStore-portable
# Hard-link creation is atomic and refuses to overwrite an existing archive.
if ! ln -- "$WORK/portable.tar.gz" "$ARCHIVE" 2>/dev/null; then
    # The temporary directory can be on another filesystem: use exclusive
    # creation instead, still without overwriting an existing file.
    if ! ( set -o noclobber; cat "$WORK/portable.tar.gz" > "$ARCHIVE" ); then
        printf 'No se pudo crear el archivo sin sobrescribir: %s\n' "$ARCHIVE" >&2
        exit 1
    fi
fi
printf '\nPortable generado: %s\n' "$ARCHIVE"
