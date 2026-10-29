#pragma once
// Autoprueba del decodificador SLP contra la especificacion verificada de
// openage (doc/media/slp-files.md, version 2.0N): construye en memoria un SLP
// que ejercita TODAS las clases de comandos, lo decodifica y compara PIXEL A
// PIXEL con la matriz esperada. Fallo cerrado: si algo difiere, el detalle
// dice fila, columna, esperado y obtenido.

#include <string>

namespace assets {

// Devuelve true si el decodificador pasa todas las comprobaciones (positivas
// y negativas). En `detalle` deja un resumen de lo verificado o la primera
// divergencia exacta.
bool probarDecodificadorSLP(std::string* detalle);

} // namespace assets
