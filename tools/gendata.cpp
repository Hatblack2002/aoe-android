// gendata: genera los archivos sinteticos de prueba (spec §8).
// Uso: gendata DIRECTORIO

#include <cstdio>
#include <string>

#include "assets/formato.h"
#include "assets/gen.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "uso: gendata DIRECTORIO\n");
        return 2;
    }
    try {
        const int n = assets::generarDatosPrueba(argv[1]);
        if (n < 0) {
            fprintf(stderr, "gendata: no se pudo escribir en %s\n", argv[1]);
            return 1;
        }
        printf("GENDATA OK: %d archivos en %s\n", n, argv[1]);
        return 0;
    } catch (const assets::ErrorDatos& e) {
        fprintf(stderr, "gendata: %s\n", e.mensaje.c_str());
        return 1;
    }
}
