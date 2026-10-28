# AoE Mobile - motor nativo de Age of Empires para Android

Implementacion de la *Especificacion: AoE nativo para Android (Niveles 1 a 3)* (ver `docs/especificacion.md`), construida por hitos (M0..M8). Este repositorio no contiene ningun archivo del juego original: el motor lee los archivos del propio dispositivo del usuario.

**Estado actual: M2 (mapa visible) - CI verde con datos sinteticos. M0 y M1 completados; la verificacion del usuario con archivos reales llega con cada hito.** Criterios, decisiones y evidencia en `STATUS.md`.

## Que hace hoy (M0 + M1 + M2)

- App Android (arm64-v8a) con C++17 + SDL2 + CMake; el mismo codigo nativo se autocomprueba en escritorio sin ventana.
- M0: version, FPS/RAM, registro en pantalla y **COMPARTIR LOG** (texto por ACTION_SEND).
- M1, con botones grandes:
  - **GENERAR**: crea datos sinteticos de prueba (DRS + SLP + paleta + empires.dat de prueba, deterministas) en `files/gen/`.
  - **ASSETCHECK**: valida todo lo de `gen/` e `import/` con el mismo criterio que la herramienta de escritorio; informe en pantalla y en `m1_assetcheck.txt`.
  - **IMPORTAR**: selector de archivos de Android (SAF, seleccion multiple, sin permisos) para traer los archivos reales del juego (interfac.drs, graphics.drs, empires.dat...). Al copiarlos, el chequeo corre solo.
  - **DUMP SLP**: decodifica un marco de sprite (de los importados o del sintetico), lo muestra en pantalla y escribe un PNG en `files/dumps/`.
  - **COMPARTIR**: envia el informe acumulado como texto.
- M2, boton **MAPA (M2)**:
  - Mapa de prueba 64x64 generado por codigo (pasto, lago con orilla, rio sinuoso, parches de tierra; determinista).
  - Dibujo isometrico "diamante" con camara que se **arrastra con el dedo** (flechas en escritorio).
  - **Overlay de depuracion obligatorio**: mapa en memoria con dimensiones, conteo de terrenos, tamano de mosaico y su origen (empires.dat real, sintetico o documentado), camara y limites, mosaicos intentados/dibujados/fuera/fallidos con causa. Si algo falla, la pantalla lo dice; nunca negro sin explicacion.
  - El tamano de mosaico **se lee del empires.dat** (48x24, mitad 24x12; el dat real del usuario manda sobre el sintetico).
- Herramientas de escritorio (CI y depuracion): `assetcheck`, `dumpslp`, `gendata`.
- Lectores verificados contra la documentacion de openage (no de memoria): DRS, SLP 2.0N (todos los comandos de dibujo), paletas JASC-PAL y `empires.dat` (DEFLATE crudo + estructura ROR de unidades). Los archivos corruptos o truncados fallan con la causa y el desplazamiento exacto.

## Como instalar el APK (sin laptop)

1. Abre la pestana **Actions** de este repositorio y entra al run mas reciente en verde.
2. En la seccion **Artifacts** descarga `aoe-mobile-M2-debug-apk` (viene en un ZIP).
3. Descomprime el ZIP e instala el APK (la primera vez, el telefono pedira permitir "apps de fuentes desconocidas" para tu explorador de archivos).

## Como cerrar M1/M2 desde el telefono (evidencia del usuario)

1. **GENERAR** y **ASSETCHECK**: el informe debe terminar en `ASSETCHECK: 12 OK, 0 FALLIDOS`.
2. **DUMP SLP**: aparece el sprite sintetico; captura de pantalla para la evidencia.
3. **IMPORTAR**: elige tus archivos reales del Age of Empires (si tienes el original instalado o su carpeta `data/`: `empires.dat`, `interfac.drs`, `graphics.drs`...). Puedes seleccionar varios a la vez.
4. **ASSETCHECK** de nuevo: comparte el resultado (texto) con **COMPARTIR**.
5. **DUMP SLP** con datos reales: captura de pantalla del sprite decodificado.
6. **MAPA (M2)**: se abre el mapa isometrico con el overlay de depuracion; **arrastra** el dedo para mover la camara. Captura de pantalla con datos reales (el TILE dira `empires.dat` como origen si importaste el tuyo).

Con ese texto + esas capturas se cierra M2 y arranca M3 (objetos y centro urbano).

## Estructura (mapeo de la spec §4)

| Carpeta | Responsabilidad | Estado |
| --- | --- | --- |
| `src/platform` | SDL2, bucle principal, registro, metricas (FPS/RAM) | M0 OK |
| `src/render` | Composicion por software (búfer 32 bits + fuente embebida) | M0/M1 OK (+ sprites, PNG) |
| `src/game` | Mapa y estado del juego, sin dibujo ni entrada | M2 OK (mapa de prueba) |
| `src/assets` | DRS, SLP, paletas, dat (unidades/edificios), inflador DEFLATE, PNG, generador sintetico, chequeo | M1 OK (verificado con sinteticos) |
| `src/input` | Toques -> ordenes para `sim` | M4 |
| `src/ai` | Bot (misma interfaz de ordenes que el humano) | M7 |
| `tools` | assetcheck, dumpslp, gendata (+ rendertest/simtest/perfreport en M2+) | M1 OK |
| `android-project/app/src/main/java/com/aoemobile/engine` | MainActivity (SAF importer) | M1 OK |

Regla clave desde el dia uno: `sim` no sabe que existe un renderizador; la capa `assets` no depende de SDL.

## Integracion continua

`.github/workflows/build.yml` ejecuta dos trabajos en cada push a `main`:

- **desktop-selftest**: compila el motor y las herramientas, corre `aoe --selftest` (driver `dummy`), las **pruebas M1** (`scripts/m1_tests.sh`: sinteticos, pixel a pixel, cross-check zlib, 9 corruptos que deben fallar), las **pruebas M2** (`scripts/m2_tests.sh`: mapa 64x64, tile leido del dat, camara con clamp, determinismo PNG byte a byte, guarda anti-pantalla-negra, dat truncado rechazado) y una **demo M1 en pantalla** con captura. Todo queda como artefacto `evidencia-M0-M2`.
- **android-apk**: Gradle + NDK compilan el APK `arm64-v8a` (artefacto `aoe-mobile-M2-debug-apk`).

SDL2 no esta versionada aqui: `scripts/fetch_sdl.sh` la descarga con **version y SHA-256 fijados** (2.32.10). Ningun archivo del juego original entra nunca al repositorio (spec §9); los sinteticos los genera `gendata`.

## Licencias

- Codigo propio: MIT (ver `LICENSE`).
- SDL2: zlib - se descarga en CI, no se versiona.
- font8x8 (`src/render/font8x8.h`): dominio publico, versionado con atribucion.
- Los archivos del juego siguen siendo propiedad del usuario y no se suben nunca aqui.
- Documentacion consultada (no se copio codigo): openage (GPL, solo especificaciones de formato y comportamiento) y FolkertVanVerseveld/aoe (MIT).
