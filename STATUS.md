# STATUS - AoE Mobile

Actualizado: 28 de octubre de 2026. Cada hito lista sus criterios de la especificacion §6 con estado y evidencia. Regla: un hito solo se cierra con TODOS sus criterios cumplidos y evidencia adjunta (spec §2, principio 1).

| Hito | Estado |
| --- | --- |
| M0 Esqueleto | COMPLETO (CI verde + APK instalado + registro y verificacion del usuario) |
| M1 Lectura de datos | CI verde con datos sinteticos; pendiente ejecutar assetcheck/dumpslp en el telefono con archivos reales |
| M2 Mapa visible | no iniciado |
| M3 Objetos y centro urbano | no iniciado |
| M4 Unidades que se mueven | no iniciado |
| M5 Economia y construccion (fin Nivel 2) | no iniciado |
| M6 Combate basico | no iniciado |
| M7 Bot v1 (fin Nivel 3) | no iniciado |
| M8 Control tactil, pulido y rendimiento | no iniciado |

## M0 - criterios de aceptacion (COMPLETO)

Runs de CI: https://github.com/Hatblack2002/aoe-android/actions/runs/40600155908 (APK M0) y el run de M1 (mismo flujo, version 0.2.0).

| Criterio (spec §6 M0) | Estado | Evidencia |
| --- | --- | --- |
| CMake + SDL2 + NDK | OK | `CMakeLists.txt` raiz compartido por escritorio y Android |
| GitHub Actions genera el APK | OK | artefacto `aoe-mobile-M0-debug-apk` (742 KB), instalado por el usuario |
| La app abre y muestra un color | OK | captura de CI + verificacion humana: "la captura que me mandaste es la misma que yo veo" (28 oct 2026) |
| Muestra la version del programa | OK | "AoE Mobile 0.1.0 (M0)" en pantalla y en el registro del telefono |
| Muestra las ultimas lineas del registro | OK | anillo visible en pantalla |
| El registro se guarda en archivo compartible | OK | `game.log` en `/storage/emulated/0/Android/data/com.aoemobile.engine/files/`; boton COMPARTIR LOG (`intent lanzado`) |
| Captura de pantalla del telefono | OK (por el usuario) | el usuario confirma que la pantalla coincide con la captura de CI; log compartido desde el telefono |

### Evidencia del usuario (telefono, 28 oct 2026)

Log compartido con el boton COMPARTIR LOG, tal como llego:

```
AoE Mobile 0.1.0 (M0) | SDL 2.32.10
LOG:
[00:00.004] == AoE Mobile 0.1.0 (M0) ==
[00:00.006] SDL 2.32.10 | plataforma: android
[00:00.006] archivo de registro: /storage/emulated/0/Android/data/com.aoemobile.engine/files/game.log (ok)
[00:00.077] marco de dibujo 960x540 listo (composicion por software)
[00:04.406] COMPARTIR LOG: intent lanzado
```

El usuario confirmo ademas que lo que ve en el telefono coincide con la captura de la autoprueba de CI (mismo binario nativo en ambos lados).

### Correcciones durante la puesta en verde (registro honesto)

1. `compartirTexto()` se llamaba fuera del guard `__ANDROID__` -> error detectado en compilacion local.
2. SDL trae su propio `gradle-wrapper.jar` en su plantilla; la validacion de wrappers de setup-gradle lo rechazaba -> se elimina en `fetch_sdl.sh` (no se usa).
3. SDL2 declara `SDL_AndroidGetJNIEnv/GetActivity` como `void*` y no incluye `<jni.h>` -> include + conversion explicita.

## M1 - lectura de datos (criterios de aceptacion)

| Criterio (spec §6 M1) | Estado | Evidencia |
| --- | --- | --- |
| Leer contenedores DRS (listar por tipo e id) | OK (CI) | `src/assets/drs.cpp` según openage `doc/media/drs-files.md` + `drs.py`; assetcheck lista tablas `slp`/`bina`; pruebas en `scripts/m1_tests.sh` paso 2 |
| Decodificar SLP | OK (CI) | `src/assets/slp.cpp` (version 2.0N, comandos 0x00..0x0F según openage `slp-files.md` + `slp.pyx`); marco 1 verificado PIXEL A PIXEL en CI (paso 4) |
| Decodificar paletas | OK (CI) | JASC-PAL de texto, verificado en openage `colortable.py` y FolkertVanVerseveld/aoe `palette.cpp` (¡no es BGR crudo!); paso 2 |
| Leer tabla de unidades/edificios (con su compresion) | OK (CI) | `empires.dat` = DEFLATE crudo (openage `gamedata.py`: zlib -15) + estructura ROR campo a campo (openage `datfile/`); inflador propio RFC1951; cross-check contra zlib real de python (paso 6); 9 tipos de unidad (10..90) parseados (paso 2) |
| Herramienta assetcheck | OK | `tools/assetcheck.cpp` + modulo compartido `src/assets/chequeo.cpp` (la app Android usa el mismo criterio) |
| Herramienta dumpslp (PNG) | OK | `tools/dumpslp.cpp` + escritor PNG propio (`src/assets/png.cpp`); 4 PNGs publicados como artefacto |
| Pruebas CI con archivos sinteticos propios | OK | `tools/gendata.cpp` genera DRS/SLP/paleta/dat deterministas + 9 variantes corruptas; `scripts/m1_tests.sh` con 8 pasos, TODAS OK |
| Archivos truncados/corruptos fallan con error claro | OK | 9 casos negativos: truncados al 30-60%, cabeceras malas, comando SLP no soportado, fila corta, unit_offset fuera de rango; todos fallan con causa y offset exacto (paso 5) |
| **Evidencia con archivos reales (usuario)** | PENDIENTE | el usuario ejecuta en el telefono: ASSETCHECK (tras IMPORTAR sus archivos) y DUMP SLP, y comparte texto + captura |

