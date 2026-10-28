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

} // namespace assets
