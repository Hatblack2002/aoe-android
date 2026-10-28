#include "assets/paleta.h"
#include "assets/formato.h"

#include <cstdlib>

namespace assets {

namespace {

// Divide el bufer en lineas (acepta \r\n y \n; el juego usa \r\n).
void lineas(const uint8_t* d, unsigned long n, void* ctx,
            bool (*consumir)(void*, const char*, unsigned long)) {
    unsigned long inicio = 0;
    for (unsigned long i = 0; i <= n; ++i) {
        const bool finArchivo = i == n;
        const bool esFin = finArchivo || d[i] == '\n';
        if (!esFin) continue;
        unsigned long largo = i - inicio;
        if (largo > 0 && d[inicio + largo - 1] == '\r') --largo;  // quitar \r final
        if (!consumir(ctx, reinterpret_cast<const char*>(d + inicio), largo)) {
            throw ErrorDatos{"paleta: linea invalida"};
        }
        inicio = i + 1;
        if (finArchivo) break;
    }
}

struct Estado {
    int numeroLinea = 0;
    int entradasLeidas = 0;
    int entradasEsperadas = -1;
    Paleta paleta;
};

bool linea(void* ctx, const char* s, unsigned long largo) {
    Estado* e = static_cast<Estado*>(ctx);
    ++e->numeroLinea;

    // Ignorar lineas vacias y comentarios (mismas reglas que openage).
    unsigned long i = 0;
    while (i < largo && (s[i] == ' ' || s[i] == '\t')) ++i;
    if (i == largo || s[i] == '#') return true;

    std::string campo(s, largo);
    if (e->numeroLinea == 1) {
        if (campo != "JASC-PAL" && campo != "JASC-PALX") {
            throw ErrorDatos{"paleta: cabecera incorrecta, se esperaba JASC-PAL y hay \"" +
                             campo + "\" (¿no es una paleta del juego?)"};
        }
        return true;
    }
    if (e->numeroLinea == 2) {
        if (campo != "0100") {
            throw ErrorDatos{"paleta: version incorrecta, se esperaba 0100 y hay \"" + campo + "\""};
        }
        return true;
    }
    if (e->numeroLinea == 3) {
        e->entradasEsperadas = atoi(campo.c_str());
        if (e->entradasEsperadas != 256) {
            throw ErrorDatos{"paleta: se esperaban 256 entradas y la cabecera declara " +
                             campo};
        }
        return true;
    }

    // Entrada: "R G B" (los DE anaden un cuarto valor, se ignora).
    if (e->entradasLeidas >= 256) {
        throw ErrorDatos{"paleta: mas entradas de las declaradas (256)"};
    }
    int rgb[3] = {0, 0, 0};
    int valores = 0;
    unsigned long pos = i;
    while (pos < largo && valores < 3) {
        while (pos < largo && (s[pos] == ' ' || s[pos] == '\t')) ++pos;
        if (pos >= largo) break;
        char* fin = nullptr;
        const long v = strtol(s + pos, &fin, 10);
        if (fin == s + pos) {
            throw ErrorDatos{"paleta: entrada no numerica en linea " +
                             std::to_string(e->numeroLinea)};
        }
        if (v < 0 || v > 255) {
            throw ErrorDatos{"paleta: valor fuera de 0..255 (" + std::to_string(v) +
                             ") en linea " + std::to_string(e->numeroLinea)};
        }
        rgb[valores++] = static_cast<int>(v);
        pos = static_cast<unsigned long>(fin - s);
    }
    if (valores != 3) {
        throw ErrorDatos{"paleta: se esperaban 3 valores por linea y hay " +
                         std::to_string(valores) + " (linea " +
                         std::to_string(e->numeroLinea) + ")"};
    }
    e->paleta[static_cast<std::size_t>(e->entradasLeidas++)] = {
        static_cast<uint8_t>(rgb[0]), static_cast<uint8_t>(rgb[1]),
        static_cast<uint8_t>(rgb[2])};
    return true;
}

} // namespace

Paleta leerPaleta(const uint8_t* datos, unsigned long tam) {
    if (tam < 15) {
        throw ErrorDatos{"paleta: archivo demasiado pequeno (" + std::to_string(tam) +
                         " bytes)"};
    }
    Estado e;
    lineas(datos, tam, &e, linea);
    if (e.numeroLinea < 3) {
        throw ErrorDatos{"paleta: archivo sin cabecera completa"};
    }
    if (e.entradasLeidas != 256) {
        throw ErrorDatos{"paleta: leidas " + std::to_string(e.entradasLeidas) +
                         " entradas, se esperaban 256"};
    }
    return e.paleta;
}

} // namespace assets
