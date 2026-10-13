#!/bin/sh
# Copia los .m3g ORIGINALES (los que extrajo el paso 1) a /sdcard/DeadSpace/published (Termux o adb shell).
# Uso: sh install_assets.sh [carpeta published del paso 1]
SRC="${1:-/storage/emulated/0/Download/salida/app/published}"
DST="/storage/emulated/0/DeadSpace/published"
[ -d "$SRC/models" ] || { echo "No encuentro $SRC/models"; exit 1; }
for d in models textures textures_uncompressed ParticleBaseTextures; do
  [ -d "$SRC/$d" ] && mkdir -p "$DST/$d" && cp -R "$SRC/$d/." "$DST/$d/"
done
du -sh "$DST"
