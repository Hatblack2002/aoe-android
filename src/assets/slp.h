#pragma once
// Decodificador de sprites SLP version "2.0N" (la del AoE1 1997, AoK y SWGB).
// FORMATO VERIFICADO contra openage: doc/media/slp-files.md y el decodificador
// real openage/convert/value_object/read/media/slp.pyx (dispatch de comandos,
// cmd_or_next, contornos 0x4E/0x5E/0x6E/0x7E). Las versiones 3.0/4.x (solo
// Definitive Editions) NO estan soportadas y fallan con mensaje claro.
//
// Salida: por pixel, el indice de paleta (para pixeles normales y de relleno)
// o el indice de color de jugador (0..9 en AoE1) mas el tipo, para que el
// render componga con la paleta y el color de jugador que correspondan.
//
// Fallo cerrado (spec §2): comando desconocido, fila incompleta, archivo
// truncado o desplazamiento fuera de rango -> ErrorDatos con detalle exacto.

#include <cstdint>
#include <string>
#include <vector>

namespace assets {

enum class PixelTipo : uint8_t {
    Transparente = 0,
    Normal = 1,        // indice de paleta
    Jugador = 2,       // indice de color de jugador (0..9 en AoE1)
    Sombra = 3,        // oscurecer lo que hay debajo
    ContornoJugador = 4,  // 0x4E/0x5E: color de jugador si esta obstruido
    Contorno2 = 5,        // 0x6E/0x7E: indice de paleta 243 (escudo en SWGB)
};

struct PixelSLP {
    uint8_t indice;
    PixelTipo tipo;
};

struct MarcoSLP {
    int32_t ancho = 0;
    int32_t alto = 0;
    int32_t anclaX = 0;  // "hotspot" del original: centro del sprite
    int32_t anclaY = 0;
    uint32_t propiedades = 0;
    std::vector<PixelSLP> pixeles;  // ancho*alto, por filas
};

struct InfoSLP {
    std::string version;
    int32_t numMarcos = 0;
    std::string comentario;
};

// Decodificador sobre los bytes del archivo .slp (o de la entrada extraida
// de un DRS). Los marcos se decodifican bajo demanda para respetar el
// presupuesto de RAM (spec §3).
class DecodificadorSLP {
public:
    DecodificadorSLP(const uint8_t* datos, unsigned long tam, std::string nombreArchivo);

    const InfoSLP& info() const { return info_; }
    int32_t numMarcos() const { return info_.numMarcos; }

    // Decodifica el marco i (0-based).
    MarcoSLP decodificarMarco(int32_t i) const;

private:
    const uint8_t* datos_;
    unsigned long tam_;
    std::string archivo_;
    InfoSLP info_;
};

} // namespace assets
