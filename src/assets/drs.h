#pragma once
// Lector de contenedores DRS (Age of Empires / AoK).
// FORMATO VERIFICADO contra openage doc/media/drs-files.md y
// openage/convert/value_object/read/media/drs.py:
//   - cabecera: copyright[40] version[4] ftype[12] table_count i32
//     file_offset i32  (64 bytes; SWGB usa copyright[60] y no es nuestro caso)
//   - entrada de tabla: extension[4] INVERTIDA en archivo, file_info_offset
//     i32, file_count i32  (12 bytes)
//   - entrada de archivo: file_id i32, file_data_offset i32, file_size i32
//     (12 bytes)
//   - extensiones logicas (tras invertir y recortar espacios): "bina"
//     (binarios: paletas y otros) y "slp" (graficos). En archivo se ven como
//     'anib' y ' pls'.
//
// Fallo cerrado: cabecera/plantillas con datos imposibles -> ErrorDatos.

#include <cstdint>
#include <string>
#include <vector>

namespace assets {

struct EntradaDRS {
    uint32_t id = 0;
    std::string extension;  // extension logica ("slp", "bina", ...)
    uint32_t offset = 0;
    uint32_t tam = 0;
};

struct InfoTablaDRS {
    std::string extension;
    uint32_t offsetTabla = 0;
    uint32_t numArchivos = 0;
};

struct InfoDRS {
    std::string copyright;
    std::string version;
    std::string tipo;
    std::vector<InfoTablaDRS> tablas;
    std::vector<EntradaDRS> archivos;  // todas las entradas, por tabla
};

class LectorDRS {
public:
    LectorDRS(const uint8_t* datos, unsigned long tam, std::string nombreArchivo);

    const InfoDRS& info() const { return info_; }

    // Devuelve la entrada con (id, extension) o nullptr si no existe.
    const EntradaDRS* buscar(uint32_t id, const std::string& extension) const;

    // Copia los bytes del recurso a un vector (para decodificar).
    std::vector<uint8_t> extraer(const EntradaDRS& entrada) const;

private:
    const uint8_t* datos_;
    unsigned long tam_;
    std::string archivo_;
    InfoDRS info_;
};

} // namespace assets
