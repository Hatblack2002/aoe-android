#include "assets/chequeo.h"
#include "assets/dat.h"
#include "assets/drs.h"
#include "assets/formato.h"
#include "assets/inflate.h"
#include "assets/paleta.h"
#include "assets/slp.h"

#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace assets {

const char* nombreTipo(TipoArchivo t) {
    switch (t) {
        case TipoArchivo::Drs: return "DRS";
        case TipoArchivo::Slp: return "SLP";
        case TipoArchivo::Paleta: return "PALETA";
        case TipoArchivo::Dat: return "DAT";
        default: return "?";
    }
}

TipoArchivo detectarTipo(const std::string& ruta, const std::vector<uint8_t>& bytes) {
    const auto empieza = [&](const char* s, std::size_t off = 0) {
        const std::size_t n = strlen(s);
        return bytes.size() >= off + n && memcmp(bytes.data() + off, s, n) == 0;
    };
    // Pista por extension (etiqueta mejor a los corruptos).
    const std::size_t punto = ruta.rfind('.');
    std::string ext = punto == std::string::npos ? std::string() : ruta.substr(punto + 1);
    for (char& c : ext) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    if (ext == "slp") return TipoArchivo::Slp;
    if (ext == "pal") return TipoArchivo::Paleta;
    if (ext == "drs") return TipoArchivo::Drs;
    if (ext == "dat") return TipoArchivo::Dat;
    // Sin extension util: olfateo por contenido.
    if (empieza("2.0N")) return TipoArchivo::Slp;
    if (empieza("JASC-PAL") || empieza("JASC-PALX")) return TipoArchivo::Paleta;
    if (bytes.size() >= 64) {
        bool plausible = true;
        static const char kVersion[4] = {'1', '.', '0', '0'};
        static const char kFtype[5] = {'t', 'r', 'i', 'b', 'e'};
        for (int i = 0; i < 4; ++i) {
            if (bytes[40 + i] != kVersion[i]) plausible = false;
        }
        for (int i = 0; i < 5; ++i) {
            if (bytes[44 + i] != kFtype[i]) plausible = false;
        }
        if (plausible) return TipoArchivo::Drs;
    }
    if (bytes.size() >= 2 && (bytes[0] & 0x06) != 0x06) return TipoArchivo::Dat;
    return TipoArchivo::Desconocido;
}

std::vector<uint8_t> cargarArchivo(const std::string& ruta) {
    FILE* f = fopen(ruta.c_str(), "rb");
    if (!f) {
        throw ErrorDatos{"no se pudo abrir " + ruta +
                         " (¿existe y tiene permisos de lectura?)"};
    }
    std::vector<uint8_t> bytes;
    uint8_t bufer[65536];
    std::size_t n;
    while ((n = fread(bufer, 1, sizeof(bufer), f)) > 0) {
        bytes.insert(bytes.end(), bufer, bufer + n);
    }
    const bool errorLectura = ferror(f) != 0;
    fclose(f);
    if (errorLectura) {
        throw ErrorDatos{"error de lectura en " + ruta};
    }
    return bytes;
}

