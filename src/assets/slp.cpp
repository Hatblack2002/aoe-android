#include "assets/slp.h"
#include "assets/formato.h"

#include <cstring>

namespace assets {

namespace {

constexpr int kTransparenteFila = 0x8000;  // fila vacia (slp-files.md, Outline Table)

} // namespace

DecodificadorSLP::DecodificadorSLP(const uint8_t* datos, unsigned long tam,
                                   std::string nombreArchivo)
    : datos_(datos), tam_(tam), archivo_(std::move(nombreArchivo)) {
    if (tam_ < 32) {
        throw ErrorDatos{archivo_ + ": SLP demasiado pequeno (" + std::to_string(tam_) +
                         " bytes; la cabecera ocupa 32)"};
    }
    LectorLE r(datos_, tam_, 0, archivo_);
    info_.version = r.crudo(4, "SLP version");
    if (info_.version != "2.0N") {
        throw ErrorDatos{archivo_ + ": version SLP \"" + info_.version +
                         "\" no soportada (este motor lee 2.0N, la del AoE1/AoK originales)"};
    }
    info_.numMarcos = r.i32("SLP numero de marcos");
    if (info_.numMarcos < 1 || info_.numMarcos > 4096) {
        throw ErrorDatos{archivo_ + ": numero de marcos imposible (" +
                         std::to_string(info_.numMarcos) + ")"};
    }
    info_.comentario = r.cadena(24, "SLP comentario");

    // Los desplazamientos de cada marco deben caber en el archivo.
    r.esperar(static_cast<std::size_t>(info_.numMarcos) * 32, "tabla de marcos SLP");
}

// Decodifica una fila de comandos. Devuelve los pixeles "utiles" (sin los
// bordes transparentes izquierdo/derecho). Semantica 1:1 con slp.pyx.
static void decodificarFila(const uint8_t* datos, unsigned long tam, unsigned long inicioCmd,
                            int objetivo, std::vector<PixelSLP>& fila,
                            const std::string& archivo, int numMarco, int numFila) {
    fila.clear();
    unsigned long pos = inicioCmd;
    auto fallo = [&](const std::string& detalle) -> ErrorDatos {
        char buf[192];
        snprintf(buf, sizeof(buf), " (marco %d, fila %d, offset 0x%lx)", numMarco, numFila,
                 inicioCmd);
        return ErrorDatos{archivo + ": " + detalle + buf};
    };

    for (;;) {
        if (pos >= tam) throw fallo("comandos truncados a mitad de fila");
        const uint8_t cmd = datos[pos];
        const uint8_t nibbleBajo = cmd & 0x0F;
        const uint8_t mordisco = cmd & 0x03;

        // count = cmd >> n; si 0, se toma el byte siguiente (slp.pyx cmd_or_next).
        auto contarONext = [&](int desplazamiento) -> int {
            int c = cmd >> desplazamiento;
            if (c == 0) {
                if (pos + 1 >= tam) throw fallo("contador truncado");
                c = datos[pos + 1];
                ++pos;
            }
            return c;
        };

        auto leerIndice = [&]() -> int {
            if (pos + 1 >= tam) throw fallo("indice de paleta truncado");
            return datos[++pos];
        };

        if (nibbleBajo == 0x0F) {
            // Fin de fila: la fila debe estar exactamente llena (slp-files.md).
            if (static_cast<int>(fila.size()) != objetivo) {
                throw fallo("la fila termina con " + std::to_string(fila.size()) +
                            " pixeles y su ancho es " + std::to_string(objetivo));
            }
            return;
        } else if (mordisco == 0x00) {
            // Dibujo corto: n indices de paleta.
            const int n = cmd >> 2;
            for (int k = 0; k < n; ++k) {
                fila.push_back({static_cast<uint8_t>(leerIndice()), PixelTipo::Normal});
            }
        } else if (mordisco == 0x01) {
            // Salto corto (transparente).
            const int n = contarONext(2);
            fila.insert(fila.end(), static_cast<std::size_t>(n), {0, PixelTipo::Transparente});
        } else if (nibbleBajo == 0x02) {
            // Dibujo largo.
            if (pos + 1 >= tam) throw fallo("contador largo truncado");
            const int n = ((cmd & 0xF0) << 4) + datos[pos + 1];
            ++pos;
            for (int k = 0; k < n; ++k) {
                fila.push_back({static_cast<uint8_t>(leerIndice()), PixelTipo::Normal});
            }
        } else if (nibbleBajo == 0x03) {
            // Salto largo.
            if (pos + 1 >= tam) throw fallo("contador largo truncado");
            const int n = ((cmd & 0xF0) << 4) + datos[pos + 1];
            ++pos;
            fila.insert(fila.end(), static_cast<std::size_t>(n), {0, PixelTipo::Transparente});
        } else if (nibbleBajo == 0x06) {
            // Dibujo con color de jugador: un indice (0..9 en AoE1) por pixel.
            const int n = contarONext(4);
            for (int k = 0; k < n; ++k) {
                fila.push_back({static_cast<uint8_t>(leerIndice()), PixelTipo::Jugador});
            }
        } else if (nibbleBajo == 0x07) {
            // Relleno con un indice de paleta.
            const int n = contarONext(4);
            const int indice = leerIndice();
            fila.insert(fila.end(), static_cast<std::size_t>(n),
                        {static_cast<uint8_t>(indice), PixelTipo::Normal});
        } else if (nibbleBajo == 0x0A) {
            // Relleno con color de jugador (no se usa en AoE1 pero es valido).
            const int n = contarONext(4);
            const int indice = leerIndice();
            fila.insert(fila.end(), static_cast<std::size_t>(n),
                        {static_cast<uint8_t>(indice), PixelTipo::Jugador});
        } else if (nibbleBajo == 0x0B) {
            // Sombra: sin datos.
            const int n = contarONext(4);
            fila.insert(fila.end(), static_cast<std::size_t>(n), {0, PixelTipo::Sombra});
        } else if (nibbleBajo == 0x0E) {
            // Comandos extendidos (mordisco alto).
            const uint8_t alto = cmd & 0xF0;
            if (alto == 0x40) {
                fila.push_back({0, PixelTipo::ContornoJugador});
            } else if (alto == 0x50) {
                if (pos + 1 >= tam) throw fallo("contador de contorno truncado");
                const int n = datos[++pos];
                fila.insert(fila.end(), static_cast<std::size_t>(n),
                            {0, PixelTipo::ContornoJugador});
            } else if (alto == 0x60) {
                fila.push_back({243, PixelTipo::Contorno2});
            } else if (alto == 0x70) {
                if (pos + 1 >= tam) throw fallo("contador de contorno truncado");
                const int n = datos[++pos];
                fila.insert(fila.end(), static_cast<std::size_t>(n), {243, PixelTipo::Contorno2});
            } else if (alto == 0x00 || alto == 0x10 || alto == 0x20 || alto == 0x30) {
                // Pistas de render sin datos (xfliptest, tablas de color): no-op.
            } else {
                throw fallo("comando extendido no soportado 0x" +
                            [&] {
                                char b[8];
                                snprintf(b, sizeof(b), "%02X", cmd);
                                return std::string(b);
                            }());
            }
        } else {
            // nibbleBajo 0x04/0x08/0x0C caen en dibujo corto (mordisco 0),
            // 0x05/0x09/0x0D en salto corto (mordisco 1): ya cubiertos arriba.
            throw fallo("comando no reconocido 0x" +
                        [&] {
                            char b[8];
                            snprintf(b, sizeof(b), "%02X", cmd);
                            return std::string(b);
                        }());
        }
        ++pos;
    }
}

MarcoSLP DecodificadorSLP::decodificarMarco(int32_t i) const {
    if (i < 0 || i >= info_.numMarcos) {
        throw ErrorDatos{archivo_ + ": marco " + std::to_string(i) + " fuera de rango (hay " +
                         std::to_string(info_.numMarcos) + ")"};
    }
    LectorLE r(datos_, tam_, 32 + static_cast<std::size_t>(i) * 32, archivo_);
    const uint32_t offCmdTabla = r.u32("marco: cmd_table_offset");
    const uint32_t offContorno = r.u32("marco: outline_table_offset");
    r.u32("marco: palette_offset (sin uso)");
    const uint32_t propiedades = r.u32("marco: properties");
    const int32_t ancho = r.i32("marco: width");
    const int32_t alto = r.i32("marco: height");
    MarcoSLP marco;
    marco.ancho = ancho;
    marco.alto = alto;
    marco.anclaX = r.i32("marco: hotspot_x");
    marco.anclaY = r.i32("marco: hotspot_y");
    marco.propiedades = propiedades;

    if (ancho <= 0 || alto <= 0 || ancho > 4096 || alto > 4096) {
        throw ErrorDatos{archivo_ + ": marco con dimensiones imposibles " +
                         std::to_string(ancho) + "x" + std::to_string(alto)};
    }
    marco.pixeles.assign(static_cast<std::size_t>(ancho) * static_cast<std::size_t>(alto),
                         {0, PixelTipo::Transparente});

    // Tablas por fila: contorno (izq/der) y desplazamiento de comandos.
    std::vector<uint32_t> cmdFila(static_cast<std::size_t>(alto));
    std::vector<int> izquierda(static_cast<std::size_t>(alto));
    std::vector<int> derecha(static_cast<std::size_t>(alto));
    for (int filaNum = 0; filaNum < alto; ++filaNum) {
        LectorLE c(datos_, tam_, offContorno + static_cast<std::size_t>(filaNum) * 4, archivo_);
        int izq = c.u16("contorno: left_space");
        int der = c.u16("contorno: right_space");
        LectorLE m(datos_, tam_, offCmdTabla + static_cast<std::size_t>(filaNum) * 4, archivo_);
        cmdFila[static_cast<std::size_t>(filaNum)] = m.u32("tabla de comandos por fila");
        if ((izq & kTransparenteFila) || (der & kTransparenteFila)) {
            // Fila completamente transparente: no hay comandos (slp-files.md).
            // Se marca con todo el ancho a la izquierda y 0 util.
            izquierda[static_cast<std::size_t>(filaNum)] = ancho;
            derecha[static_cast<std::size_t>(filaNum)] = 0;
            continue;
        }
        if (izq + der > ancho) {
            throw ErrorDatos{archivo_ + ": marco " + std::to_string(i) + ", fila " +
                             std::to_string(filaNum) + ": contorno " + std::to_string(izq) + "+" +
                             std::to_string(der) + " excede el ancho " + std::to_string(ancho)};
        }
        izquierda[static_cast<std::size_t>(filaNum)] = izq;
        derecha[static_cast<std::size_t>(filaNum)] = der;
    }

    std::vector<PixelSLP> fila;
    fila.reserve(static_cast<std::size_t>(ancho));
    for (int filaNum = 0; filaNum < alto; ++filaNum) {
        const int izq = izquierda[static_cast<std::size_t>(filaNum)];
        const int der = derecha[static_cast<std::size_t>(filaNum)];
        const int util = ancho - izq - der;
        if (util == 0) continue;  // fila transparente
        decodificarFila(datos_, tam_, cmdFila[static_cast<std::size_t>(filaNum)], util, fila,
                        archivo_, i, filaNum);
        PixelSLP* destino = marco.pixeles.data() +
                            static_cast<std::size_t>(filaNum) * static_cast<std::size_t>(ancho) + izq;
        memcpy(destino, fila.data(), sizeof(PixelSLP) * static_cast<std::size_t>(util));
    }
    return marco;
}

} // namespace assets
