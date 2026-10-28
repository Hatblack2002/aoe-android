#include "assets/drs.h"
#include "assets/formato.h"

#include <algorithm>
#include <cctype>

namespace assets {

namespace {

// Invierte los 4 bytes de la extension y recorta espacios: 'anib' -> "bina",
// ' pls' -> "slp". Ver openage drs.py: fileext[::-1].rstrip().lower().
std::string extensionLogica(const std::string& cruda) {
    std::string inv(cruda.rbegin(), cruda.rend());
    while (!inv.empty() && isspace(static_cast<unsigned char>(inv.back()))) inv.pop_back();
    for (char& c : inv) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return inv;
}

bool imprimible(const std::string& s) {
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u != 0 && (u < 0x20 || u > 0x7E)) return false;
    }
    return true;
}

} // namespace

LectorDRS::LectorDRS(const uint8_t* datos, unsigned long tam, std::string nombreArchivo)
    : datos_(datos), tam_(tam), archivo_(std::move(nombreArchivo)) {
    if (tam_ < 64) {
        throw ErrorDatos{archivo_ + ": DRS demasiado pequeno (" + std::to_string(tam_) +
                         " bytes; la cabecera ocupa 64)"};
    }
    LectorLE r(datos_, tam_, 0, archivo_);
    info_.copyright = r.cadena(40, "DRS copyright");
    info_.version = r.crudo(4, "DRS version");
    info_.tipo = r.cadena(12, "DRS ftype");
    const int32_t numTablas = r.i32("DRS table_count");
    r.i32("DRS file_offset (primer archivo)");

    if (!imprimible(info_.copyright) || !imprimible(info_.tipo)) {
        throw ErrorDatos{archivo_ +
                         ": la cabecera no parece un DRS (copyright/tipo con bytes "
                         "no imprimibles). ¿Es este archivo un DRS?"};
    }
    if (info_.version != "1.00") {
        throw ErrorDatos{archivo_ + ": version DRS \"" + info_.version +
                         "\" no soportada (se espera 1.00, la de AoE1/AoK)"};
    }
    if (numTablas < 0 || numTablas > 32) {
        throw ErrorDatos{archivo_ + ": numero de tablas imposible (" +
                         std::to_string(numTablas) + ")"};
    }

    for (int t = 0; t < numTablas; ++t) {
        InfoTablaDRS tabla;
        const std::string cruda = r.crudo(4, "DRS extension de tabla");
        tabla.offsetTabla = r.u32("DRS file_info_offset");
        tabla.numArchivos = r.u32("DRS file_count");
        tabla.extension = extensionLogica(cruda);
        if (tabla.extension.empty()) {
            throw ErrorDatos{archivo_ + ": tabla con extension vacia (bytes crudos: " + cruda + ")"};
        }
        // La tabla de entradas debe caber en el archivo.
        const std::size_t finTabla = static_cast<std::size_t>(tabla.offsetTabla) +
                                     static_cast<std::size_t>(tabla.numArchivos) * 12;
        if (finTabla > tam_) {
            throw ErrorDatos{archivo_ + ": tabla \"" + tabla.extension +
                             "\" desborda el archivo (offset 0x" +
                             [&] {
                                 char b[16];
                                 snprintf(b, sizeof(b), "%x", tabla.offsetTabla);
                                 return std::string(b);
                             }() + ", " + std::to_string(tabla.numArchivos) + " archivos)"};
        }
        info_.tablas.push_back(tabla);

        LectorLE te(datos_, tam_, tabla.offsetTabla, archivo_);
        for (uint32_t f = 0; f < tabla.numArchivos; ++f) {
            EntradaDRS entrada;
            entrada.id = te.u32("DRS file_id");
            entrada.offset = te.u32("DRS file_data_offset");
            entrada.tam = te.u32("DRS file_size");
            entrada.extension = tabla.extension;
            if (static_cast<std::size_t>(entrada.offset) + entrada.tam > tam_) {
                throw ErrorDatos{archivo_ + ": recurso " + std::to_string(entrada.id) + "." +
                                 tabla.extension + " desborda el archivo (offset 0x" +
                                 [&] {
                                     char b[16];
                                     snprintf(b, sizeof(b), "%x", entrada.offset);
                                     return std::string(b);
                                 }() + ", tamano " + std::to_string(entrada.tam) + ")"};
            }
            info_.archivos.push_back(entrada);
        }
    }
}

const EntradaDRS* LectorDRS::buscar(uint32_t id, const std::string& extension) const {
    auto it = std::find_if(info_.archivos.begin(), info_.archivos.end(),
                           [&](const EntradaDRS& e) { return e.id == id && e.extension == extension; });
    return it == info_.archivos.end() ? nullptr : &*it;
}

std::vector<uint8_t> LectorDRS::extraer(const EntradaDRS& entrada) const {
    const uint8_t* inicio = datos_ + entrada.offset;
    return std::vector<uint8_t>(inicio, inicio + entrada.tam);
}

} // namespace assets