// ---------------------------------------------------------------
// Inspeccion defensiva para los ERRORES: dice lo que el archivo
// realmente contiene (spec §2, principio 2: si hay duda, herramienta
// que inspeccione y reporte). Nunca lanza: todo es tolerado aqui.
// ---------------------------------------------------------------
namespace {

std::string asciiLegible(const uint8_t* d, std::size_t n) {
    std::string t;
    for (std::size_t i = 0; i < n; ++i) {
        const char c = static_cast<char>(d[i]);
        t += (c >= 0x20 && c <= 0x7E) ? c : '.';
    }
    return t;
}

// Quita un prefijo de ruta de un mensaje (para que la causa quepa en pantalla).
std::string acortarMensaje(const std::string& msg, const std::string& prefijo) {
    if (prefijo.empty() || prefijo.size() + 1 > msg.size()) return msg;
    std::string s = msg;
    const std::string conBarra = prefijo + (prefijo.back() == '/' ? "" : "/");
    std::size_t pos = 0;
    while ((pos = s.find(conBarra, pos)) != std::string::npos) {
        s.replace(pos, conBarra.size(), "");
        pos += 1;
    }
    return s;
}

std::string hexPrimerosBytes(const std::vector<uint8_t>& bytes, std::size_t n = 16) {
    static const char* h = "0123456789ABCDEF";
    const std::size_t n2 = std::min(n, bytes.size());
    std::string s = "primeros bytes: ";
    if (n2 == 0) s += "(archivo vacio)";
    char buf[4];
    for (std::size_t i = 0; i < n2; ++i) {
        buf[0] = h[bytes[i] >> 4];
        buf[1] = h[bytes[i] & 0x0F];
        buf[2] = ' ';
        buf[3] = '\0';
        s += buf;
    }
    s += " '";
    s += asciiLegible(bytes.data(), n2);
    s += "'";
    return s;
}

long u32Seguro(const std::vector<uint8_t>& b, std::size_t off) {
    return off + 4 <= b.size() ? static_cast<long>(leerU32(b.data() + off)) : -1L;
}

// Extension logica DRS (invertida + recortada), tolerante: solo ASCII
// imprimible (los archivos basura no deben meter caracteres raros).
std::string extensionInvertida(const uint8_t* d) {
    std::string inv;
    for (int i = 3; i >= 0; --i) {
        const char c = static_cast<char>(d[i]);
        if (c >= 0x21 && c <= 0x7E) inv += static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }
    return inv;
}

std::string inspeccionarCabecera(TipoArchivo tipo, const std::vector<uint8_t>& b) {
    std::string s;
    char linea[192];
    switch (tipo) {
        case TipoArchivo::Drs: {
            if (b.size() < 64) break;
            snprintf(linea, sizeof(linea), "cabecera: version '%s' tipo '%s' tablas=%ld primer_archivo=0x%lx",
                     asciiLegible(b.data() + 40, 4).c_str(),
                     asciiLegible(b.data() + 44, 12).c_str(), u32Seguro(b, 56), u32Seguro(b, 60));
            s += linea;
            const long tablas = u32Seguro(b, 56);
            for (long i = 0; tablas >= 0 && i < tablas && i < 8; ++i) {
                const std::size_t off = 64 + static_cast<std::size_t>(i) * 12;
                if (off + 12 > b.size()) break;
                snprintf(linea, sizeof(linea), "  tabla %ld: '%s' offset=0x%lx archivos=%ld",
                         i, extensionInvertida(b.data() + off).c_str(), u32Seguro(b, off + 4),
                         u32Seguro(b, off + 8));
                s += "\n";
                s += linea;
            }
            break;
        }
        case TipoArchivo::Slp: {
            if (b.size() < 32) break;
            snprintf(linea, sizeof(linea), "cabecera: version '%s' marcos=%ld",
                     asciiLegible(b.data(), 4).c_str(), u32Seguro(b, 4));
            s += linea;
            break;
        }
        case TipoArchivo::Dat: {
            // El dat esperado es DEFLATE crudo: el primer byte codifica el
            // tipo de bloque (bits 1-2). Lo mostramos para diagnosticar.
            if (b.size() < 1) break;
            const int btype = (b[0] >> 1) & 0x03;
            const char* nombres[4] = {"almacenado", "fijo", "dinamico", "INVALIDO(3)"};
            snprintf(linea, sizeof(linea),
                     "si fuera DEFLATE crudo: primer byte 0x%02X -> bloque %s",
                     b[0], nombres[btype]);
            s += linea;
            break;
        }
        case TipoArchivo::Paleta: {
            // Primera linea de texto (hasta \n o 31 bytes).
            std::size_t fin = 0;
            while (fin < b.size() && fin < 31 && b[fin] != '\n' && b[fin] != '\r') ++fin;
            snprintf(linea, sizeof(linea), "primera linea: '%s'", asciiLegible(b.data(), fin).c_str());
            s += linea;
            break;
        }
        default:
            break;
    }
    return s;
}

} // namespace

