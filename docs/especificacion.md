# Especificación: AoE nativo para Android (Niveles 1 a 3)

Versión 1, 27 de octubre de 2026. Documento pensado para entregarse a un agente de programación (GLM-5.3, Claude u otro) y para que la persona que lo dirige pueda verificar cada avance desde un teléfono, sin laptop.

## 1. Objetivo y alcance

**Objetivo:** un motor nativo para Android ARM64 que lea los archivos del Age of Empires original (propiedad del usuario) y permita jugar una partida contra un bot, con control táctil, usando poca RAM.

**Dentro del alcance (Niveles 1 a 3):**

- Nivel 1: mapa visible, objetos y unidades dibujadas.
- Nivel 2: aldeanos que se mueven, recolectan, construyen; control táctil; jugable contra un rival pasivo.
- Nivel 3: un bot sencillo que construye, se defiende y ataca.

**Fuera del alcance (por ahora):** multijugador, campañas, editor de escenarios, mods, todas las civilizaciones y tecnologías (se empieza con una civilización y pocas unidades), compatibilidad exacta con partidas guardadas originales.

**Criterio de éxito honesto:** llegar al Nivel 2 ya es un resultado valioso. El Nivel 3 es el objetivo. El juego completo y fiel NO es un objetivo de esta especificación.

## 2. Principios (obligatorios para el agente)

1. **Nada se da por terminado sin evidencia reproducible** (prueba automática, captura de imagen o registro). Un “debería funcionar” no cuenta.
2. **No inventar detalles de formatos de archivo de memoria.** Verificarlos contra la documentación de openage y contra los archivos reales. Si hay duda, escribir una herramienta que inspeccione el archivo y reportar lo que realmente contiene.
3. **Falla cerrada:** ante datos inesperados, el motor registra el error y se detiene con un mensaje claro; nunca muestra una pantalla negra en silencio.
4. **Separar capas:** los datos, la simulación, el dibujo y el control no se mezclan. Un fallo de dibujo no debe parecer un fallo de lógica.
5. **Medir RAM y cuadros por segundo desde el primer día**, no al final.
6. **Avanzar por hitos pequeños.** Cada hito deja algo que funciona y que se puede probar.
7. **Respetar licencias.** Se pueden estudiar otros proyectos (openage, FolkertVanVerseveld/aoe) como documentación, pero no se copia código sin verificar su licencia y respetarla.

## 3. Restricciones técnicas

- **Lenguaje:** C++17. **Biblioteca de plataforma:** SDL2 (ventana, entrada táctil, audio). Compilación con Android NDK, arquitectura arm64-v8a.
- **Sistema de compilación:** CMake. **Integración continua:** GitHub Actions, que debe producir un APK descargable como artefacto en cada cambio.
- **Dispositivo objetivo mínimo:** teléfono de 4 GB de RAM con Android moderno. Meta de pico de memoria del juego: menos de 250 MB. Meta de rendimiento: 30 cuadros por segundo estables.
- **Dibujo:** empezar con composición por software a 8 bits con paleta (como el original) en un búfer de 32 bits que se sube como textura cada cuadro. Es lo más simple, lo más predecible y permite generar imágenes de prueba sin GPU. Optimizar con GPU solo si las mediciones lo exigen.
- **Memoria:** cargar sprites bajo demanda y descartar los que no se usan. Mantener los sprites en 8 bits (un cuarto del tamaño de 32 bits).
- **Sin laptop:** todo debe poder construirse y probarse desde GitHub Actions y un teléfono.

## 4. Arquitectura (módulos)

| Módulo | Responsabilidad | Depende de |
| --- | --- | --- |
| `assets` | Leer archivos DRS, SLP, paletas, tabla de datos de unidades; caché con límite de memoria | nada |
| `sim` | Estado del juego: mapa, objetos, unidades, economía, combate. Sin dibujo ni entrada | `assets` (solo datos) |
| `render` | Dibujar el estado: terreno, objetos, unidades, interfaz | `sim` (solo lectura), `assets` |
| `input` | Convertir toques en órdenes para `sim` | `sim` |
| `ai` | El bot: da órdenes a `sim` igual que un jugador | `sim` |
| `platform` | Ventana, bucle principal, registro de eventos, audio, medición de RAM y cuadros | SDL2 |
| `tools` | Programas de inspección y prueba (ver sección 8) | los anteriores |

**Regla clave:** `sim` no sabe que existe un renderizador. La IA y el jugador humano usan la misma interfaz de órdenes.

## 5. Determinismo (decisión de diseño desde el día uno)

