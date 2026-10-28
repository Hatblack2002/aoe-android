#pragma once
// Inflador DEFLATE crudo (RFC 1951), sin cabecera zlib ni CRC (wbits = -15).
// Verificado contra la forma en que openage lee empires.dat:
//   zlib.decompress(compressed_data, -15)
//   (openage convert/service/read/gamedata.py, metodo load_gamespec)
//
// Solo lo necesario para los archivos del juego: bloques almacenados (00),
// Huffman fijo (01) y Huffman dinamico (10). Cualquier otra cosa -> fallo
// cerrado con mensaje claro (spec §2 principio 3).
//
// Limites: la salida se corta en maxSalida bytes para evitar bombardeo de
// memoria con datos corruptos (un dat real descomprime a unos pocos MB).

#include <cstddef>
#include <cstdint>
#include <vector>

namespace assets {

// Descomprime un flujo DEFLATE crudo. Lanza ErrorDatos ante cualquier dato
// invalido o si la salida excede maxSalida.
std::vector<uint8_t> inflar(const uint8_t* datos, std::size_t tam,
                            std::size_t maxSalida, std::size_t* bytesConsumidos = nullptr);

// Escritor de bloques DEFLATE "almacenados" (sin compresion): flujo valido
// que nuestro inflador y cualquier inflador estandar aceptan. Lo usa el
// generador de datos sinteticos y el escritor PNG.
std::vector<uint8_t> desinflarAlmacenado(const uint8_t* datos, std::size_t tam);

// CRC32 (ISO 3309, tabla estandar de zlib/PNG) y Adler-32 (RFC 1950).
uint32_t crc32(const uint8_t* datos, std::size_t tam);
uint32_t adler32(const uint8_t* datos, std::size_t tam);

} // namespace assets
