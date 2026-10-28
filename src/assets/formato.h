#pragma once
// Lectura de datos binarios little-endian con validacion de limites.
// Regla de la especificacion §2 principio 3 (fallo cerrado): cualquier dato
// inesperado lanza ErrorDatos con un mensaje claro que incluye el contexto y
// el desplazamiento exacto. Nunca se lee fuera de rango ni se "inventa".
//
// Capa assets: no depende de SDL ni de ninguna otra parte del motor (spec §4).

#include <cstddef>
#include <cstdint>
#include <string>

namespace assets {

// Error de datos: formato inesperado, archivo truncado o valor invalido.
struct ErrorDatos {
    std::string mensaje;
};

// Lector secuencial sobre un bufer en memoria. Todos los metodos validan
// que haya bytes suficientes; si no, lanzan ErrorDatos con contexto.
class LectorLE {
public:
    LectorLE(const uint8_t* datos, std::size_t tam, std::size_t pos = 0,
             std::string nombreArchivo = std::string());

    std::size_t pos() const { return pos_; }
    std::size_t restante() const { return tam_ - pos_; }
    const std::string& archivo() const { return archivo_; }

    void saltar(std::size_t bytes, const char* contexto);
    // Verifica que queden `bytes` sin leerlos.
    void esperar(std::size_t bytes, const char* contexto);

    uint8_t u8(const char* contexto);
    uint16_t u16(const char* contexto);
    uint32_t u32(const char* contexto);
    int16_t i16(const char* contexto);
    int32_t i32(const char* contexto);
    float f32(const char* contexto);

    // Lee `bytes` crudos (sin interpretar).
    std::string crudo(std::size_t bytes, const char* contexto);
    // Lee hasta `max` bytes y corta en el primer '\0' (sin incluirlo).
    std::string cadena(std::size_t max, const char* contexto);

    // Reposiciona y valida el nuevo desplazamiento.
    void buscar(std::size_t pos, const char* contexto);

private:
    [[noreturn]] void fallar(const char* contexto, const std::string& detalle) const;

    const uint8_t* datos_;
    std::size_t tam_;
    std::size_t pos_;
    std::string archivo_;
};

// Utilidades numericas sin estado (para campos leidos con desplazamientos
// calculados, por ejemplo unit_offsets en el dat).
uint16_t leerU16(const uint8_t* p);
uint32_t leerU32(const uint8_t* p);
int16_t leerI16(const uint8_t* p);
int32_t leerI32(const uint8_t* p);

} // namespace assets
