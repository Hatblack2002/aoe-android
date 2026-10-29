#include "assets/slp_prueba.h"

#include "assets/formato.h"
#include "assets/slp.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace assets {

namespace {

// ------------------------------------------------------------------
// Escritor binario little-endian minimo (solo para esta prueba).
// ------------------------------------------------------------------
struct Esc {
    std::vector<uint8_t> b;

    void u8(unsigned v) { b.push_back(static_cast<uint8_t>(v)); }
    void u16(unsigned v) {
        b.push_back(static_cast<uint8_t>(v & 0xFF));
        b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    }
    void u32(unsigned long v) {
        for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
    }
    void i32(long v) { u32(static_cast<unsigned long>(v) & 0xFFFFFFFFul); }
    void fijas(const char* s, std::size_t total) {
        const std::size_t n = strlen(s);
        for (std::size_t i = 0; i < total; ++i) u8(i < n ? static_cast<unsigned char>(s[i]) : 0);
    }
    void poner32(std::size_t donde, unsigned long v) {
        for (int i = 0; i < 4; ++i) {
            b[donde + static_cast<std::size_t>(i)] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
        }
    }
};

// Comodines para las matrices esperadas.
constexpr PixelSLP T{0, PixelTipo::Transparente};
inline PixelSLP N(unsigned i) { return {static_cast<uint8_t>(i), PixelTipo::Normal}; }
inline PixelSLP J(unsigned i) { return {static_cast<uint8_t>(i), PixelTipo::Jugador}; }
constexpr PixelSLP S{0, PixelTipo::Sombra};
constexpr PixelSLP OJ{0, PixelTipo::ContornoJugador};
constexpr PixelSLP O2{243, PixelTipo::Contorno2};

using Fila = std::vector<PixelSLP>;

bool filaIgual(const MarcoSLP& m, int numFila, const Fila& esperada, std::string* detalle) {
    const int ancho = m.ancho;
    if (static_cast<int>(esperada.size()) != ancho) {
        *detalle = "prueba mal construida: fila " + std::to_string(numFila) + " esperada=" +
                   std::to_string(esperada.size()) + " ancho=" + std::to_string(ancho);
        return false;
    }
    for (int x = 0; x < ancho; ++x) {
        const PixelSLP& o = m.pixeles[static_cast<std::size_t>(numFila) * ancho + x];
        const PixelSLP& e = esperada[static_cast<std::size_t>(x)];
        if (o.indice != e.indice || o.tipo != e.tipo) {
            char buf[160];
            snprintf(buf, sizeof(buf),
                     "divergencia fila %d col %d: esperado (idx=%u tipo=%d) obtenido (idx=%u tipo=%d)",
                     numFila, x, e.indice, static_cast<int>(e.tipo), o.indice,
                     static_cast<int>(o.tipo));
            *detalle = buf;
            return false;
        }
    }
    return true;
}

bool lanzaCon(const std::vector<uint8_t>& slp, const char* subcadenaEsperada,
              std::string* detalle) {
    try {
        DecodificadorSLP dec(slp.data(), slp.size(), "prueba-negativa.slp");
        dec.decodificarMarco(0);
    } catch (const ErrorDatos& e) {
        if (strstr(e.mensaje.c_str(), subcadenaEsperada) == nullptr) {
            *detalle = std::string("caso negativo: fallo pero con otro mensaje: ") + e.mensaje;
            return false;
        }
        return true;
    }
    *detalle = std::string("caso negativo: DEBIA fallar con \"") + subcadenaEsperada +
               "\" y decodifico sin error";
    return false;
}

// Construye el SLP de prueba: 280x7, 1 marco, con todas las clases de
// comandos del slp-files.md de openage (2.0N).
std::vector<uint8_t> construirSlpCompleto(std::vector<Fila>* esperadas) {
    const int ANCHO = 280, ALTO = 7;
    const std::size_t OFF_CONTORNOS = 64, OFF_CMDTAB = 92;

    Esc f;
    // Cabecera 2.0N: version[4] + num_frames i32 + comment[24] = 32 B.
    f.fijas("2.0N", 4);
    f.i32(1);
    f.fijas("autoprueba decodificador", 24);
    // Frame info: cmd_table, outline_table, palette(sin uso), properties,
    // width, height, hotspot_x, hotspot_y.
    f.u32(OFF_CMDTAB);
    f.u32(OFF_CONTORNOS);
    f.u32(0);
    f.u32(0);
    f.i32(ANCHO);
    f.i32(ALTO);
    f.i32(12);
    f.i32(3);

    // Tabla de contornos (izq, der por fila). Fila 4: 0x8000 = transparente.
    const uint16_t izq[7] = {0, 0, 0, 0, 0x8000, 0, 0};
    const uint16_t der[7] = {0, 0, 0, 0, 0x8000, 0, 0};
    for (int i = 0; i < ALTO; ++i) {
        f.u16(izq[i]);
        f.u16(der[i]);
    }
    // Hueco para la tabla de offsets de comandos (se rellena al final).
    for (int i = 0; i < ALTO; ++i) f.u32(0);

    std::vector<std::vector<uint8_t>> filasCmd(7);
    esperadas->assign(ALTO, {});

    const auto emitirFila = [&](int indiceFila) {
        // Parchea la entrada de la tabla de offsets y agrega los comandos.
        const unsigned long absoluta = static_cast<unsigned long>(f.b.size());
        f.poner32(OFF_CMDTAB + static_cast<std::size_t>(indiceFila) * 4, absoluta);
        if (indiceFila == 4) {
            // Fila transparente (contornos 0x8000): entrada apuntando a una
            // posicion valida del stream pero SIN comandos; no debe leerse.
            return;
        }
        const auto& cmds = filasCmd[static_cast<std::size_t>(indiceFila)];
        f.b.insert(f.b.end(), cmds.begin(), cmds.end());
    };

    // ---- Fila 0: lesser draw (0x04/0x08/0x0C/0x10), lesser skip con y sin
    // byte extra (0x01/0x05/0x09/0x0D), fill 0x17/0x27/0x07+next/0x47, y
    // relleno largo 0x07+0xFF para completar los 280 px.
    filasCmd[0] = {0x04, 0x63,
                   0x08, 0x7B, 0x1D,
                   0x0C, 0x02, 0x33, 0x5A,
                   0x01, 0x02,            // salto corto con contador en el byte siguiente
                   0x05,
                   0x09,
                   0x0D,
                   0x17, 0xA5,
                   0x27, 0x3C,
                   0x07, 0x03, 0x91,      // relleno con contador en el byte siguiente
                   0x47, 0xD1,            // 0x47: >>4 = 4 pixeles
                   0x07, 0xFF, 0x11,      // relleno con contador 255
                   0x17, 0x22,
                   0x0F};
    {
        Fila& e = (*esperadas)[0];
        e.push_back(N(0x63));
        e.push_back(N(0x7B)); e.push_back(N(0x1D));
        e.push_back(N(0x02)); e.push_back(N(0x33)); e.push_back(N(0x5A));
        e.insert(e.end(), 2, T);
        e.insert(e.end(), 1, T);
        e.insert(e.end(), 2, T);
        e.insert(e.end(), 3, T);
        e.push_back(N(0xA5));
        e.push_back(N(0x3C)); e.push_back(N(0x3C));
        e.insert(e.end(), 3, N(0x91));
        e.insert(e.end(), 4, N(0xD1));
        e.insert(e.end(), 255, N(0x11));
        e.push_back(N(0x22));
    }

    // ---- Fila 1: color de jugador (0x16/0x26/0x06+next), relleno de jugador
    // (0x1A/0x2A/0x0A+next), sombra (0x0B+next/0x1B/0x2B/0x4B), salto corto y
    // greater draw 0x12+0x00 (256 px) para completar la fila.
    filasCmd[1] = {0x16, 0x03,
                   0x26, 0x01, 0x02,
                   0x06, 0x04, 0x00, 0x01, 0x02, 0x03,
                   0x1A, 0x05,
                   0x2A, 0x07,            // relleno jugador: cuenta=0x2A>>4=2, indice 0x07
                   0x0A, 0x03, 0x01,
                   0x0B, 0x02,
                   0x1B,
                   0x2B,
                   0x4B,
                   0x09,
                   0x12, 0x00};           // 256 pixeles: los indices van a continuacion
    for (int k = 0; k < 256; ++k)
        filasCmd[1].push_back(static_cast<uint8_t>((k * 11) % 256));
    filasCmd[1].push_back(0x0F);
    {
        Fila& e = (*esperadas)[1];
        e.push_back(J(3));
        e.push_back(J(1)); e.push_back(J(2));
        e.push_back(J(0)); e.push_back(J(1)); e.push_back(J(2)); e.push_back(J(3));
        e.push_back(J(5));
        e.push_back(J(7)); e.push_back(J(7));
        e.insert(e.end(), 3, J(1));
        e.insert(e.end(), 2, S);
        e.push_back(S);
        e.push_back(S);
        e.push_back(S);
        e.insert(e.end(), 4, S);
        e.insert(e.end(), 2, T);
        for (int k = 0; k < 256; ++k) e.push_back(N((k * 11) % 256));
    }

    // ---- Fila 2: greater draw 0x12+0x14 = 256+20 = 276 pixeles, mas corto.
    filasCmd[2] = {0x12, 0x14};
    for (int k = 0; k < 276; ++k)
        filasCmd[2].push_back(static_cast<uint8_t>((k * 7) % 256));
    filasCmd[2].push_back(0x08);
    filasCmd[2].push_back(0xC1);
    filasCmd[2].push_back(0xC2);
    filasCmd[2].push_back(0x05);
    filasCmd[2].push_back(0x04);
    filasCmd[2].push_back(0xC3);
    filasCmd[2].push_back(0x0F);
    {
        Fila& e = (*esperadas)[2];
        for (int k = 0; k < 276; ++k) e.push_back(N((k * 7) % 256));
        e.push_back(N(0xC1)); e.push_back(N(0xC2));
        e.push_back(T);
        e.push_back(N(0xC3));
    }

    // ---- Fila 3: greater skip 0x13+0x14 = 276 transparentes, mas corto.
    filasCmd[3] = {0x13, 0x14,
                   0x0C, 0xD1, 0xD2, 0xD3,
                   0x05,
                   0x0F};
    {
        Fila& e = (*esperadas)[3];
        e.insert(e.end(), 276, T);
        e.push_back(N(0xD1)); e.push_back(N(0xD2)); e.push_back(N(0xD3));
        e.push_back(T);
    }

    // ---- Fila 4: fila completamente transparente (contornos 0x8000): NO hay
    // comandos; la entrada de la tabla de offsets queda intacta (stream base).
    filasCmd[4] = {};
    (*esperadas)[4].assign(static_cast<std::size_t>(ANCHO), T);

    // ---- Fila 5: comandos extendidos 0x4E/0x5E/0x6E/0x7E + no-ops
    // (0x0E/0x1E/0x2E/0x3E: transformaciones sin datos).
    filasCmd[5] = {0x4E,
                   0x5E, 0x03,
                   0x6E,
                   0x7E, 0x02,
                   0x0E, 0x1E, 0x2E, 0x3E,   // sin datos, no cambian pixeles
                   0x12, 0x00};              // 256 pixeles: indices a continuacion
    for (int k = 0; k < 256; ++k)
        filasCmd[5].push_back(static_cast<uint8_t>((k * 7) % 256));
    filasCmd[5].push_back(0x57);
    filasCmd[5].push_back(0x44);   // relleno 5
    filasCmd[5].push_back(0x67);
    filasCmd[5].push_back(0x55);   // relleno 6
    filasCmd[5].push_back(0x17);
    filasCmd[5].push_back(0x66);
    filasCmd[5].push_back(0x27);
    filasCmd[5].push_back(0x77);
    filasCmd[5].push_back(0x09);
    filasCmd[5].push_back(0x04);
    filasCmd[5].push_back(0x88);
    filasCmd[5].push_back(0x0F);
    {
        Fila& e = (*esperadas)[5];
        e.push_back(OJ);
        e.insert(e.end(), 3, OJ);
        e.push_back(O2);
        e.insert(e.end(), 2, O2);
        for (int k = 0; k < 256; ++k) e.push_back(N((k * 7) % 256));
        e.insert(e.end(), 5, N(0x44));
        e.insert(e.end(), 6, N(0x55));
        e.push_back(N(0x66));
        e.push_back(N(0x77)); e.push_back(N(0x77));
        e.insert(e.end(), 2, T);
        e.push_back(N(0x88));
    }

    // ---- Fila 6: relleno de fila exacto con greater draw + mezcla.
    filasCmd[6] = {0x12, 0x13};   // 256+19 = 275; indices a continuacion
    for (int k = 0; k < 275; ++k)
        filasCmd[6].push_back(static_cast<uint8_t>((k * 3) % 256));
    filasCmd[6].push_back(0x05);
    filasCmd[6].push_back(0x27);
    filasCmd[6].push_back(0x77);
    filasCmd[6].push_back(0x17);
    filasCmd[6].push_back(0x88);
    filasCmd[6].push_back(0x04);
    filasCmd[6].push_back(0x99);
    filasCmd[6].push_back(0x0F);
    {
        Fila& e = (*esperadas)[6];
        for (int k = 0; k < 275; ++k) e.push_back(N((k * 3) % 256));
        e.push_back(T);
        e.push_back(N(0x77)); e.push_back(N(0x77));
        e.push_back(N(0x88));
        e.push_back(N(0x99));
    }

    // Emitir el stream y parchear la tabla de offsets de comandos.
    for (int i = 0; i < ALTO; ++i) emitirFila(i);
    return f.b;
}

} // namespace