### Formatos verificados contra documentacion (spec §2, principio 2)

Nada se implemento de memoria. Fuentes consultadas (repo openage, rama master, octubre 2026):

- DRS: `doc/media/drs-files.md` y `convert/value_object/read/media/drs.py` (cabecera 64 B, tablas 12 B, extension invertida en archivo: ' pls' = SLP, 'anib' = binario/paleta).
- SLP: `doc/media/slp-files.md` y `convert/value_object/read/media/slp.pyx` (version 2.0N del AoE1/AoK; comandos, contornos 0x4E/0x5E/0x6E/0x7E, filas 0x8000).
- Paleta: `convert/value_object/read/media/colortable.py` (JASC-PAL texto) + `game/src/legacy/palette.cpp` de FolkertVanVerseveld/aoe.
- empires.dat: `convert/service/read/gamedata.py` (DEFLATE crudo, wbits -15) + `convert/value_object/read/media/datfile/{empiresdat,civ,unit,terrain,maps,tech,playercolor,sound}.py` (layout ROR; PlayerColor ROR = name[30]+i16+i16+u8+u8; UnitCommand no-DE = 59 B).

Advertencia honesta: el layout del dat esta verificado para Rise of Rome. Para el AoE1 original (1997) puede diferir en alguna constante; si assetcheck tropezara con el archivo real, reportara el desplazamiento exacto y se corregira con esa evidencia (spec §8: no adivinar).

### Correcciones durante M1 (registro honesto)

1. El generador escribia offsets de comandos SLP relativos: en el formato son absolutos al archivo (detectado por las propias pruebas).
2. Orden de campos Sound en el lector (sound_id/play_delay/file_count/cache_time) corregido.
3. MapLand/MapUnit son 44 B (no 48) y Map tiene 9 enteros fijos; verificado contra maps.py.
4. Los cuerpos de Graphic no viven en el flujo secuencial: se alcanzan por puntero (el generador los movio a la cola no parseada).
5. PlayerColor en ROR es distinto al de AoK (sin player_color_base): corregido antes de escribir el lector.

## Decisiones tecnicas registradas

- SDL2 2.32.10 fijada con SHA-256 (`scripts/fetch_sdl.sh`); nunca se versiona en el repo.
- Render: búfer ARGB8888 propio subido a una textura cada cuadro (spec §3).
- Capa `assets` sin SDL (spec §4): lectores + inflador RFC1951 propio + escritor PNG propio (bloques DEFLATE almacenados, CRC32/Adler-32 propios); cero dependencias nuevas.
- Los sprites se decodifican por demanda y se mantienen como indice de paleta + tipo por pixel (fondo para el presupuesto de RAM).
- Importador de archivos por SAF (ACTION_OPEN_DOCUMENT multiple, sin permisos): Java copia a files/import/ y avisa por JNI; la app re-chequea automaticamente.
- DUMP SLP muestra el sprite en pantalla (captura del telefono como evidencia) y escribe PNG en files/dumps/.
- Color de jugador: indice de paleta = base + indice SLP (base 16*jugador por defecto; el dat real aportara la base por jugador cuando se verifique).
- Determinismo: el generador sintetico no usa azar; las pruebas comparan byte a byte.

## Proximo hito (M2) - checklist

- [ ] Mapa de prueba generado por codigo (64x64) con varios terrenos
- [ ] Dibujo isometrico de terreno con camina (camara desplazable)
- [ ] Modo depuracion obligatorio (mapa en memoria, mosaicos dibujados/fallidos con causa, camara)
- [ ] Verificar tamano/proyeccion de mosaicos contra archivos reales (dat: tile=48x24, mitad 24x12)
- [ ] Evidencia: PNG en CI con sinteticos + captura del usuario con reales

## Lo que el usuario debe hacer para cerrar M1 (una sola cosa por paso)

1. Instalar el APK nuevo (artefacto `aoe-mobile-M1-debug-apk` del ultimo run de CI).
2. Tocar GENERAR (crea datos sinteticos) y luego ASSETCHECK: debe decir `12 OK, 0 FALLIDOS`.
3. Tocar DUMP SLP: aparece el sprite sintetico y el PNG queda en files/dumps/.
4. IMPORTAR: elegir sus archivos reales del AoE1 (interfac.drs, graphics.drs, empires.dat; seleccion multiple) y el chequeo corre solo.
5. COMPARTIR: enviar el informe por texto; y una captura de pantalla del sprite con datos reales.
