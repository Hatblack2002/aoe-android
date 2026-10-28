#include "assets/chequeo.h"
#include "assets/dat.h"
#include "assets/drs.h"
#include "assets/formato.h"
#include "assets/inflate.h"
#include "assets/paleta.h"
#include "assets/slp.h"

#include <algorithm>
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
        throw ErrorDatos{"no se pudo abrir " + ruta};
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
            snprintf(linea, sizeof(linea), "  bytes sin parsear (cola): %zu",
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
        for (const std::string& ruta : listarArchivos(dir)) {
            const std::size_t barra = ruta.rfind('/');
            const std::string nombre = barra == std::string::npos ? ruta : ruta.substr(barra + 1);
            // Auxiliares que no son objetivo de validacion.
            if (nombre.find("MANIFIESTO") != std::string::npos ||
                nombre.find("payload") != std::string::npos) {
                res.informe += "(omitido) " + ruta + "\n";
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
                    ++res.fallidos;
                    res.informe += "ERROR DEL VALIDADOR: " + ruta +
                                   " es corrupto pero PASO la validacion\n";
                } else {
                    ++res.ok;
                    res.informe += "OK: " + ruta + "\n" + informe;
                }
            } catch (const ErrorDatos& e) {
                if (corruptoEsperado) {
                    ++res.ok;
                    res.informe +=
                        "CORRUPTO OK (fallo esperado): " + ruta + ": " + e.mensaje + "\n";
                } else {
                    ++res.fallidos;
                    res.informe += "ERROR: " + ruta + ": " + e.mensaje + "\n";
                }
            }
        }
    }
    char linea[96];
    snprintf(linea, sizeof(linea), "ASSETCHECK: %d OK, %d FALLIDOS\n", res.ok, res.fallidos);
    res.informe += linea;
    return res;
}

} // namespace assets
