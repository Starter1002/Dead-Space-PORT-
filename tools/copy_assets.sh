#!/bin/sh
# Copia los .m3g ORIGINALES (los que extrajo el paso 1) a los assets del APK.
# Uso: tools/copy_assets.sh /ruta/a/salida/app/published
set -e
SRC="${1:?uso: copy_assets.sh <carpeta published del paso 1>}"
DST="$(dirname "$0")/../app/src/main/assets/published"
mkdir -p "$DST"
for d in models textures textures_uncompressed ParticleBaseTextures; do
  [ -d "$SRC/$d" ] && mkdir -p "$DST/$d" && cp -R "$SRC/$d/." "$DST/$d/"
done
du -sh "$DST"
