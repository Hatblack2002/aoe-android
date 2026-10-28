#include "assets/inflate.h"
#include "assets/formato.h"

#include <cstring>

namespace assets {

namespace {

// ---------------------------------------------------------------
// Decodificador Huffman canonico (RFC 1951 §3.2.2).
// Enfoque clasico por conteo por longitud: counts[l] = cuantos codigos
// tienen longitud l; symbols ordenados por (longitud, valor).
// ---------------------------------------------------------------
struct Huffman {
    uint16_t conteo[16];   // conteo[l] = codigos con longitud l
    uint16_t simbolos[288];
};

bool construirHuffman(Huffman* h, const uint8_t* longitudes, int n) {
    for (int i = 0; i < 16; ++i) h->conteo[i] = 0;
    for (int i = 0; i < n; ++i) h->conteo[longitudes[i]]++;
    if (h->conteo[0] == n) return true;  // sin codigos: valido pero inutilizable

    // Verificar sobre/subscripcion (sum(l * conteo[l]) <= 2^l por nivel).
    int sobra = 1;  // 1 << 0
    for (int l = 1; l <= 15; ++l) {
        sobra <<= 1;
        sobra -= h->conteo[l];
        if (sobra < 0) return false;
    }

    uint16_t offsets[16];
    offsets[1] = 0;
    for (int l = 1; l < 15; ++l) offsets[l + 1] = offsets[l] + h->conteo[l];

    for (int i = 0; i < n; ++i) {
        if (longitudes[i] != 0) {
            h->simbolos[offsets[longitudes[i]]++] = static_cast<uint16_t>(i);
        }
    }
    return true;
}

// Lector de bits LSB-first (RFC 1951: los bits llegan del menos al mas
// significativo de cada byte).
struct LectorBits {
    const uint8_t* datos;
    std::size_t tam;
    std::size_t pos = 0;   // siguiente byte
    uint32_t bufer = 0;
    int bitsEnBufer = 0;

    ErrorDatos error(const char* msg) const { return ErrorDatos{std::string("deflate: ") + msg}; }

    int bit() {
        if (bitsEnBufer == 0) {
            if (pos >= tam) throw error("flujo terminado a mitad de codigo");
            bufer = datos[pos++];
            bitsEnBufer = 8;
        }
        const int b = static_cast<int>(bufer & 1u);
        bufer >>= 1;
        --bitsEnBufer;
        return b;
    }

    uint32_t bits(int n) {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i) v |= static_cast<uint32_t>(bit()) << i;
        return v;
    }

