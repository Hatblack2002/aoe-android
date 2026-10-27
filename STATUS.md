# STATUS - AoE Mobile

Actualizado: 28 de octubre de 2026. Cada hito lista sus criterios de la especificacion §6 con estado y evidencia. Regla: un hito solo se cierra con TODOS sus criterios cumplidos y evidencia adjunta (spec §2, principio 1).

| Hito | Estado |
| --- | --- |
| M0 Esqueleto | Codigo y CI listos; falta evidencia del telefono |
| M1 Lectura de datos | no iniciado |
| M2 Mapa visible | no iniciado |
| M3 Objetos y centro urbano | no iniciado |
| M4 Unidades que se mueven | no iniciado |
| M5 Economia y construccion (fin Nivel 2) | no iniciado |
| M6 Combate basico | no iniciado |
| M7 Bot v1 (fin Nivel 3) | no iniciado |
| M8 Control tactil, pulido y rendimiento | no iniciado |

## M0 - criterios de aceptacion

| Criterio (spec §6 M0) | Estado | Evidencia |
| --- | --- | --- |
| CMake + SDL2 + NDK | OK | `CMakeLists.txt` raiz compartido por escritorio y Android; Gradle invoca el mismo CMake con el toolchain del NDK |
| GitHub Actions genera el APK | OK | workflow `build.yml`, trabajo `android-apk` -> artefacto `aoe-mobile-M0-debug-apk` |
| La app abre y muestra un color | OK (codigo) | composicion por software en `src/render/framebuffer.cpp` (`clear` + franja superior) |
| Muestra la version del programa | OK (codigo) | primera linea en pantalla: `AOE_VERSION_STRING` (`src/version.h`) |
| Muestra las ultimas lineas del registro | OK (codigo) | anillo de 64 lineas en `src/platform/log.cpp`; se dibujan 9 en pantalla |
| El registro se guarda en archivo compartible | OK (codigo) | `game.log` en el almacenamiento externo de la app (`Android/data/com.aoemobile.engine/files/`) + boton COMPARTIR LOG (ACTION_SEND con texto, sin permisos) |
| Captura de pantalla del telefono por el usuario | PENDIENTE | se pide al usuario tras instalar el APK |

### Evidencia del agente (sin telefono)

- Autoprueba de escritorio: mismo codigo C++ compilado en Linux, ejecutado con el driver de video `dummy` durante 120 cuadros, guarda `selftest.bmp` y `game.log`, salida 0. Se ejecuta en CI (trabajo `desktop-selftest`) y localmente antes de cada push.
- CI en verde: enlaces al run en los comentarios del commit correspondiente.

## Decisiones tecnicas registradas

- SDL2 2.32.10 fijada con SHA-256 (`scripts/fetch_sdl.sh`); nunca se versiona en el repo y se recupera en cada build.
- Render: búfer ARGB8888 propio subido a una textura cada cuadro (spec §3). GPU solo si las mediciones lo exigen.
- Fuente bitmap 8x8 embebida (dominio publico); cero dependencias de texto.
- minSdk 21, targetSdk 34, solo arm64-v8a (spec §3).
- Registro con tres destinos: SDL_Log/logcat, archivo y anillo para pantalla. Metricas desde el dia uno: FPS (media movil) y VmRSS de `/proc/self/status`.
- Compartir registro por ACTION_SEND desde JNI (sin FileProvider ni permisos): util para toda la evidencia futura sin laptop.
- Fallo cerrado: cualquier error de SDL/render escribe FATAL en registro + stderr y termina con codigo 1; nunca pantalla negra silenciosa.

## Proximo hito (M1) - checklist

- [ ] Lector de contenedores DRS + herramienta `assetcheck`
- [ ] Decodificador SLP + `dumpslp` (PNG)
- [ ] Lectura de paletas
- [ ] Tabla de datos de unidades/edificios (verificar compresion real, no de memoria)
- [ ] Generador de datos sinteticos (DRS/SLP/paleta) + pruebas en CI, incluidos archivos truncados/corruptos que deben fallar con error claro
