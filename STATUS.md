# STATUS - AoE Mobile

Actualizado: 28 de octubre de 2026. Cada hito lista sus criterios de la especificacion §6 con estado y evidencia. Regla: un hito solo se cierra con TODOS sus criterios cumplidos y evidencia adjunta (spec §2, principio 1).

| Hito | Estado |
| --- | --- |
| M0 Esqueleto | CI verde + registro del dispositivo recibido; falta solo la captura de pantalla |
| M1 Lectura de datos | no iniciado |
| M2 Mapa visible | no iniciado |
| M3 Objetos y centro urbano | no iniciado |
| M4 Unidades que se mueven | no iniciado |
| M5 Economia y construccion (fin Nivel 2) | no iniciado |
| M6 Combate basico | no iniciado |
| M7 Bot v1 (fin Nivel 3) | no iniciado |
| M8 Control tactil, pulido y rendimiento | no iniciado |

## M0 - criterios de aceptacion

Run de CI que acredita esta tabla: https://github.com/Hatblack2002/aoe-android/actions/runs/40600155908

| Criterio (spec §6 M0) | Estado | Evidencia |
| --- | --- | --- |
| CMake + SDL2 + NDK | OK | `CMakeLists.txt` raiz compartido por escritorio y Android; Gradle invoca el mismo CMake con el toolchain del NDK |
| GitHub Actions genera el APK | OK | artefacto `aoe-mobile-M0-debug-apk` (742 KB) en el run 40600155908 |
| La app abre y muestra un color | OK | captura `selftest.bmp` del trabajo `desktop-selftest` (mismo codigo nativo) + pendiente captura del telefono |
| Muestra la version del programa | OK | visible en la captura: "AoE Mobile 0.1.0 (M0)" |
| Muestra las ultimas lineas del registro | OK | visibles en la captura (9 lineas del anillo) |
| El registro se guarda en archivo compartible | RECIBIDO del dispositivo | log del telefono: `game.log (ok)` en `/storage/emulated/0/Android/data/com.aoemobile.engine/files/` + COMPARTIR LOG funcionando (`intent lanzado`); en CI, `game.log` publicado como artefacto |
| Captura de pantalla del telefono por el usuario | PENDIENTE | se pide al usuario tras instalar el APK (ver README) |

### Evidencia del agente (sin telefono)

- Autoprueba de escritorio local (g++ 14 / SDL2 2.32.10 / driver dummy): 120 cuadros, captura y registro, salida 0, sin FATAL.
- CI verde en el run 40600155908: `desktop-selftest` OK (compila, corre y publica evidencia) y `android-apk` OK (APK arm64-v8a como artefacto).

### Correcciones durante la puesta en verde (registro honesto)

1. `compartirTexto()` se llamaba fuera del guard `__ANDROID__` -> error detectado en compilacion local.
2. SDL trae su propio `gradle-wrapper.jar` en su plantilla; la validacion de wrappers de setup-gradle lo rechazaba -> se elimina en `fetch_sdl.sh` (no se usa).
3. SDL2 declara `SDL_AndroidGetJNIEnv/GetActivity` como `void*` y no incluye `<jni.h>` -> include + conversion explicita.

### Evidencia del usuario (telefono, recibida el 28 oct 2026)

Texto compartido con el boton COMPARTIR LOG, tal como llego:

```
AoE Mobile 0.1.0 (M0) | SDL 2.32.10
LOG:
[00:00.004] == AoE Mobile 0.1.0 (M0) ==
[00:00.006] SDL 2.32.10 | plataforma: android
[00:00.006] archivo de registro: /storage/emulated/0/Android/data/com.aoemobile.engine/files/game.log (ok)
[00:00.077] marco de dibujo 960x540 listo (composicion por software)
[00:04.406] COMPARTIR LOG: intent lanzado
```

Lo que acredita: la app corre en un telefono Android real, SDL 2.32.10 carga, el archivo de registro se escribe en el almacenamiento externo de la app y el boton de compartir funciona. Verificacion humana del render (captura de pantalla): pendiente.

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
