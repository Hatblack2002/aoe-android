// dumpslp: decodifica un marco de un SLP y lo guarda como PNG (spec §8).
// Uso:
//   dumpslp ARCHIVO.slp MARCO PALETA SALIDA.png [--player N] [--base N]
//                  [--bg checker|white|black]
//
// Color de jugador: los pixeles tipo Jugador usan indice_paleta =
// base + indice (formula de la doc de openage; en AoK base = 16*jugador).
// --player fija el jugador 1..8 (base 16*N por defecto); --base fuerza la
// base exacta (el motor usara la del dat cuando haya archivo real).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "assets/formato.h"
#include "assets/inflate.h"
#include "assets/paleta.h"
#include "assets/png.h"
#include "assets/slp.h"

namespace {

assets::RGB componer(const assets::PixelSLP& p, const assets::Paleta& pal, int base,
                     int fondo) {
    switch (p.tipo) {
        case assets::PixelTipo::Normal:
            return {pal[p.indice].r, pal[p.indice].g, pal[p.indice].b};
        case assets::PixelTipo::Jugador: {
            const int indice = base + p.indice;
            if (indice < 0 || indice > 255) {
                throw assets::ErrorDatos{"dumpslp: indice de jugador fuera de paleta (" +
                                         std::to_string(indice) + ")"};
            }
            return {pal[indice].r, pal[indice].g, pal[indice].b};
        }
        case assets::PixelTipo::Sombra:
            // Sin paleta de sombra separada: mezcla 50% con el fondo.
            return {static_cast<uint8_t>(fondo / 2), static_cast<uint8_t>(fondo / 2),
                    static_cast<uint8_t>(fondo / 2)};
        case assets::PixelTipo::ContornoJugador: {
            const int indice = base + 0;
            return {pal[indice].r, pal[indice].g, pal[indice].b};
        }
        case assets::PixelTipo::Contorno2:
            return {pal[243].r, pal[243].g, pal[243].b};
        case assets::PixelTipo::Transparente:
        default: {
            // Tablero de dos grises para distinguir la transparencia.
            static int par = 0;
            (void)par;
            return {static_cast<uint8_t>(fondo), static_cast<uint8_t>(fondo),
                    static_cast<uint8_t>(fondo)};
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string rutaSlp, rutaPal, rutaSalida;
    int marco = 0, jugador = 1, base = -1, fondo = 255;
    bool tablero = true;

    std::vector<std::string> pos;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--player" && i + 1 < argc) {
            jugador = atoi(argv[++i]);
        } else if (a == "--base" && i + 1 < argc) {
            base = atoi(argv[++i]);
        } else if (a == "--bg" && i + 1 < argc) {
            const std::string bg = argv[++i];
            if (bg == "checker") tablero = true;
            else if (bg == "white") { tablero = false; fondo = 255; }
            else if (bg == "black") { tablero = false; fondo = 0; }
            else {
                fprintf(stderr, "dumpslp: fondo desconocido \"%s\"\n", bg.c_str());
                return 2;
            }
        } else {
            pos.push_back(a);
        }
    }
    if (pos.size() != 4) {
        fprintf(stderr,
                "uso: dumpslp ARCHIVO.slp MARCO PALETA SALIDA.png [--player N] [--base N]\n"
                "     [--bg checker|white|black]\n");
        return 2;
    }
    rutaSlp = pos[0];
    marco = atoi(pos[1].c_str());
    rutaPal = pos[2];
    rutaSalida = pos[3];
    if (jugador < 1 || jugador > 8) {
        fprintf(stderr, "dumpslp: jugador %d fuera de 1..8\n", jugador);
        return 2;
    }
    if (base < 0) base = 16 * jugador;

    try {
        // Cargar SLP.
        FILE* f = fopen(rutaSlp.c_str(), "rb");
        if (!f) {
            fprintf(stderr, "dumpslp: no se pudo abrir %s\n", rutaSlp.c_str());
            return 1;
        }
        std::vector<uint8_t> bytes;
        uint8_t bufer[65536];
        size_t n;
        while ((n = fread(bufer, 1, sizeof(bufer), f)) > 0) bytes.insert(bytes.end(), bufer, bufer + n);
        fclose(f);

        // Cargar paleta.
        f = fopen(rutaPal.c_str(), "rb");
        if (!f) {
            fprintf(stderr, "dumpslp: no se pudo abrir %s\n", rutaPal.c_str());
            return 1;
        }
        std::vector<uint8_t> bytesPal;
        while ((n = fread(bufer, 1, sizeof(bufer), f)) > 0) bytesPal.insert(bytesPal.end(), bufer, bufer + n);
        fclose(f);
        const assets::Paleta pal = assets::leerPaleta(bytesPal.data(), bytesPal.size());

        // Decodificar y componer.
        assets::DecodificadorSLP slp(bytes.data(), bytes.size(), rutaSlp);
        const assets::MarcoSLP m = slp.decodificarMarco(marco);
        std::vector<assets::RGB> pixeles;
        pixeles.reserve(m.pixeles.size());
        for (int y = 0; y < m.alto; ++y) {
            for (int x = 0; x < m.ancho; ++x) {
                const assets::PixelSLP& p = m.pixeles[static_cast<std::size_t>(y) * m.ancho + x];
                int fndo = fondo;
                if (tablero && p.tipo == assets::PixelTipo::Transparente) {
                    fndo = ((x / 4 + y / 4) % 2) ? 210 : 170;
                }
                pixeles.push_back(componer(p, pal, base, fndo));
            }
        }
        std::string error;
        if (!assets::escribirPngArchivo(rutaSalida, m.ancho, m.alto, pixeles, &error)) {
            fprintf(stderr, "dumpslp: %s\n", error.c_str());
            return 1;
        }
        printf("DUMPSLP OK: %s marco %d (%dx%d, ancla %d,%d) + %s -> %s\n",
               rutaSlp.c_str(), marco, m.ancho, m.alto, m.anclaX, m.anclaY,
               rutaPal.c_str(), rutaSalida.c_str());
        return 0;
    } catch (const assets::ErrorDatos& e) {
        fprintf(stderr, "dumpslp: %s\n", e.mensaje.c_str());
        return 1;
    }
}
