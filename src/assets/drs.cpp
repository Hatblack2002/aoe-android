#include "assets/drs.h"
#include "assets/formato.h"

#include <algorithm>
#include <cctype>

namespace assets {

namespace {

// Invierte los 4 bytes de la extension y recorta espacios/nulos: 'anib' ->
// "bina", ' pls' -> "slp". Ver openage drs.py: fileext[::-1].rstrip().lower().
// Se recortan tambien nulos por si un editor relleno con 0x00 ('slp' + 0x00
// invertido deja el nulo al final tras dar la vuelta).
std::string extensionLogica(const std::string& cruda) {
    std::string inv(cruda.rbegin(), cruda.rend());
    while (!inv.empty() &&
           (inv.back() == ' ' || inv.back() == '\0')) {
        inv.pop_back();
    }
    for (char& c : inv) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return inv;
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

    // Validacion ESTRUCTURAL, no de caracteres (spec M1). Los archivos reales
    // de AoE1 llevan relleno NO imprimible en la cabecera: el hexdump de
    // artho.com/age/drs.html muestra "Copyright (c) 1997 Ensemble Studios."
    // seguido del byte de control 0x1A y nulos, y ftype es "tribe" relleno
    // con nulos hasta 12. Exigir imprimibilidad ahi rechazaba DRS validos
    // (caso graphics.drs real: version 1.00, tipo tribe, tabla slp con 683
    // archivos). Lo estructural es: version magica "1.00", conteo de tablas
    // plausible y tablas/entradas dentro del archivo; copyright y tipo se
    // registran como informacion sin condicionar la aceptacion.
    if (info_.version != "1.00") {
        char hexV[16];
        for (int i = 0; i < 4 && 40 + i < tam_; ++i) {
            snprintf(hexV + i * 3, 4, "%02X ", datos_[40 + i]);
        }
        hexV[4 * 3 - 1] = '\0';
        throw ErrorDatos{archivo_ + ": version DRS \"" + info_.version +
                         "\" no soportada (se espera 1.00, la de AoE1/AoK) bytes[40..43]=" +
                         std::string(hexV)};
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
