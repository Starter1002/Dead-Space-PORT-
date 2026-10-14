# Dead Space iOS → Android (arm64) · visor nativo (paso 5)

Visor que carga los `.m3g` **originales del IPA** (sin convertir) con OpenGL ES 3.0.
Los archivos del juego NO van dentro del APK ni del repositorio: se leen de `/sdcard/DeadSpace/published/`.

## 1. Compilar el APK en GitHub (gratis, en la nube)
1. Crea un repositorio vacío en GitHub (sin README).
2. Sube el contenido de esta carpeta (`dsport/`) a la raíz del repositorio, incluida `.github/`.
   Desde Termux: ver comandos abajo.
3. Pestaña **Actions → Build APK**. Se lanza solo al subir; también con *Run workflow*.
4. Al terminar (5-10 min), abre la ejecución y descarga el artefacto **DeadSpacePort-debug** (un ZIP con `app-debug.apk`).

### Subir desde Termux
```
pkg install git
git config --global --add safe.directory '*'
cd /ruta/a/dsport
git init -b main && git config core.fileMode false
git add . && git commit -m "Dead Space Port"
git remote add origin https://github.com/TU_USUARIO/TU_REPO.git
git push -u origin main
```
Usuario = tu usuario de GitHub; contraseña = un **token personal** (Settings → Developer settings → Tokens).
Token clásico con permisos `repo` y `workflow` (hace falta `workflow` para subir `.github/workflows`).

## 2. Copiar los assets al móvil
Con la carpeta `salida/app/published` del paso 1 en Download (Termux):
```
sh tools/install_assets.sh
```
Copia `models`, `textures`, `textures_uncompressed` y `ParticleBaseTextures` (~72 MB) a
`/sdcard/DeadSpace/published/`. Si la carpeta del paso 1 está en otro sitio: `sh tools/install_assets.sh /ruta/published`.

## 3. Usar
Instala el APK, ábrelo y pulsa **Conceder acceso a archivos** (Android 11+: "Permitir acceso a todos los archivos").
Un dedo: orbitar · pellizco: zoom · `<` `>`: modelo anterior/siguiente (empieza en Isaac).
Fila superior de botones: `clip -` / `clip +` cambian de animación y `play/pausa` la detiene.
Los clips se reproducen en bucle; arriba sale el número de clip y su duración. Los personajes tienen entre 1 y 121 clips.
Arriba sale el resumen del modelo (mallas, vértices, triángulos, texturas) o el error.

## Animaciones (paso 6)
Cada SkinnedMesh trae su lista de canales (propiedad, hueso): 275 traslación, 268 orientación, 270 escala.
Los clips son bloques de N secuencias de fotogramas consecutivas con esa forma exacta (verificado en los 11 personajes:
Isaac 121 clips, brute 27, slasher 56, lurker 46, boss 42...). Skinning en GPU (hasta 48 huesos).
Los nombres de los clips parecen estar en los `.bin` hermanos (formato FORM/BINX), todavía sin descifrar.

## Estructura
- `m3g.cpp`  lector IM-M3G / JSR-184 (grafo de escena, strips, SubMesh tipo 100, SkinnedMesh en pose base, texturas externas).
- `pvrtc.cpp` PVRTC 4bpp (imágenes 124/125) + RGB/RGBA crudos (99/100).
- `renderer.cpp` GLES 3, `jni.cpp` / `MainActivity.java` puente y UI.
- `tools/host_test.cpp` prueba en PC del núcleo (g++ -std=c++17).

## Verificado
En PC: 49 modelos = 4180 mallas, 577 036 vértices, 421 176 triángulos (idéntico al exportador de Python);
168 texturas idénticas píxel a píxel; 11 esqueletos y 356 clips cargan sin errores y las poses animadas
(Isaac, brute, slasher, lurker) se ven coherentes en pruebas de PC; fuentes C++ sin avisos con `-Wall -Wextra` contra cabeceras de sustitución.

## NO verificado
No se ha ejecutado el workflow de GitHub, ni compilado con el NDK real, ni probado en dispositivo/GPU.
Pendiente: animación, skinning en ejecución, modos de mezcla, niebla, sonido, entrada de juego.
Los assets del juego son propiedad de sus dueños: no los subas a ningún repositorio.
