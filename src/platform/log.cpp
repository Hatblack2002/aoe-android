#include "platform/log.h"

#include <SDL.h>
#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace platform {

namespace {
std::mutex g_mutex;
std::string g_ruta;
std::vector<std::string> g_anillo;
constexpr std::size_t ANILLO_MAX = 64;
Uint32 g_inicio = 0;
bool g_archivoOk = false;

void pushLinea(const std::string& linea) {
    if (g_anillo.size() >= ANILLO_MAX) g_anillo.erase(g_anillo.begin());
    g_anillo.push_back(linea);
}
} // namespace

void Log::init(const std::string& rutaArchivo) {
    std::lock_guard<std::mutex> l(g_mutex);
    g_inicio = SDL_GetTicks();
    g_ruta = rutaArchivo;
    if (rutaArchivo.empty()) {
        g_archivoOk = false;
        return;
    }
    // Trunca al inicio de la sesión y comprueba que se puede escribir.
    FILE* f = fopen(rutaArchivo.c_str(), "w");
    if (f) {
        fprintf(f, "# AoE Mobile log (nueva sesion)\n");
        fclose(f);
        g_archivoOk = true;
    } else {
        // Fallo cerrado y visible: sin archivo no se oculta nada.
        g_archivoOk = false;
    }
}

void Log::write(const char* formato, ...) {
    char cuerpo[1024];
    va_list args;
    va_start(args, formato);
    vsnprintf(cuerpo, sizeof(cuerpo), formato, args);
    va_end(args);

    const Uint32 ms = SDL_GetTicks() - g_inicio;
    char sello[32];
    snprintf(sello, sizeof(sello), "[%02u:%02u.%03u]", (unsigned)(ms / 60000),
             (unsigned)((ms / 1000) % 60), (unsigned)(ms % 1000));

    const std::string linea = std::string(sello) + " " + cuerpo;

    std::lock_guard<std::mutex> l(g_mutex);
    pushLinea(linea);
    SDL_Log("%s", linea.c_str());
    if (g_archivoOk) {
        FILE* f = fopen(g_ruta.c_str(), "a");
        if (f) {
            fprintf(f, "%s\n", linea.c_str());
            fclose(f);
        } else {
            // Si el archivo deja de ser escribible, se nota en pantalla.
            g_archivoOk = false;
        }
    }
}

std::vector<std::string> Log::recentLines(std::size_t maxLineas) {
    std::lock_guard<std::mutex> l(g_mutex);
    if (maxLineas >= g_anillo.size()) return g_anillo;
    return std::vector<std::string>(g_anillo.end() - static_cast<long>(maxLineas), g_anillo.end());
}

const std::string& Log::filePath() {
    std::lock_guard<std::mutex> l(g_mutex);
    return g_ruta;
}

bool Log::fileOk() {
    std::lock_guard<std::mutex> l(g_mutex);
    return g_archivoOk;
}

} // namespace platform
