#pragma once
// Capa game (spec §4): el mapa vive aqui, SIN SDL y sin assets. Es la
// fuente de verdad de lo que hay que dibujar; el render solo la lee.
//
// M2: mapa de prueba generado por codigo (spec §6 M2), determinista
// (formulas puras, cero azar) para que el CI pueda comparar salida
// byte a byte entre ejecuciones.

#include <cstdint>
#include <string>
#include <vector>

namespace game {

enum class Terreno : uint8_t {
    Pasto = 0,
    Agua = 1,
    Orilla = 2,  // arena que rodea el agua
    Tierra = 3,
    Num = 4
};

struct Mapa {
    int ancho = 0;
    int alto = 0;
    std::vector<uint8_t> terreno;  // ancho*alto, valores de Terreno

    bool dentro(int x, int y) const {
        return x >= 0 && y >= 0 && x < ancho && y < alto;
    }
    uint8_t at(int x, int y) const {
        return dentro(x, y) ? terreno[static_cast<std::size_t>(y) * ancho + x]
                            : 255;  // fuera de rango: el llamador decide
    }
    void set(int x, int y, uint8_t t) {
        if (dentro(x, y)) terreno[static_cast<std::size_t>(y) * ancho + x] = t;
    }
};

struct ConteoTerreno {
    int porTipo[static_cast<int>(Terreno::Num)] = {};
    int total() const {
        int s = 0;
        for (int v : porTipo) s += v;
        return s;
    }
};

// Mapa de prueba 64x64 (o el tamano pedido): pasto base, un lago con
// orilla, un rio sinuoso y parches de tierra. Todo con formulas fijas;
// dos llamadas con los mismos argumentos producen el mismo mapa.
Mapa generarMapaPrueba(int ancho, int alto);

ConteoTerreno contarTerrenos(const Mapa& m);

// "pasto=3500 agua=120 ..." para el registro y el overlay.
std::string resumenTerrenos(const ConteoTerreno& c);

}  // namespace game
