# AoE Mobile - motor nativo de Age of Empires para Android

Implementacion de la *Especificacion: AoE nativo para Android (Niveles 1 a 3)* (ver `docs/especificacion.md`), construida por hitos (M0..M8). Este repositorio no contiene ningun archivo del juego original: el motor lee los archivos del propio dispositivo del usuario.

**Estado actual: M0 (esqueleto) - en verificacion.** Criterios, decisiones y evidencia en `STATUS.md`.

## Que hace hoy (M0)

- App Android (arm64-v8a) con C++17 + SDL2 + CMake, empaquetada por Gradle; el mismo codigo nativo se autocomprueba en escritorio sin ventana.
- En pantalla: color de fondo, version del programa, estado de SDL, FPS y RAM del proceso, las ultimas lineas del registro y el ultimo evento tactil.
- El registro se guarda en `game.log` (almacenamiento externo de la app) y se puede compartir como texto con el boton **COMPARTIR LOG**.

## Como instalar el APK (sin laptop)

1. Abre la pestana **Actions** de este repositorio y entra al run mas reciente en verde.
2. En la seccion **Artifacts** descarga `aoe-mobile-M0-debug-apk` (viene en un ZIP).
3. Descomprime el ZIP e instala `aoe-mobile-M0-debug.apk` (la primera vez, el telefono pedira permitir "apps de fuentes desconocidas" para tu explorador de archivos).

## Evidencia que cierra M0 (la pedimos al usuario)

1. **Captura de pantalla** de la app abierta: debe verse el color, la version, FPS/RAM y lineas de registro.
2. **Texto del registro**: toca **COMPARTIR LOG** (abajo a la derecha) y envia el texto que Android ofrece compartir.
3. Opcional: archivo `Android/data/com.aoemobile.engine/files/game.log` (segun el telefono, requiere un explorador con acceso a esa carpeta).

Con esa evidencia se cierra M0 y arranca M1 (lectura DRS/SLP/paletas con datos sinteticos; spec §6).

## Estructura (mapeo de la spec §4)

| Carpeta | Responsabilidad | Estado |
| --- | --- | --- |
| `src/platform` | SDL2, bucle principal, registro, metricas (FPS/RAM) | M0 OK |
| `src/render` | Composicion por software (búfer 32 bits + fuente embebida) | M0 base OK |
| `src/assets` | DRS, SLP, paletas, tabla de unidades | M1 |
| `src/sim` | Estado del juego, sin dibujo ni entrada | M4+ |
| `src/input` | Toques -> ordenes para `sim` | M4 |
| `src/ai` | Bot (misma interfaz de ordenes que el humano) | M7 |
| `tools` | assetcheck, dumpslp, rendertest, simtest, perfreport | M1+ |

Regla clave desde el dia uno: `sim` no sabe que existe un renderizador; el registro y las metricas viven solo en `platform`.

## Integracion continua

`.github/workflows/build.yml` ejecuta dos trabajos en cada push a `main`:

- **desktop-selftest**: compila el codigo nativo en Linux (SDL con solo lo minimo), corre `aoe --selftest` con el driver de video `dummy`, y sube como artefacto la captura `selftest.bmp` + `game.log`. La autoprueba falla si no hay captura, no hay registro o aparece `FATAL`.
- **android-apk**: Gradle + NDK compilan el APK `arm64-v8a` y lo sube como artefacto `aoe-mobile-M0-debug-apk`.

SDL2 no esta versionada aqui: `scripts/fetch_sdl.sh` la descarga con **version y SHA-256 fijados** (2.32.10) y copia sus archivos Java al proyecto Android antes del build. Ningun archivo del juego original entra nunca al repositorio (spec §9).

## Licencias

- Codigo propio: MIT (ver `LICENSE`).
- SDL2: zlib - se descarga en CI, no se versiona.
- font8x8 (`src/render/font8x8.h`): dominio publico, versionado con atribucion.
- Los archivos del juego siguen siendo propiedad del usuario y no se suben nunca aqui.