std::string chequearDatos(TipoArchivo tipo, const std::vector<uint8_t>& bytes,
                          const std::string& nombreVisible) {
    char linea[512];
    std::string informe;
    switch (tipo) {
        case TipoArchivo::Slp: {
            DecodificadorSLP slp(bytes.data(), bytes.size(), nombreVisible);
            int32_t utiles = 0;
            for (int32_t f = 0; f < slp.numMarcos(); ++f) {
                const MarcoSLP marco = slp.decodificarMarco(f);
                for (const PixelSLP& p : marco.pixeles) {
                    if (p.tipo != PixelTipo::Transparente) ++utiles;
                }
                snprintf(linea, sizeof(linea), "  marco %d: %dx%d, ancla (%d,%d)", f,
                         marco.ancho, marco.alto, marco.anclaX, marco.anclaY);
                informe += linea + std::string("\n");
            }
            snprintf(linea, sizeof(linea),
                     "SLP OK: version %s, marcos=%d, pixeles_no_transparentes=%d",
                     slp.info().version.c_str(), slp.numMarcos(), utiles);
            return std::string(linea) + "\n" + informe;
        }
        case TipoArchivo::Drs: {
            LectorDRS drs(bytes.data(), bytes.size(), nombreVisible);
            snprintf(linea, sizeof(linea),
                     "DRS OK: version %s, tipo %s, tablas=%zu, recursos=%zu",
                     drs.info().version.c_str(), drs.info().tipo.c_str(),
                     drs.info().tablas.size(), drs.info().archivos.size());
            informe = std::string(linea) + "\n";
            for (const InfoTablaDRS& t : drs.info().tablas) {
                snprintf(linea, sizeof(linea), "  tabla .%s: %u recursos\n",
                         t.extension.c_str(), t.numArchivos);
                informe += linea;
            }
            return informe;
        }
        case TipoArchivo::Paleta: {
            const Paleta pal = leerPaleta(bytes.data(), bytes.size());
            snprintf(linea, sizeof(linea),
                     "PALETA OK: 256 entradas; [0]=(%d,%d,%d) [16]=(%d,%d,%d) [243]=(%d,%d,%d)",
                     pal[0].r, pal[0].g, pal[0].b, pal[16].r, pal[16].g, pal[16].b, pal[243].r,
                     pal[243].g, pal[243].b);
            return std::string(linea) + "\n";
        }
        case TipoArchivo::Dat: {
            const std::vector<uint8_t> datos =
                inflar(bytes.data(), bytes.size(), 64u * 1024 * 1024);
            const ResumenDat dat = leerDat(datos.data(), datos.size(), nombreVisible);
            snprintf(linea, sizeof(linea),
                     "DAT OK: versionstr \"%s\"; comprimido=%zu B; descomprimido=%zu B",
                     dat.versionStr.c_str(), bytes.size(), datos.size());
            informe = std::string(linea) + "\n";
            snprintf(linea, sizeof(linea),
                     "  mapa=%dx%d mundo=%dx%d tile=%dx%d (mitad %dx%d, elev %d)\n",
                     dat.mapaAncho, dat.mapaAlto, dat.mundoAncho, dat.mundoAlto, dat.tileAncho,
                     dat.tileAlto, dat.tileMediaAncho, dat.tileMediaAlto, dat.alturaElevacion);
            informe += linea;
            snprintf(linea, sizeof(linea),
                     "  terrenos=%u restricciones=%u colores_jugador=%u sonidos=%u graficos=%u\n",
                     dat.numTerrenos, dat.restriccionesTerreno, dat.numColoresJugador,
                     dat.numSonidos, dat.numGraficos);
            informe += linea;
            snprintf(linea, sizeof(linea), "  mapas=%u efectos=%u civs=%u investigaciones=%u\n",
                     dat.numMapas, dat.numPaquetesEfectos, dat.numCivs, dat.numInvestigaciones);
            informe += linea;
            for (const Civilizacion& c : dat.civs) {
                snprintf(linea, sizeof(linea), "  civ \"%s\": %u slots, %zu unidades\n",
                         c.nombre.c_str(), c.numUnidades, c.unidades.size());
                informe += linea;
                for (const Unidad& u : c.unidades) {
                    snprintf(linea, sizeof(linea),
                             "    id=%d tipo=%d \"%s\" hp=%d vel=%.2f quieto=%d\n", u.id, u.tipo,
                             u.nombre.c_str(), u.hitPoints, u.velocidad, u.graficoQuieto);
                    informe += linea;
                }
            }
            snprintf(linea, sizeof(linea), "  bytes sin parsear (cola): %zu\n",
                     dat.bytesSinParsear);
            informe += linea;
            return informe;
        }
        case TipoArchivo::Desconocido:
        default:
            throw ErrorDatos{
                "tipo de archivo no reconocido (no parece DRS, SLP, paleta ni dat)"};
    }
}

std::string chequearArchivo(const std::string& ruta) {
    const std::vector<uint8_t> bytes = cargarArchivo(ruta);
    const TipoArchivo tipo = detectarTipo(ruta, bytes);
    const std::string informe = chequearDatos(tipo, bytes, ruta);
    return std::string("[") + nombreTipo(tipo) + "]\n" + informe;
}

