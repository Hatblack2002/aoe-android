// Render isometrico de terreno: implementacion (M2).
// Los mosaicos son rombos de tamAncho x tamAlto que teselan el plano
// (sin solape, terreno plano sin elevacion), asi que el orden de dibujo no
// altera el resultado; aun asi se recorre por filas para que M3 (objetos)
// pueda reutilizar el mismo bucle de profundidad.

#include "render/isometrico.h"

namespace render {

namespace {

// Color base por tipo de terreno (paleta propia de la prueba; cuando M3
// traiga terrenos del dat real, esta tabla se mapeara a los SLP de
// terrain.drs con la paleta del juego).
struct ColorTerreno {
    uint8_t r, g, b;
};
ColorTerreno colorDe(uint8_t terreno) {
    switch (terreno) {
        case static_cast<uint8_t>(game::Terreno::Agua):    return {36, 96, 168};
        case static_cast<uint8_t>(game::Terreno::Orilla):  return {196, 178, 118};
        case static_cast<uint8_t>(game::Terreno::Tierra):  return {128, 96, 60};
        default:                                           return {58, 128, 44};  // pasto
    }
}

// Variacion determinista por mosaico (ajedrez suave, como el original):
// brillo +-12 segun (x*31 + y*17) mod 5. Sin azar.
int variacion(int tx, int ty) {
    const int v = (tx * 31 + ty * 17) % 5;
    return (v - 2) * 6;
}

uint8_t satura(int v) { return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v)); }

// Rellena el rombo centrado en (sx, sy): una linea horizontal por fila.
// ancho(dy) crece hasta medioAlto filas y decrece despues; con
// alto=24/medioAlto=12 la fila central mide el ancho completo (48).
void dibujarRombo(Framebuffer& fb, int sx, int sy, const TamTile& t, uint8_t terreno,
                  int tx, int ty) {
    const ColorTerreno base = colorDe(terreno);
    const int dv = variacion(tx, ty);
    const int r = satura(base.r + dv), g = satura(base.g + dv), b = satura(base.b + dv);
    const int mitadAlto = t.alto / 2;
    const int minY = sy - mitadAlto;
    for (int dy = 0; dy < t.alto; ++dy) {
        const int escalon = dy < mitadAlto ? (dy + 1) : (t.alto - dy);
        int w = t.ancho * escalon / mitadAlto;
        if (w < 2) w = 2;  // minimo visible en la punta
        fb.fillRect(sx - w / 2, minY + dy, w, 1, r, g, b);
    }
}

}  // namespace

void proyectar(int tx, int ty, const TamTile& t, const Camara& c, int* sx, int* sy) {
    *sx = (tx - ty) * t.medioAncho + static_cast<int>(c.x);
    *sy = (tx + ty) * t.medioAlto + static_cast<int>(c.y);
}

LimitesCamara limitesCamara(int anchoMapa, int altoMapa, const TamTile& t,
                            int anchoPantalla, int altoPantalla) {
    // Vertices extremos del rombo total del mapa (centros + medio mosaico).
    const float izq = -static_cast<float>(altoMapa - 1) * t.medioAncho - t.medioAncho;
    const float der = static_cast<float>(anchoMapa - 1) * t.medioAncho + t.medioAncho;
    const float arriba = -static_cast<float>(t.medioAlto);
    const float abajo = static_cast<float>(anchoMapa + altoMapa - 2) * t.medioAlto + t.medioAlto;
    LimitesCamara l;
    l.minX = anchoPantalla - der;  // borde derecho del mapa >= 0 de pantalla
    l.maxX = anchoPantalla - izq;  // borde izquierdo <= ancho de pantalla
    l.minY = altoPantalla - abajo;
    l.maxY = altoPantalla - arriba;
    if (l.minX > l.maxX) l.minX = l.maxX = 0;
    if (l.minY > l.maxY) l.minY = l.maxY = 0;
    return l;
}

void ajustarCamara(const LimitesCamara& l, Camara& c) {
    if (c.x < l.minX) c.x = l.minX;
    if (c.x > l.maxX) c.x = l.maxX;
    if (c.y < l.minY) c.y = l.minY;
    if (c.y > l.maxY) c.y = l.maxY;
}

void dibujarTerreno(Framebuffer& fb, const game::Mapa& mapa, const TamTile& t,
                    const Camara& cam, StatsIso& stats, bool dibujarMosaicos) {
    stats.reset();

    if (!t.valido()) {
        // Fallo explicito: sin tamano de mosaico no se dibuja nada, pero el
        // overlay (y el registro) diran por que (spec §2/§6 M2).
        stats.fallidos = mapa.ancho * mapa.alto;
        stats.ultimaCausa = "tamano de mosaico invalido (" + std::to_string(t.ancho) + "x" +
                            std::to_string(t.alto) + ", origen: " + t.origen + ")";
        return;
    }
    if (!dibujarMosaicos) {
        stats.ultimaCausa = "dibujo suprimido por el instrumento de prueba";
        return;
    }

    const int margenX = t.medioAncho + 2, margenY = t.medioAlto + 2;
    for (int ty = 0; ty < mapa.alto; ++ty) {
        for (int tx = 0; tx < mapa.ancho; ++tx) {
            int sx, sy;
            proyectar(tx, ty, t, cam, &sx, &sy);
            if (sx < -margenX || sx > fb.width() + margenX || sy < -margenY ||
                sy > fb.height() + margenY) {
                ++stats.fueraDeVista;
                continue;
            }
            ++stats.intentados;
            const uint8_t terreno = mapa.at(tx, ty);
            if (terreno == 255) {
                ++stats.fallidos;
                stats.ultimaCausa = "tile (" + std::to_string(tx) + "," + std::to_string(ty) +
                                    ") fuera del mapa en memoria";
                continue;
            }
            dibujarRombo(fb, sx, sy, t, terreno, tx, ty);
            ++stats.dibujados;
        }
    }
}

}  // namespace render