bool probarDecodificadorSLP(std::string* detalle) {
    std::vector<Fila> esperadas;
    const std::vector<uint8_t> slp = construirSlpCompleto(&esperadas);

    MarcoSLP marco;
    try {
        DecodificadorSLP dec(slp.data(), slp.size(), "autoprueba.slp");
        if (dec.info().version != "2.0N" || dec.numMarcos() != 1) {
            *detalle = "cabecera mal interpretada: version \"" + dec.info().version +
                       "\", marcos=" + std::to_string(dec.numMarcos());
            return false;
        }
        marco = dec.decodificarMarco(0);
    } catch (const ErrorDatos& e) {
        *detalle = "el SLP de prueba valido NO decodifico: " + e.mensaje;
        return false;
    }

    if (marco.ancho != 280 || marco.alto != 7) {
        *detalle = "dimensiones mal leidas: " + std::to_string(marco.ancho) + "x" +
                   std::to_string(marco.alto) + " (esperado 280x7)";
        return false;
    }
    if (marco.anclaX != 12 || marco.anclaY != 3) {
        *detalle = "ancla mal leida: (" + std::to_string(marco.anclaX) + "," +
                   std::to_string(marco.anclaY) + ") (esperado 12,3)";
        return false;
    }
    for (int fila = 0; fila < 7; ++fila) {
        if (!filaIgual(marco, fila, esperadas[static_cast<std::size_t>(fila)], detalle)) {
            return false;
        }
    }

    // Casos negativos: cada uno DEBE fallar con la causa indicada.
    {
        // Version no soportada (cabecera completa de 32 B con version 2.0X).
        Esc f;
        f.fijas("2.0X", 4);
        f.i32(1);
        f.fijas("version mala", 24);
        if (!lanzaCon(f.b, "no soportada", detalle)) return false;
    }

    {
        // SLP 2.0N valido pero fila corta: ancho 8, solo 1 pixel dibujado.
        Esc f;
        f.fijas("2.0N", 4);
        f.i32(1);
        f.fijas("fila corta", 24);
        f.u32(92); f.u32(64); f.u32(0); f.u32(0);
        f.i32(8); f.i32(1); f.i32(0); f.i32(0);
        f.u16(0); f.u16(0);       // contorno fila 0 (offset 64)
        for (int i = 0; i < 24; ++i) f.u8(0);  // relleno hasta la tabla (offset 92)
        f.u32(92 + 4);            // offset de comandos (96)
        f.b.push_back(0x04);      // dibuja 1 pixel
        f.b.push_back(0x42);
        f.b.push_back(0x0F);      // fin de fila con 1 de 8
        if (!lanzaCon(f.b, "la fila termina", detalle)) return false;
    }
    {
        // Comando extendido no soportado (0x8E, dither, sin ejemplos conocidos).
        Esc f;
        f.fijas("2.0N", 4);
        f.i32(1);
        f.fijas("dither", 24);
        f.u32(92); f.u32(64); f.u32(0); f.u32(0);
        f.i32(8); f.i32(1); f.i32(0); f.i32(0);
        f.u16(0); f.u16(0);
        for (int i = 0; i < 24; ++i) f.u8(0);  // relleno hasta la tabla (offset 92)
        f.u32(92 + 4);
        f.b.push_back(0x8E);
        f.b.push_back(0x0F);
        if (!lanzaCon(f.b, "comando extendido no soportado", detalle)) return false;
    }
    {
        // Stream truncado a mitad de fila.
        Esc f;
        f.fijas("2.0N", 4);
        f.i32(1);
        f.fijas("truncado", 24);
        f.u32(92); f.u32(64); f.u32(0); f.u32(0);
        f.i32(8); f.i32(1); f.i32(0); f.i32(0);
        f.u16(0); f.u16(0);
        for (int i = 0; i < 24; ++i) f.u8(0);  // relleno hasta la tabla (offset 92)
        f.u32(92 + 4);
        f.b.push_back(0x08);      // dibuja 2 pixeles...
        f.b.push_back(0x42);      // ...pero solo llega 1 (fin de archivo)
        if (!lanzaCon(f.b, "truncado", detalle)) return false;  // "indice de paleta truncado" o "comandos truncados"
    }

    *detalle = "OK: 7 filas pixel a pixel (280x7) con lesser/greater draw+skip, "
               "contador-en-byte-siguiente, fills 0x17/0x27/0x47/0x57/0x67, jugador 0x06/0x16/"
               "0x26/0x0A/0x1A/0x2A, sombras 0x0B/0x1B/0x2B/0x4B, extendidos 0x4E/0x5E/0x6E/0x7E, "
               "no-ops 0x0E/0x1E/0x2E/0x3E, fila 0x8000, ancla y propiedades; 4 casos negativos "
               "fallan con la causa correcta";
    return true;
}

} // namespace assets
