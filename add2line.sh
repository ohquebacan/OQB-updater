#!/bin/bash
# Traduce las direcciones de un crash report de Atmosphere a lineas de codigo.
#
#   ./add2line.sh crash_report.log [OQB-updater.elf]
#
# El .elf tiene que ser el del build exacto que estaba corriendo. Lo publica el
# CI junto al .nro; si no coincide, las lineas que salgan seran de otro sitio.
#
# Para comprobar que es el correcto: el "Module Id" del reporte son los primeros
# 40 caracteres del build-id del .elf.
#   aarch64-none-elf-readelf -n OQB-updater.elf | grep -i 'build id'

set -u

REPORTE="${1:-}"
ELF="${2:-OQB-updater.elf}"

if [ -z "$REPORTE" ] || [ ! -f "$REPORTE" ]; then
    echo "uso: $0 <crash_report.log> [ruta/al/OQB-updater.elf]" >&2
    exit 1
fi

if [ ! -f "$ELF" ]; then
    echo "no encuentro el .elf: $ELF" >&2
    echo "bajalo del artifact del build correspondiente en GitHub Actions." >&2
    exit 1
fi

# Saca cada "(OQB-updater + 0x...)" del reporte, incluidos LR, PC y la pila.
grep -oE '\(.* \+ (0x[0-9a-f]+)\)' "$REPORTE" | grep -oE '0x[0-9a-f]+' | while read -r dir; do
    printf '%s  ' "$dir"
    aarch64-none-elf-addr2line -e "$ELF" -pCf -si "$dir"
done
