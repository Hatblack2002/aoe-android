#include "platform/metrics.h"

#include <SDL.h>
#include <cstdio>
#include <cstring>

namespace platform {

namespace {
float g_fps = 0.0f;
Uint32 g_anterior = 0;
bool g_primero = true;
long g_ramKb = -1;

// VmRSS: memoria real del proceso. Funciona igual en Android y Linux.
void leerRam() {
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) {
        g_ramKb = -1;
        return;
    }
    char linea[256];
    while (fgets(linea, sizeof(linea), f)) {
        if (strncmp(linea, "VmRSS:", 6) == 0) {
            long kb = 0;
            if (sscanf(linea + 6, "%ld", &kb) == 1) g_ramKb = kb;
            break;
        }
    }
    fclose(f);
}
} // namespace

void Metrics::beginFrame() {
    const Uint32 ahora = SDL_GetTicks();
    if (!g_primero) {
        const float dt = static_cast<float>(ahora - g_anterior);
        if (dt > 0.0f) {
            const float instantaneo = 1000.0f / dt;
            g_fps = (g_fps <= 0.0f) ? instantaneo : g_fps * 0.9f + instantaneo * 0.1f;
        }
    }
    g_anterior = ahora;
    g_primero = false;
    leerRam();
}

void Metrics::endFrame() {}

float Metrics::fps() { return g_fps; }

long Metrics::ramKb() { return g_ramKb; }

} // namespace platform
