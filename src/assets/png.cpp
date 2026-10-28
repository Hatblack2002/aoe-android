#include "assets/png.h"
#include "assets/formato.h"
#include "assets/inflate.h"

#include <cstdio>
#include <vector>

namespace assets {

namespace {

void ponerU32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x >> 24));
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}

// Chunk PNG: longitud + tipo + datos + CRC32.
void ponerChunk(std::vector<uint8_t>& v, const char* tipo, const uint8_t* datos,
                std::size_t tam) {
    ponerU32(v, static_cast<uint32_t>(tam));
    const std::size_t inicio = v.size();
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(tipo[i]));
    v.insert(v.end(), datos, datos + tam);
    const uint32_t crc = crc32(v.data() + inicio, 4 + tam);
    ponerU32(v, crc);
}

} // namespace

std::vector<uint8_t> escribirPng(int ancho, int alto, const std::vector<RGB>& pixeles) {
    if (ancho <= 0 || alto <= 0) {
        throw ErrorDatos{"png: dimensiones invalidas"};
    }
    if (pixeles.size() != static_cast<std::size_t>(ancho) * static_cast<std::size_t>(alto)) {
        throw ErrorDatos{"png: el bufer no coincide con las dimensiones"};
    }

    // IHDR: ancho, alto, profundidad 8, RGB (2), compresion 0, filtro 0, entrelazado 0.
    uint8_t ihdr[13];
    ihdr[0] = static_cast<uint8_t>(ancho >> 24);
    ihdr[1] = static_cast<uint8_t>(ancho >> 16);
    ihdr[2] = static_cast<uint8_t>(ancho >> 8);
    ihdr[3] = static_cast<uint8_t>(ancho);
    ihdr[4] = static_cast<uint8_t>(alto >> 24);
    ihdr[5] = static_cast<uint8_t>(alto >> 16);
    ihdr[6] = static_cast<uint8_t>(alto >> 8);
    ihdr[7] = static_cast<uint8_t>(alto);
    ihdr[8] = 8;   // profundidad
    ihdr[9] = 2;   // color: RGB verdadero
    ihdr[10] = 0;  // deflate
    ihdr[11] = 0;  // filtro adaptativo (no usado)
    ihdr[12] = 0;  // sin entrelazado

    // Datos de imagen: cada linea precedida por un byte de filtro (0 = ninguna).
    const std::size_t linea = static_cast<std::size_t>(ancho) * 3;
    std::vector<uint8_t> crudo;
    crudo.reserve((linea + 1) * static_cast<std::size_t>(alto));
    for (int y = 0; y < alto; ++y) {
        crudo.push_back(0);
        for (int x = 0; x < ancho; ++x) {
            const RGB& p = pixeles[static_cast<std::size_t>(y) * ancho + x];
            crudo.push_back(p.r);
            crudo.push_back(p.g);
            crudo.push_back(p.b);
        }
    }

    // Flujo zlib: 0x78 0x01 + bloques almacenados + Adler-32.
    const std::vector<uint8_t> defl = desinflarAlmacenado(crudo.data(), crudo.size());
    std::vector<uint8_t> zlib;
    zlib.reserve(defl.size() + 6);
    zlib.push_back(0x78);
    zlib.push_back(0x01);
    zlib.insert(zlib.end(), defl.begin(), defl.end());
    const uint32_t adler = adler32(crudo.data(), crudo.size());
    zlib.push_back(static_cast<uint8_t>(adler >> 24));
    zlib.push_back(static_cast<uint8_t>(adler >> 16));
    zlib.push_back(static_cast<uint8_t>(adler >> 8));
    zlib.push_back(static_cast<uint8_t>(adler));

    std::vector<uint8_t> png;
    png.reserve(zlib.size() + 64);
    const uint8_t firma[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    png.insert(png.end(), firma, firma + 8);
    ponerChunk(png, "IHDR", ihdr, sizeof(ihdr));
    ponerChunk(png, "IDAT", zlib.data(), zlib.size());
    ponerChunk(png, "IEND", nullptr, 0);
    return png;
}

bool escribirPngArchivo(const std::string& ruta, int ancho, int alto,
                        const std::vector<RGB>& pixeles, std::string* error) {
    try {
        const std::vector<uint8_t> png = escribirPng(ancho, alto, pixeles);
        FILE* f = fopen(ruta.c_str(), "wb");
        if (!f) {
            if (error) *error = "no se pudo abrir " + ruta + " para escritura";
            return false;
        }
        const std::size_t escritos = fwrite(png.data(), 1, png.size(), f);
        const bool ok = escritos == png.size() && fclose(f) == 0;
        if (!ok && error) *error = "escritura incompleta de " + ruta;
        return ok;
    } catch (const ErrorDatos& e) {
        if (error) *error = e.mensaje;
        return false;
    }
}

} // namespace assets