    // Decodifica un simbolo caminando el arbol bit a bit.
    int simbolo(const Huffman& h) {
        int codigo = 0, primero = 0, indice = 0;
        for (int l = 1; l <= 15; ++l) {
            codigo |= bit();
            const int cuenta = h.conteo[l];
            if (codigo - primero < cuenta) return h.simbolos[indice + (codigo - primero)];
            indice += cuenta;
            primero = (primero + cuenta) << 1;
            codigo <<= 1;
        }
        throw error("codigo Huffman invalido");
    }
};

const uint16_t kLongBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLongExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                                8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// Longitudes de codigo para el encabezado dinamico (RFC 1951 §3.2.7).
const uint8_t kOrdenClaves[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

void construirFijos(Huffman* litlen, Huffman* dist) {
    uint8_t lon[288];
    int i = 0;
    for (; i < 144; ++i) lon[i] = 8;
    for (; i < 256; ++i) lon[i] = 9;
    for (; i < 280; ++i) lon[i] = 7;
    for (; i < 288; ++i) lon[i] = 8;
    construirHuffman(litlen, lon, 288);
    for (i = 0; i < 30; ++i) lon[i] = 5;
    construirHuffman(dist, lon, 30);
}

void bloque(LectorBits& lb, std::vector<uint8_t>& salida, const Huffman& litlen,
            const Huffman& dist, std::size_t maxSalida) {
    for (;;) {
        const int sim = lb.simbolo(litlen);
        if (sim < 256) {
            if (salida.size() >= maxSalida) {
                throw ErrorDatos{"deflate: salida excede el maximo permitido (datos corruptos?)"};
            }
            salida.push_back(static_cast<uint8_t>(sim));
        } else if (sim == 256) {
            return;
        } else {
            if (sim > 285) throw lb.error("codigo de longitud invalido");
            const int li = sim - 257;
            const uint32_t longitud = kLongBase[li] + lb.bits(kLongExtra[li]);
            const int di = lb.simbolo(dist);
            if (di > 29) throw lb.error("codigo de distancia invalido");
            const uint32_t d = kDistBase[di] + lb.bits(kDistExtra[di]);
            if (d > salida.size()) throw lb.error("distancia antes del inicio de la salida");
            if (salida.size() + longitud > maxSalida) {
                throw ErrorDatos{"deflate: salida excede el maximo permitido (datos corruptos?)"};
            }
            const std::size_t desde = salida.size() - d;
            for (uint32_t k = 0; k < longitud; ++k) salida.push_back(salida[desde + k]);
        }
    }
}

void bloqueDinamico(LectorBits& lb, std::vector<uint8_t>& salida, std::size_t maxSalida) {
    const uint32_t hlit = lb.bits(5) + 257;
    const uint32_t hdist = lb.bits(5) + 1;
    const uint32_t hclen = lb.bits(4) + 4;
    if (hlit > 286 || hdist > 30) throw lb.error("encabezado dinamico invalido");

    uint8_t lonClaves[19] = {0};
    for (uint32_t i = 0; i < hclen; ++i) lonClaves[kOrdenClaves[i]] = static_cast<uint8_t>(lb.bits(3));

    Huffman claves;
    if (!construirHuffman(&claves, lonClaves, 19)) throw lb.error("arbol de longitudes invalido");

    uint8_t longitudes[286 + 30] = {0};
    uint32_t n = 0;
    while (n < hlit + hdist) {
        int sim = lb.simbolo(claves);
        if (sim < 16) {
            longitudes[n++] = static_cast<uint8_t>(sim);
        } else if (sim == 16) {
            if (n == 0) throw lb.error("repeticion sin longitud previa");
            const uint8_t prev = longitudes[n - 1];
            const uint32_t rep = 3 + lb.bits(2);
            if (n + rep > hlit + hdist) throw lb.error("repeticion cruza el limite");
            for (uint32_t k = 0; k < rep; ++k) longitudes[n++] = prev;
        } else if (sim == 17) {
            const uint32_t rep = 3 + lb.bits(3);
            if (n + rep > hlit + hdist) throw lb.error("repeticion cruza el limite");
            n += rep;
        } else {  // 18
            const uint32_t rep = 11 + lb.bits(7);
            if (n + rep > hlit + hdist) throw lb.error("repeticion cruza el limite");
            n += rep;
        }
    }
    if (longitudes[256] == 0) throw lb.error("sin codigo de fin de bloque");

    Huffman litlen, dist;
    if (!construirHuffman(&litlen, longitudes, hlit)) throw lb.error("arbol literal/longitud invalido");
    if (!construirHuffman(&dist, longitudes + hlit, hdist)) throw lb.error("arbol de distancias invalido");
    bloque(lb, salida, litlen, dist, maxSalida);
}

} // namespace

std::vector<uint8_t> inflar(const uint8_t* datos, std::size_t tam, std::size_t maxSalida,
                            std::size_t* bytesConsumidos) {
    LectorBits lb{datos, tam};
    std::vector<uint8_t> salida;
    salida.reserve(4096);
    int ultimo = 0;
    do {
        ultimo = lb.bit();
        const int tipo = static_cast<int>(lb.bits(2));
        switch (tipo) {
            case 0: {
                // Bloque almacenado: alinear a byte, LEN/NLEN, copia cruda.
                lb.bitsEnBufer = 0;  // descartar bits parciales
                if (lb.pos + 4 > lb.tam) throw lb.error("bloque almacenado truncado");
                const uint32_t len = lb.datos[lb.pos] | lb.datos[lb.pos + 1] << 8;
                const uint32_t nlen = lb.datos[lb.pos + 2] | lb.datos[lb.pos + 3] << 8;
                lb.pos += 4;
                if (len != ((~nlen) & 0xFFFF)) throw lb.error("LEN/NLEN inconsistentes");
                if (lb.pos + len > lb.tam) throw lb.error("bloque almacenado truncado");
                if (salida.size() + len > maxSalida) {
                    throw ErrorDatos{"deflate: salida excede el maximo permitido (datos corruptos?)"};
                }
                salida.insert(salida.end(), lb.datos + lb.pos, lb.datos + lb.pos + len);
                lb.pos += len;
                break;
            }
            case 1: {
                Huffman litlen, dist;
                construirFijos(&litlen, &dist);
                bloque(lb, salida, litlen, dist, maxSalida);
                break;
            }
            case 2:
                bloqueDinamico(lb, salida, maxSalida);
                break;
            default:
                throw lb.error("tipo de bloque invalido (11)");
        }
    } while (!ultimo);

    if (bytesConsumidos) *bytesConsumidos = lb.pos;
    return salida;
}

// ---------------------------------------------------------------
// CRC32 (tabla estandar reflejada) y Adler-32.
// ---------------------------------------------------------------
uint32_t crc32(const uint8_t* datos, std::size_t tam) {
    static uint32_t tabla[256];
    static bool lista = false;
    if (!lista) {
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            tabla[n] = c;
        }
        lista = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < tam; ++i) c = tabla[(c ^ datos[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

uint32_t adler32(const uint8_t* datos, std::size_t tam) {
    uint32_t a = 1, b = 0;
    for (std::size_t i = 0; i < tam; ++i) {
        a = (a + datos[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return b << 16 | a;
}

std::vector<uint8_t> desinflarAlmacenado(const uint8_t* datos, std::size_t tam) {
    std::vector<uint8_t> salida;
    salida.reserve(tam + tam / 65535 * 5 + 16);
    std::size_t off = 0;
    do {
        const std::size_t restante = tam - off;
        const std::size_t trozo = restante > 65535 ? 65535 : restante;
        const int ultimo = trozo == restante ? 1 : 0;
        salida.push_back(static_cast<uint8_t>(ultimo));          // BFINAL + BTYPE=00
        salida.push_back(static_cast<uint8_t>(trozo & 0xFF));
        salida.push_back(static_cast<uint8_t>(trozo >> 8));
        salida.push_back(static_cast<uint8_t>(~trozo & 0xFF));
        salida.push_back(static_cast<uint8_t>((~trozo >> 8) & 0xFF));
        salida.insert(salida.end(), datos + off, datos + off + trozo);
        off += trozo;
    } while (off < tam);
    return salida;
}

} // namespace assets
