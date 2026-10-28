#pragma once
// Render isometrico de terreno (spec §6 M2). Proyeccion "diamante":
//   pantalla_x = (x - y) * medioAncho + camara.x
//   pantalla_y = (x + y) * medioAlto + camara.y
// con el tamano de mosaico VERIFICADO contra empires.dat (48x24, mitad
// 24x12) — nunca inventado de memoria (spec §6 M2: "verificar el tamano y
// la proyeccion contra los archivos reales").
//
// Regla del modo de depuracion obligatorio: cada cuadro deja en StatsIso
// cuantos mosaicos se intentaron, se dibujaron, quedaron fuera de vista y
// fallaron, con la causa de cada fallo. Nunca una pantalla negra sin
// explicacion (spec §6 M2).

#include "../game/mapa.h"
#include "framebuffer.h"

#include <string>

namespace render {

// Desplazamiento del origen del mapa en pixeles de pantalla.
struct Camara {
    float x = 0.0f;
    float y = 0.0f;
};

struct TamTile {
    int ancho = 48;
    int alto = 24;
    int medioAncho = 24;
    int medioAlto = 12;
    std::string origen;  // "empires.dat", "empires_test.dat (sintetico)" o "documentado"
    bool valido() const {
        return ancho > 0 && alto > 0 && medioAncho > 0 && medioAlto > 0 &&
               medioAncho * 2 == ancho && medioAlto * 2 == alto;
    }
};

struct StatsIso {
    int intentados = 0;    // proyectados dentro (aprox) del marco
    int dibujados = 0;
    int fueraDeVista = 0;  // proyectados fuera del marco
    int fallidos = 0;      // no se pudieron dibujar
    std::string ultimaCausa;

    void reset() { *this = StatsIso{}; }
};

// Proyecta el centro del mosaico (tx, ty) a pantalla.
void proyectar(int tx, int ty, const TamTile& t, const Camara& c, int* sx, int* sy);

// Rango de camara para que el mapa nunca desaparezca por completo.
struct LimitesCamara {
    float minX = 0, maxX = 0, minY = 0, maxY = 0;
};
LimitesCamara limitesCamara(int anchoMapa, int altoMapa, const TamTile& t,
                            int anchoPantalla, int altoPantalla);

void ajustarCamara(const LimitesCamara& l, Camara& c);

// Dibuja todo el terreno del cuadro. No lanza: todo problema se contabiliza
// en stats con causa (fallo explicito, spec §2).
// `dibujarMosaicos=false` es SOLO para la prueba de la guarda anti-pantalla-
// negra (el binario debe salir con error si no se dibujo nada).
void dibujarTerreno(Framebuffer& fb, const game::Mapa& mapa, const TamTile& t,
                    const Camara& cam, StatsIso& stats, bool dibujarMosaicos = true);

}  // namespace render