- La simulación avanza en pasos fijos (por ejemplo 10 pasos por segundo), independientes de los cuadros por segundo.
- Se usa aritmética de enteros o punto fijo en la lógica del juego. Prohibido el uso de números aleatorios del sistema: se usa un generador propio con semilla.
- Mismo estado inicial más mismas órdenes debe dar siempre el mismo resultado. Hay una prueba automática que lo comprueba (grabar órdenes, repetir, comparar un resumen del estado).
- Esto permite depurar partidas, probar la IA sin pantalla y, si algún día se quiere, añadir multijugador.

## 6. Hitos y criterios de aceptación

Cada hito termina cuando se cumplen TODOS sus criterios de aceptación y se adjunta la evidencia indicada.

### M0. Esqueleto

- CMake + SDL2 + NDK. GitHub Actions genera el APK.
- La app abre, muestra un color, la versión del programa y las últimas líneas del registro.
- El registro también se guarda en un archivo que el usuario pueda compartir.
- **Evidencia:** APK descargable de la integración continua; captura de pantalla del teléfono por el usuario.

### M1. Lectura de datos

- Leer contenedores DRS: listar recursos por tipo e identificador.
- Decodificar SLP (sprites) y paletas.
- Leer la tabla de datos de unidades y edificios (verificar formato real, incluida su compresión).
- Herramienta `assetcheck`: imprime conteos y valida que todo se lee sin errores.
- Herramienta `dumpslp`: guarda un sprite como imagen PNG.
- **Pruebas en integración continua:** con archivos sintéticos generados por una herramienta propia (no se suben archivos del juego al repositorio). Pruebas de archivos truncados y corruptos que deben fallar con un error claro.
- **Evidencia con archivos reales:** el usuario ejecuta `assetcheck` y `dumpslp` en su teléfono y envía el resultado (texto e imagen).

### M2. Mapa visible (aquí vive el bug del mapa negro)

- Mapa de prueba generado por código (por ejemplo 64x64), con varios tipos de terreno.
- Dibujo isométrico de terreno con cámara que se desplaza.
- **Modo de depuración obligatorio:** una superposición que muestra por separado (a) que el mapa existe en memoria, con sus dimensiones; (b) cuántos mosaicos se intentaron dibujar y cuántos fallaron, con la razón; (c) la posición de la cámara. Si algo falla, el motor lo dice; nunca queda negro sin explicación.
- Verificar el tamaño y la proyección de los mosaicos contra los archivos reales, no de memoria.
- **Evidencia:** imagen PNG generada en integración continua con datos sintéticos; captura del usuario con datos reales.

### M3. Objetos y centro urbano

- Colocar objetos estáticos (árboles, oro, piedra, arbustos de comida) y un edificio inicial.
- Orden correcto de dibujo por profundidad (lo que está más abajo en pantalla se dibuja encima).
- Colores de jugador (reemplazo de colores en el sprite).
- **Evidencia:** imagen PNG; captura del usuario.

### M4. Unidades que se mueven

- Aldeanos con animaciones de caminar y quieto, en 8 direcciones.
- Selección (toque y arrastre de rectángulo) y orden de mover.
- Búsqueda de rutas A\* sobre la cuadrícula con evitación simple de obstáculos. No se persigue calidad perfecta todavía.
- Prueba automática: una unidad llega a un destino alcanzable y falla de forma limpia ante uno inalcanzable.
- **Evidencia:** prueba automática y vídeo corto del usuario.

### M5. Economía y construcción

- Recolectar madera, comida, oro y piedra; llevar al punto de entrega; contadores de recursos en pantalla.
- Construir edificios (casa y al menos un edificio de producción) con colocación, cancelación y costo.
- Cola de producción de unidades.
- **Esto completa el Nivel 2** (con un rival pasivo).
- **Evidencia:** prueba automática de economía (tras N pasos, los recursos son exactamente X) y vídeo del usuario.

### M6. Combate básico

- Atacar, puntos de vida, muerte, edificios destruibles.
- Dos tipos de unidades militares como mínimo.
- **Evidencia:** prueba automática de combate determinista.

### M7. Bot v1 (Nivel 3)

- El bot usa solo la interfaz de órdenes, igual que un humano.
- Reglas simples y legibles (en datos o en una tabla, no enterradas en el código): aumentar aldeanos hasta un número, construir casas cuando falte población, construir un edificio militar, entrenar un ejército, atacar cuando tenga N unidades, reaccionar si lo atacan.
- No hace trampa: no recibe recursos extra ni ve el mapa oculto.
- **Evidencia:** una partida simulada sin pantalla donde el bot construye una economía y ataca; el registro de decisiones del bot en texto.

### M8. Control táctil, pulido y rendimiento

- Control completo según la sección 7.
- Medición de RAM y cuadros por segundo en un teléfono de 4 GB; informe en texto.
- Si se excede el presupuesto, se corrige antes de añadir nada nuevo.
- **Evidencia:** informe de rendimiento del usuario.

### Después (opcional, solo si M8 está sólido)

