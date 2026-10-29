# STATUS - AoE Mobile

Actualizado: 29 de octubre de 2026. Cada hito lista sus criterios de la especificacion §6 con estado y evidencia. Regla: un hito solo se cierra con TODOS sus criterios cumplidos y evidencia adjunta (spec §2, principio 1).

| Hito | Estado |
| --- | --- |
| M0 Esqueleto | COMPLETO (CI verde + APK instalado + registro y verificacion del usuario) |
| M1 Lectura de datos | **PENDIENTE**: con sinteticos todo pasa (CI), pero los 4 archivos reales del usuario FALLAN el chequeo (13 OK / 4 FALLIDOS en el telefono, 28 oct). La pantalla ocultaba las causas (recorte a 58 chars); corregido en 0.3.1 para ver archivo+causa+offset+hex. Diagnostico en §M1 abajo |
| M2 Mapa visible | CI verde con datos sinteticos (run 40778921591: mapa 64x64, tile del dat, camara, anti-pantalla-negra + APK 993 KB); el tile con el DAT REAL del usuario sigue PENDIENTE (su empires.dat no pasa aun el chequeo; el mapa usa sintetico/documentado) |
| M3 Objetos y centro urbano | no iniciado |
| M4 Unidades que se mueven | no iniciado |
| M5 Economia y construccion (fin Nivel 2) | no iniciado |
| M6 Combate basico | no iniciado |
| M7 Bot v1 (fin Nivel 3) | no iniciado |
| M8 Control tactil, pulido y rendimiento | no iniciado |

## M0 - criterios de aceptacion (COMPLETO)

Runs de CI: https://github.com/Hatblack2002/aoe-android/actions/runs/40600155908 (APK M0, 742 KB) y https://github.com/Hatblack2002/aoe-android/actions/runs/40640469585 (M1: pruebas + APK 981 KB + evidencia).

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
| Pruebas CI con archivos sinteticos propios | OK | `tools/gendata.cpp` genera DRS/SLP/paleta/dat deterministas + 9 variantes corruptas; `scripts/m1_tests.sh` con 11 pasos, TODAS OK |
| Archivos truncados/corruptos fallan con error claro | OK | 9 casos negativos: truncados al 30-60%, cabeceras malas, comando SLP no soportado, fila corta, unit_offset fuera de rango; todos fallan con causa y offset exacto (paso 5) |
| **Evidencia con archivos reales (usuario)** | **FALLIDO → en diagnostico** | 28 oct: telefono con 4 archivos reales importados (Empires.dat, graphics.drs, Interfac.drs, Terrain.drs): **13 OK, 4 FALLIDOS**. Las 4 lineas de ERROR salian recortadas y la causa no se leia (bug de pantalla: recorte a 58 chars); DUMP SLP caia al sintetico en silencio. Corregido en 0.3.1 (ver abajo) |

### Resultado real del telefono (28 oct 2026) y descarte de hipotesis

El usuario importo sus 4 archivos reales con IMPORTAR y el chequeo dio `ASSETCHECK: 13 OK, 4 FALLIDOS` (los 13 sinteticos pasan; los 4 reales fallan la validacion). Los datos tecnicos del resultado:

| Archivo | Tamano declarado por el usuario | Resultado |
| --- | --- | --- |
| Empires.dat | ~2.5 MB | FALLIDO |
| graphics.drs | ~16 MB | FALLIDO |
| Interfac.drs | ~25 MB | FALLIDO |
| Terrain.drs | (sin dato) | FALLIDO |

Causa visible: NINGUNA en el telefono, porque la pantalla recortaba cada linea a 58 caracteres. Eso era un bug de presentacion, ya corregido. Hipotesis planteadas por el usuario y su estado:

1. **(a) Layout de Empires.dat 1997 vs Rise of Rome** — SIGUE VIVA para el dat: el lector esta verificado contra el layout ROR (STATUS/«Formatos verificados»), y la propia nota honesta de `dat.h` advierte que el 1997 puede diferir en alguna constante. Para los DRS en cambio NO explica los fallos: la doc de openage (drs-files.md) y la referencia de 1997 (artho.com/age/drs.html) coinciden en que el DRS de AoE1 usa la MISMA forma que ya implementamos (cabecera 64 B, version "1.00", tablas 12 B, entradas 12 B).
2. **(b) Nombres con mayusculas** — DESCARTADA con evidencia: el validador NO busca nombres fijos, ENUMERA el directorio (`listarArchivos`) y lower-casea la extension (`detectarTipo`); IMPORTAR copia con el nombre original. Prueba automatica nueva (paso 9): `Empires.dat`, `Graphics.drs`, `Interfac.drs`, `Terrain.drs`, `PALETA.PAL` validan 5 OK / 0 FALLIDOS.
3. **(c) Limites de memoria con 16-25 MB** — DESCARTADA como causa de los 4 ERROR: un fallo de memoria mataria el proceso (bad_alloc sin capturar), y en cambio los 4 archivos produjeron lineas `ERROR:` (ErrorDatos de validacion, lanzadas DESPUES de leer el archivo completo). Ademas, los 13 chequeos sinteticos corrieron en el mismo proceso. Prueba automatica nueva (paso 10): un dat de ~26 MB (mayor que Interfac.drs) se carga, se infla y valida sin fallos.

La causa exacta de cada archivo estara en el informe de la 0.3.1: cada ERROR muestra tipo detectado, tamano exacto, causa completa con offset, desglose defensivo de la cabecera (version/tipo/tablas del DRS; tipo de bloque DEFLATE del dat) y los primeros 16 bytes en hex (spec §2, principio 2: herramienta que inspecciona y reporta lo que el archivo realmente contiene).

### Correcciones 0.3.1 (diagnostico con archivos reales)

1. Pantalla: las lineas del informe se ENVUELVEN (nunca se recortan a 58 chars) y van coloreadas por tipo (ERROR rojo, causa, OK verde, resumen ambar).
2. Informe ASSETCHECK estructurado: por directorio (`gen: 15 archivos, 13 OK...` / `import: 4 archivos, 0 OK, 4 FALLIDOS`), cada ERROR con tamano, causa completa, cabecera defensiva y hex; el informe completo sigue quedando en `m1_assetcheck.txt`.
3. DUMP SLP: prueba hasta 12 sprites SLP de los DRS reales en orden; paleta real preferente (bina 50500-50599, si no cualquier bina, si no .pal suelta); la pantalla muestra un LETRERO grande **ORIGEN: REAL** (verde) u **ORIGEN: SINTETICO (reserva)** (ambar) con la causa del recurso; PNGs nombrados `dump_real_*` / `dump_sint_*`. Nunca cae al sintetico en silencio.
4. Extensiones case-insensitive en DUMP/MAPA (`.slp`/`.pal`/`.dat` con mayusculas funcionan).
5. Mensajes DRS enriquecidos: version incorrecta y copyright no imprimible incluyen bytes crudos en hex.
6. Sprites reales enormes: se deciman (vecino-mas-cercano) para caber en su caja.
7. dumpslp (CLI) acepta `CONTENEDOR.drs::ID.ext` (el mismo camino de extraccion que la app), probado en CI (paso 11).
8. Generador: `--drs-realista` (DRS multitabla con copyright del original, forma de interfac.drs) y `--grande` (dat de ~26 MB); pasos 9-11 nuevos en `m1_tests.sh`.

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

## M2 - mapa visible (criterios de aceptacion)

Run de CI: https://github.com/Hatblack2002/aoe-android/actions/runs/40778921591 (pruebas M1+M2 + APK 0.3.0-m2 de 993 KB + evidencia PNG).
| Criterio (spec §6 M2) | Estado | Evidencia |
| --- | --- | --- |
| Mapa de prueba generado por codigo (64x64) con varios terrenos | OK | `src/game/mapa.cpp`: lago con orilla, rio sinuoso, parches de tierra; formulas puras sin azar; `m2_tests` paso 3 verifica 64x64 y los 4 terrenos |
| Dibujo isometrico con camara desplazable | OK | `src/render/isometrico.cpp` (rombos 48x24, teselado exacto); camara por arrastre (dedo/mause/flechas) con limites; `m2_tests` paso 2/3 + PNG `m2_mapa.bmp.png` |
| Modo de depuracion obligatorio (mapa en memoria, mosaicos dibujados/fallidos con causa, camara) | OK | overlay en pantalla con dimensiones, conteo de terrenos, tile y origen, camara, intentados/dibujados/fuera/fallidos + causa; guarda anti-pantalla-negra con banner y FATAL (paso 6) |
| Tamano y proyeccion de mosaicos contra archivos reales | PENDIENTE (corregido el reclamo) | el tile 48x24 (mitad 24x12) viene del dat SINTETICO en CI y de valores documentados en el telefono: el empires.dat real del usuario NO pasa aun el chequeo (M1), por lo que nunca llego a alimentar el tile; se cerrara cuando el dat real valide |
| Evidencia: PNG en CI con sinteticos | OK | `m2_mapa.bmp.png` (960x540, artefacto CI); determinismo byte a byte entre ejecuciones (paso 5) |
| Evidencia: captura del usuario con reales | PENDIENTE | se pide una sola captura del mapa con el dat real importado (no bloquea el avance) |

