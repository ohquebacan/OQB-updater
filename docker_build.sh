#!/usr/bin/env bash
# Build local dentro del contenedor devkitPro, replicando el workflow de CI.
# Uso: ./docker_build.sh [objetivo-make-extra]
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# APP_VERSION llega a los fuentes como -D y make no ve esa dependencia: si se
# cambia la versión sin limpiar, el binario sigue reportando la vieja, que es
# justo el dato con el que la app decide si hay actualización disponible.
# CI no lo necesita porque siempre parte de un checkout limpio.
if [ -d "${REPO_DIR}/build" ] && [ "${REPO_DIR}/Makefile" -nt "${REPO_DIR}/build" ]; then
    echo "Makefile cambió: limpiando build/ para no arrastrar la versión vieja"
    rm -rf "${REPO_DIR}/build"
fi

# El directorio de trabajo debe llamarse OQB-updater: el Makefile deriva
# TARGET del nombre de la carpeta, igual que hace el checkout de CI.
docker run --rm \
    -v "${REPO_DIR}:/OQB-updater" \
    -w /OQB-updater \
    devkitpro/devkita64 \
    bash -lc '
        set -euo pipefail
        git config --global --add safe.directory /OQB-updater
        git config --global --add safe.directory /OQB-updater/lib/borealis
        git config --global --add safe.directory /OQB-updater/TegraExplorer
        dkp-pacman -S --needed --noconfirm switch-curl switch-zlib switch-mbedtls >/dev/null
        make -C aiosu-forwarder -f Makefile
        make -j"$(nproc)" '"${1:-}"'
    '