Más edades y tecnologías, más civilizaciones, mejoras del pathfinding, y la parte que cada jugador reconocerá de AoE: murallas y puertas.

## 7. Especificación del control táctil

Diseñado para el dedo, no como imitación del ratón.

- **Seleccionar:** tocar una unidad. Arrastrar un rectángulo con un dedo para varias. Botón “aldeanos inactivos”.
- **Mover, recolectar o atacar:** tocar un punto con unidades seleccionadas. El juego decide la acción según lo que hay ahí (como el clic derecho original).
- **Cámara:** arrastre con dos dedos; también con un dedo si no hay selección activa. Minimapa tocable para saltar a una zona. Pellizco para zoom.
- **Construir:** menú grande de edificios; tocar para elegir, tocar en el mapa para colocar y botones grandes de confirmar y cancelar. El edificio fantasma se muestra desplazado para que el dedo no lo tape.
- **Tamaño mínimo de botones:** unos 48 px equivalentes.
- **Pausa:** botón siempre visible.
- **Atajos de teclado:** se convierten en botones contextuales que aparecen solo cuando son útiles.
- **Versión mínima (M4):** tocar para seleccionar, tocar para mover, arrastrar para mover la cámara.

## 8. Protocolo de verificación (sin laptop)

**Herramientas obligatorias:**

- `assetcheck`: valida y cuenta los datos reales.
- `dumpslp`: guarda un sprite como PNG.
- `rendertest`: dibuja un cuadro del mapa sin ventana y lo guarda como PNG (funciona en integración continua con datos sintéticos).
- `simtest`: ejecuta una simulación sin pantalla, imprime un resumen del estado y su suma de verificación.
- `perfreport`: informa RAM y cuadros por segundo.

**Cómo informa el usuario:** copia y envía al agente (a) la salida de texto de la herramienta, (b) una captura de pantalla, (c) el archivo de registro. El agente debe poder diagnosticar solo con eso.

**Datos sintéticos:** una herramienta propia genera archivos DRS, SLP y paletas de prueba pequeños. Así la integración continua verifica el código sin ningún archivo del juego original.

**Si algo falla:** el agente no adivina. Pide exactamente un dato concreto (por ejemplo, “ejecuta `assetcheck` y pega la salida”) y explica qué hipótesis va a descartar con él.

## 9. Aspectos legales y de archivos

- El repositorio nunca contiene archivos del juego (ejecutables, DRS, sonidos, gráficos), ni claves, ni rutas personales.
- El usuario aporta sus archivos originales, en su propio dispositivo, después de que el motor pase M0 y M1 con datos sintéticos.
- Si el repositorio es público, el motor solo contiene código propio. Verificar y respetar la licencia de cualquier código de otros proyectos que se consulte o se reutilice.

## 10. Instrucciones para el agente (copiar al inicio de la sesión)

Eres el agente de programación de este proyecto. Sigue esta especificación al pie de la letra.

1. Trabaja un hito a la vez, en orden. No empieces el siguiente hasta cumplir los criterios del actual.
2. Mantén un archivo `STATUS.md` con la lista de hitos, qué criterios están cumplidos y con qué evidencia.
3. No afirmes que algo funciona sin una prueba, una imagen generada o un registro que lo demuestre.
4. No supongas detalles de formatos de archivo, tamaños de mosaico ni identificadores de recursos. Verifícalos con documentación y con herramientas de inspección.
5. Cuando necesites algo de la persona (probar en el teléfono, un archivo, una captura), pide una sola cosa concreta y di cómo la vas a usar.
6. Commits pequeños, mensajes claros, integración continua verde antes de pedir pruebas manuales.
7. Si algo es inviable o excede el presupuesto de memoria, dilo con datos y propón un recorte. No lo ocultes.
8. No añadas funciones fuera del alcance (multijugador, campañas, editor).

## 11. Riesgos conocidos

- **Formatos sin documentación completa:** parte de los datos de AoE se conoce por ingeniería inversa de la comunidad. Mitigación: herramientas de inspección y verificación contra archivos reales.
- **Pathfinding:** es históricamente la parte más difícil de pulir en esta familia de juegos. Mitigación: A\* simple primero, mejora después.
- **IA fiel al original:** no es objetivo. Se busca una IA sencilla que dé la sensación de construir y atacar.
- **Ciclo de pruebas lento por no tener laptop:** mitigación con datos sintéticos, imágenes generadas en integración continua y herramientas que producen texto fácil de copiar.
- **Agotamiento del autor:** mitigación con hitos pequeños y evidencia visible desde M2.

## 12. Referencias a consultar (no copiar sin verificar licencia)

- openage (SFTtech/openage): documentación técnica del motor Genie y de los formatos de archivo.
- FolkertVanVerseveld/aoe: reimplementación de AoE y Rise of Rome, útil como referencia de formatos y comportamiento.
