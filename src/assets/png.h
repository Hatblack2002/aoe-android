#pragma once
// Escritor PNG minimo y determinista (sin dependencias).
// Genera PNG validos de 8 bits RGB (color type 2) usando bloques DEFLATE
// almacenados (sin compresion): sobra para sprites de prueba y evidencia,
// y evita meter una biblioteca de imagenes al motor (spec §4: assets no
// depende de nada).

#include <cstdint>
#include <string>
#include <vector>

namespace assets {

struct RGB {
    uint8_t r, g, b;
};

// Escribe un PNG RGB de ancho*alto pixeles. `pixeles` debe tener ancho*alto
// elementos (orden por filas). Devuelve los bytes del archivo.
std::vector<uint8_t> escribirPng(int ancho, int alto, const std::vector<RGB>& pixeles);

// Escribir a archivo; devuelve false si no se pudo (fallo visible, nunca
// silencioso).
bool escribirPngArchivo(const std::string& ruta, int ancho, int alto,
                        const std::vector<RGB>& pixeles, std::string* error = nullptr);

} // namespace assets