std::vector<std::string> listarArchivos(const std::string& dir) {
    std::vector<std::string> archivos;
    DIR* d = opendir(dir.c_str());
    if (!d) return archivos;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        const std::string nombre = e->d_name;
        if (nombre == "." || nombre == "..") continue;
        const std::string ruta = dir + "/" + nombre;
        struct stat st;
        if (stat(ruta.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            archivos.push_back(ruta);
        }
    }
    closedir(d);
    std::sort(archivos.begin(), archivos.end());
    return archivos;
}

ResumenChequeo chequearDirectorios(const std::vector<std::string>& dirs) {
    ResumenChequeo res;
    for (const std::string& dir : dirs) {
        const std::vector<std::string> archivos = listarArchivos(dir);
        // Etiqueta corta del directorio (ultima componente de la ruta).
        std::string etiqueta = dir;
        const std::size_t barra = etiqueta.find_last_of('/');
        if (barra != std::string::npos) etiqueta = etiqueta.substr(barra + 1);

        int ok = 0, mal = 0, omitidos = 0;
        std::string cuerpo;
        for (const std::string& ruta : archivos) {
            const std::size_t barraR = ruta.rfind('/');
            const std::string nombre =
                barraR == std::string::npos ? ruta : ruta.substr(barraR + 1);
            // Auxiliares que no son objetivo de validacion.
            if (nombre.find("MANIFIESTO") != std::string::npos ||
                nombre.find("payload") != std::string::npos) {
                ++omitidos;
                continue;
            }
            // Los archivos sinteticos corruptos (trunc*/bad*) DEBEN fallar:
            // si fallan, cuentan como OK (fallo esperado); si pasan, es un
            // fallo del validador.
            const bool corruptoEsperado = nombre.find("trunc") != std::string::npos ||
                                          nombre.find("bad") != std::string::npos;
            try {
                const std::string informe = chequearArchivo(ruta);
                if (corruptoEsperado) {
                    ++mal;
                    cuerpo += "ERROR DEL VALIDADOR: " + nombre +
                              " es corrupto pero PASO la validacion\n";
                } else {
                    ++ok;
                    cuerpo += "OK: " + nombre + "\n" + informe;
                }
            } catch (const ErrorDatos& e) {
                if (corruptoEsperado) {
                    ++ok;
                    cuerpo += "CORRUPTO OK (fallo esperado): " + nombre + "\n";
                    cuerpo += "  causa: " + acortarMensaje(e.mensaje, dir) + "\n";
                } else {
                    ++mal;
                    // Entrada con evidencia completa: tipo detectado, tamano
                    // exacto, causa completa, desglose defensivo de la
                    // cabecera y primeros bytes en hex.
                    std::vector<uint8_t> bytes;
                    TipoArchivo tipo = TipoArchivo::Desconocido;
                    try {
                        bytes = cargarArchivo(ruta);
                        tipo = detectarTipo(ruta, bytes);
                    } catch (const ErrorDatos&) {
                        bytes.clear();
                    }
                    char linea[160];
                    snprintf(linea, sizeof(linea), "ERROR: %s [%s] (%zu B)\n", nombre.c_str(),
                             nombreTipo(tipo), bytes.size());
                    cuerpo += linea;
                    // Causa completa, sin la ruta larga (el nombre ya esta en
                    // la linea anterior y el directorio en la cabecera).
                    cuerpo += "  causa: " + acortarMensaje(e.mensaje, dir) + "\n";
                    if (!bytes.empty()) {
                        const std::string insp = inspeccionarCabecera(tipo, bytes);
                        if (!insp.empty()) cuerpo += "  " + insp + "\n";
                        cuerpo += "  " + hexPrimerosBytes(bytes) + "\n";
                    }
                }
            }
        }
        char linea[160];
        snprintf(linea, sizeof(linea), "%s: %zu archivo(s), %d OK, %d FALLIDOS, %d omitidos\n",
                 etiqueta.c_str(), archivos.size(), ok, mal, omitidos);
        res.informe += linea + cuerpo;
        res.ok += ok;
        res.fallidos += mal;
    }
    char linea[96];
    snprintf(linea, sizeof(linea), "ASSETCHECK: %d OK, %d FALLIDOS\n", res.ok, res.fallidos);
    res.informe += linea;
    return res;
}

} // namespace assets
