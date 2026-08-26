#!/usr/bin/env bash
# Build local dentro del contenedor devkitPro, replicando el workflow de CI.
# Uso: ./docker_build.sh [objetivo-make-extra]
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

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
