#include "assets/formato.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace assets {

LectorLE::LectorLE(const uint8_t* datos, std::size_t tam, std::size_t pos,
                   std::string nombreArchivo)
    : datos_(datos), tam_(tam), pos_(pos), archivo_(std::move(nombreArchivo)) {}

void LectorLE::fallar(const char* contexto, const std::string& detalle) const {
    char buf[256];
    snprintf(buf, sizeof(buf), "offset 0x%zx (%zu)", pos_, pos_);
    std::string donde = archivo_.empty() ? std::string(buf) : archivo_ + ": " + buf;
    throw ErrorDatos{std::string(contexto) + ": " + detalle + " [" + donde + "]"};
}

void LectorLE::esperar(std::size_t bytes, const char* contexto) {
    if (pos_ + bytes > tam_) {
        char buf[96];
        snprintf(buf, sizeof(buf), "archivo truncado: pide %zu bytes, quedan %zu", bytes,
                 tam_ - pos_);
        fallar(contexto, buf);
    }
}

void LectorLE::saltar(std::size_t bytes, const char* contexto) {
    esperar(bytes, contexto);
    pos_ += bytes;
}

uint8_t LectorLE::u8(const char* contexto) {
    esperar(1, contexto);
    return datos_[pos_++];
}

uint16_t LectorLE::u16(const char* contexto) {
    esperar(2, contexto);
    const uint16_t v = static_cast<uint16_t>(datos_[pos_]) |
                       static_cast<uint16_t>(datos_[pos_ + 1]) << 8;
    pos_ += 2;
    return v;
}

uint32_t LectorLE::u32(const char* contexto) {
    esperar(4, contexto);
    const uint32_t v = static_cast<uint32_t>(datos_[pos_]) |
                       static_cast<uint32_t>(datos_[pos_ + 1]) << 8 |
                       static_cast<uint32_t>(datos_[pos_ + 2]) << 16 |
                       static_cast<uint32_t>(datos_[pos_ + 3]) << 24;
    pos_ += 4;
    return v;
}

int16_t LectorLE::i16(const char* contexto) {
    return static_cast<int16_t>(u16(contexto));
}

int32_t LectorLE::i32(const char* contexto) {
    return static_cast<int32_t>(u32(contexto));
}

float LectorLE::f32(const char* contexto) {
    const uint32_t bits = u32(contexto);
    float f = 0.0f;
    static_assert(sizeof(f) == sizeof(bits), "float de 32 bits requerido");
    memcpy(&f, &bits, sizeof(f));
    if (std::isnan(f)) {
        fallar(contexto, "float NaN (datos corruptos)");
    }
    return f;
}

std::string LectorLE::crudo(std::size_t bytes, const char* contexto) {
    esperar(bytes, contexto);
    std::string s(reinterpret_cast<const char*>(datos_ + pos_), bytes);
    pos_ += bytes;
    return s;
}

std::string LectorLE::cadena(std::size_t max, const char* contexto) {
    esperar(max, contexto);
    const char* inicio = reinterpret_cast<const char*>(datos_ + pos_);
    std::size_t len = 0;
    while (len < max && inicio[len] != '\0') ++len;
    pos_ += max;
    return std::string(inicio, len);
}

void LectorLE::buscar(std::size_t pos, const char* contexto) {
    if (pos > tam_) {
        char buf[96];
        snprintf(buf, sizeof(buf), "desplazamiento fuera de rango: 0x%zx > 0x%zx", pos, tam_);
        fallar(contexto, buf);
    }
    pos_ = pos;
}

uint16_t leerU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(p[1]) << 8;
}

uint32_t leerU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 |
           static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}

int16_t leerI16(const uint8_t* p) {
    return static_cast<int16_t>(leerU16(p));
}

int32_t leerI32(const uint8_t* p) {
    return static_cast<int32_t>(leerU32(p));
}

} // namespace assets
