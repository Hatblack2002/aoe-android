// Implementacion del mapa de prueba M2 (spec §6: "mapa de prueba generado
// por codigo, por ejemplo 64x64, con varios tipos de terreno").
// Determinismo total: ninguna funcion de azar; lago, rio y parches salen de
// formulas geometricas fijas.

#include "game/mapa.h"

#include <cmath>

namespace game {

Mapa generarMapaPrueba(int ancho, int alto) {
    Mapa m;
    m.ancho = ancho > 0 ? ancho : 1;
    m.alto = alto > 0 ? alto : 1;
    m.terreno.assign(static_cast<std::size_t>(m.ancho) * m.alto,
                     static_cast<uint8_t>(Terreno::Pasto));

    const double fx = static_cast<double>(m.ancho);
    const double fy = static_cast<double>(m.alto);

    // Lago: circulo centrado en (0.68w, 0.36h), radio 0.13*min(w,h),
    // con anillo de orilla 2 tiles alrededor.
    const double lx = 0.68 * fx, ly = 0.36 * fy;
    const double rl = 0.13 * (fx < fy ? fx : fy);
    // Rio sinuoso: banda horizontal ondulada cerca de y = 0.74h.
    auto rioY = [&](double x) { return 0.74 * fy + std::sin(x * 0.35) * 4.0; };
    // Parches de tierra: dos discos fijos.
    const double tx1 = 0.20 * fx, ty1 = 0.62 * fy, tr1 = 0.10 * (fx < fy ? fx : fy);
    const double tx2 = 0.82 * fx, ty2 = 0.80 * fy, tr2 = 0.08 * (fx < fy ? fx : fy);

    for (int y = 0; y < m.alto; ++y) {
        for (int x = 0; x < m.ancho; ++x) {
            const double dx = static_cast<double>(x);
            const double dy = static_cast<double>(y);
            const double dLago = std::sqrt((dx - lx) * (dx - lx) + (dy - ly) * (dy - ly));
            const double dRio = std::fabs(dy - rioY(dx));
            const double dT1 = std::sqrt((dx - tx1) * (dx - tx1) + (dy - ty1) * (dy - ty1));
            const double dT2 = std::sqrt((dx - tx2) * (dx - tx2) + (dy - ty2) * (dy - ty2));

            uint8_t t = static_cast<uint8_t>(Terreno::Pasto);
            if (dLago <= rl || dRio <= 1.6) {
                t = static_cast<uint8_t>(Terreno::Agua);
            } else if (dLago <= rl + 2.2 || dRio <= 3.4) {
                t = static_cast<uint8_t>(Terreno::Orilla);
            }
            if (t == static_cast<uint8_t>(Terreno::Pasto) && (dT1 <= tr1 || dT2 <= tr2)) {
                t = static_cast<uint8_t>(Terreno::Tierra);
            }
            m.set(x, y, t);
        }
    }
    return m;
}

ConteoTerreno contarTerrenos(const Mapa& m) {
    ConteoTerreno c;
    for (const uint8_t t : m.terreno) {
        if (t < static_cast<uint8_t>(Terreno::Num)) ++c.porTipo[t];
    }
    return c;
}

std::string resumenTerrenos(const ConteoTerreno& c) {
    static const char* kNombres[static_cast<int>(Terreno::Num)] = {"pasto", "agua", "orilla",
                                                                   "tierra"};
    std::string s;
    for (int i = 0; i < static_cast<int>(Terreno::Num); ++i) {
        if (i) s += " ";
        s += std::string(kNombres[i]) + "=" + std::to_string(c.porTipo[i]);
    }
    return s;
}

}  // namespace game
