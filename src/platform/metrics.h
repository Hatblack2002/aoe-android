#pragma once

namespace platform {

// Medición de rendimiento desde el día uno (spec §3, principio 5):
// FPS como media móvil exponencial y RAM del proceso vía /proc/self/status.
class Metrics {
public:
    static void beginFrame();  // llama al inicio de cada cuadro
    static void endFrame();    // al final (reservado)

    static float fps();   // 0.0 mientras no haya datos
    static long ramKb();  // VmRSS en KiB; -1 si no se puede leer
};

} // namespace platform
