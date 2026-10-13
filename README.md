# Dead Space iOS → Android (arm64) · Paso 5: visor nativo

Visor que carga los `.m3g` **originales del IPA** (sin convertir) y los dibuja con OpenGL ES 3.0.
Es la base del port: el lector de formatos (C++) y el renderizador son los que usará el motor final.

## Compilar
1. Extrae el IPA con `ds_step1_extract.py` (paso 1) → carpeta `salida/app/published`.
2. Copia los assets al proyecto:
   `tools/copy_assets.sh /ruta/a/salida/app/published`
   (copia `models`, `textures`, `textures_uncompressed`, `ParticleBaseTextures`; ~72 MB).
3. Abre la carpeta en **Android Studio** (SDK 33 + NDK + CMake) y pulsa *Run*, o por línea de comandos
   con Gradle 7.4.2:  `gradle wrapper --gradle-version 7.4.2 && ./gradlew assembleDebug`
   → `app/build/outputs/apk/debug/app-debug.apk` (solo `arm64-v8a`).

> Versiones: AGP 7.3.1 + Gradle 7.4.2 + compileSdk 33 (las que traen AndroidIDE / Android Code Studio).
> En Android Studio moderno puedes subirlas (AGP 8.x, Gradle 8.7, SDK 34).

## Uso
Un dedo: orbitar · Pellizco: zoom · `<` `>`: modelo anterior/siguiente (arranca en Isaac).
Arriba se muestra el resumen (mallas, vértices, triángulos, texturas) o el error.

## Qué hace
- `m3g.cpp`  lector IM-M3G / JSR-184: grafo de escena, matrices de nodo, VertexBuffer/Array (byte, short, float),
  strips → triángulos, SubMesh (tipo 100), SkinnedMesh en pose base, texturas externas (parámetro 900).
- `pvrtc.cpp` decodificador PVRTC 4bpp (formatos de imagen 124/125) + RGB/RGBA crudos (99/100).
- `renderer.cpp` GLES 3: un shader, texturas con mipmaps, normales calculadas si faltan, pasada opaca + pasada con alfa.
- `jni.cpp` / `MainActivity.java` puente y UI mínima.

## Verificado en PC (tools/host_test.cpp, g++ -std=c++17)
- 49 modelos de `models/`: **4180 mallas, 577 036 vértices, 421 176 triángulos** = idéntico al exportador de Python.
- 168 texturas (PVRTC 124/125, RGB, RGBA): **idénticas píxel a píxel** al decodificador de Python.
- `jni.cpp`, `renderer.cpp` y el resto: compilan sin avisos (`-Wall -Wextra`) contra cabeceras de sustitución.

## NO verificado todavía
- No se ha compilado con el NDK real ni ejecutado en un dispositivo/GPU: el renderizador y la actividad son código
  sin probar en hardware.
- Animación, skinning en ejecución, modos de mezcla del material (se usa alfa si la textura tiene alfa),
  niebla, luces del juego, sonido, entrada de juego: pendientes.
- Los `*_dynamic_text` (carteles con texto) no existen en disco y se dibujan sin textura.

Los assets del juego son propiedad de sus dueños: no los subas a ningún repositorio (`.gitignore` ya los excluye).
