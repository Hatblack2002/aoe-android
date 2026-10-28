#pragma once
// Paleta del juego (archivos 505XX dentro de interfac.drs).
// FORMATO VERIFICADO (no inventado):
//  - Son archivos de TEXTO JASC-PAL: cabecera "JASC-PAL\r\n0100\r\n256\r\n"
//    seguida de 256 lineas "R G B". Ver openage convert/value_object/read/
//    media/colortable.py (ColorTable::fill) y FolkertVanVerseveld/aoe
//    game/src/legacy/palette.cpp (DRS::open_pal).
//  - En el DRS viajan con extension "anib" (bytes 'anib' en archivo, forma
//    invertida de "bina"; ver openage doc/media/drs-files.md).

#include <array>
#include <cstdint>

namespace assets {

struct EntradaPaleta {
    uint8_t r, g, b;
};

// Tabla de 256 colores indexada (como la del juego).
using Paleta = std::array<EntradaPaleta, 256>;

// Parsea el texto JASC-PAL completo. Lanza ErrorDatos ante cabecera
// incorrecta, conteo distinto de 256 o valores fuera de 0..255.
Paleta leerPaleta(const uint8_t* datos, unsigned long tam);

} // namespace assets
