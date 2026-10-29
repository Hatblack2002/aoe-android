#pragma once
// Generador de datos sinteticos para pruebas (spec §8: "una herramienta
// propia genera archivos DRS, SLP y paletas de prueba"). Crea:
//   prueba.pal                 paleta JASC-PAL de 256 entradas (formula determinista)
//   prueba.slp                 SLP 2.0N de 3 marcos que ejercita TODOS los comandos
//   prueba.drs                 DRS con el SLP (tabla ' pls') y la paleta (tabla 'anib')
//   empires_test.dat           dat ROR completo, comprimido con DEFLATE almacenado
//   empires_test_payload.bin   el mismo dat SIN comprimir (para validacion cruzada
//                              contra zlib en las pruebas)
//   trunc50.drs / trunc60.slp / trunc40.dat / trunc30.pal
//   drs_badmagic.drs / slp_badcmd.slp / slp_rowshort.slp / dat_badptr.dat /
//   pal_badheader.pal          variantes corruptas que DEBEN fallar con error claro
//   MANIFIESTO.txt             valores esperados (para revisión humana y del CI)
//
// Todo determinista (sin azar): el marco 1 del SLP tiene el patron
// indice(x,y) = (x + 16*y) mod 256, que las pruebas verifican pixel a pixel.

#include <string>

namespace assets {

// Escribe todos los archivos en `dir` (sin barra final). Lanza ErrorDatos si
// no puede escribir. Devuelve el numero de archivos generados.
int generarDatosPrueba(const std::string& dir);

// Extra (pruebas CI): DRS "estilo real" con 3 tablas (' pls', 'anib', ' wav')
// y 5 recursos, con el copyright del original documentado en artho.com
// (INTERFAC.DRS es el DRS con varias tablas). Devuelve 1 o -1 al no poder
// escribir.
int generarDrsRealista(const std::string& dir);

// Extra (pruebas CI): dat valido de ~26 MB (payload + cola de ceros,
// comprimido en bloques almacenados). Sirve para DESCARTAR los limites de
// memoria como causa de fallo con archivos reales de 16-25 MB.
int generarGrande(const std::string& dir, unsigned mb);

} // namespace assets
