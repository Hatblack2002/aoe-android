#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace platform {

// Registro con tres destinos (capa platform, spec §4):
//  1) SDL_Log -> logcat en Android / stderr en escritorio
//  2) archivo (game.log) que el usuario puede compartir
//  3) anillo en memoria para mostrar las últimas líneas en pantalla
class Log {
public:
    // rutaArchivo vacío = sin archivo. Se llama una vez, al inicio.
    static void init(const std::string& rutaArchivo);

    // Estilo printf. Toda línea queda con sello de tiempo [mm:ss.mmm].
    static void write(const char* formato, ...) __attribute__((format(printf, 1, 2)));

    // Últimas líneas (para el overlay en pantalla y para COMPARTIR LOG).
    static std::vector<std::string> recentLines(std::size_t maxLineas);

    static const std::string& filePath();
    static bool fileOk();  // false = no se pudo abrir el archivo (se muestra en pantalla)
};

} // namespace platform