### Decisiones de M2

- El mapa vive en `src/game/` (sin SDL ni assets): el render solo lo lee (spec §4).
- Proyeccion diamante: pantalla = ((x-y)*medioAncho, (x+y)*medioAlto) + camara; el rombo de 48x24 tesela el plano sin solapes (terreno plano sin elevacion todavia).
- Los limites de camara garantizan que el mapa nunca desaparezca por completo; la demo los fuerza deliberadamente (38 ajustes en 40 cuadros, registrado).
- Prueba negativa propia: `AOE_M2_TEST_NEGRO=1` suprime el dibujo y el binario DEBE salir con error y causa (guarda anti-negro verificada en CI, paso 6).
- `--dat RUTA` en la demo exige un dat legible; truncado o corrupto -> exit 1 con causa (paso 7).
- Exportacion PNG propia (escritor del M1) reutilizada para la evidencia; sin dependencias nuevas.

### Correcciones durante M2 (registro honesto)

1. `leerDat` se llamo sin el namespace `assets::` en la integracion del modo mapa (detectado en compilacion local).
2. Con `--dat` forzado que falla, el codigo hacia fallback silencioso al dat del pref-path; corregido: tile INVALIDO + FATAL (detectado por la propia prueba 7).
3. CI: el runner de GitHub intento auto-instalar el NDK 27.0.12077973 y la descarga fallo (infra, no codigo); fijado `ndkVersion 27.2.12479018` + paso que garantiza el NDK con sdkmanager antes de Gradle.

## Decisiones tecnicas registradas

- SDL2 2.32.10 fijada con SHA-256 (`scripts/fetch_sdl.sh`); nunca se versiona en el repo.
- Render: búfer ARGB8888 propio subido a una textura cada cuadro (spec §3).
- Capa `assets` sin SDL (spec §4): lectores + inflador RFC1951 propio + escritor PNG propio (bloques DEFLATE almacenados, CRC32/Adler-32 propios); cero dependencias nuevas.
- Los sprites se decodifican por demanda y se mantienen como indice de paleta + tipo por pixel (fondo para el presupuesto de RAM).
- Importador de archivos por SAF (ACTION_OPEN_DOCUMENT multiple, sin permisos): Java copia a files/import/ y avisa por JNI; la app re-chequea automaticamente.
- DUMP SLP muestra el sprite en pantalla (captura del telefono como evidencia) y escribe PNG en files/dumps/.
- Color de jugador: indice de paleta = base + indice SLP (base 16*jugador por defecto; el dat real aportara la base por jugador cuando se verifique).
- Determinismo: el generador sintetico no usa azar; las pruebas comparan byte a byte.

## Proximo hito (M3) - checklist

- [ ] Objetos estaticos: arboles, oro, piedra, arbustos de comida (SLP de los sinteticos y del dat real)
- [ ] Un edificio inicial (centro urbano) con sprite SLP + colores de jugador
- [ ] Orden de dibujo por profundidad (lo mas abajo en pantalla se dibuja encima)
- [ ] Colocacion determinista de objetos sobre el mapa de prueba
- [ ] Evidencia: PNG en CI con sinteticos + captura del usuario con reales

## Lo que el usuario debe hacer para diagnosticar M1 (0.3.1)

1. Instalar el APK nuevo (artefacto del ultimo run de CI, version 0.3.1).
2. ASSETCHECK (sin necesidad de re-importar: los archivos siguen en import/): ahora las 4 causas COMPLETAS se leen en pantalla (envueltas, con offset y hex). La seccion `import:` lista cada archivo con su tamano exacto.
3. COMPARTIR: el texto incluye el informe completo (y queda en `m1_assetcheck.txt`). Enviarlo tal cual.
4. DUMP SLP: la pantalla dira ORIGEN: REAL (verde) si algun sprite de los DRS reales decodifica, u ORIGEN: SINTETICO (reserva) (ambar) con la causa exacta de cada DRS reprobado.
5. Con ese informe se cierra M1: o los errores revelan un ajuste concreto del formato (se corrige con evidencia, spec §8), o los archivos pasan y queda la evidencia REAL en pantalla.
