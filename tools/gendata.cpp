// gendata: genera los archivos sinteticos de prueba (spec §8).
// Uso:
//   gendata DIRECTORIO               suite basica (14 archivos + manifiesto)
//   gendata DIRECTORIO --grande      anade grande.dat (~26 MB, dat valido)
//   gendata DIRECTORIO --drs-realista  anade interfac_estilo.drs (3 tablas)

#include <cstdio>
#include <string>

#include "assets/formato.h"
#include "assets/gen.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "uso: gendata DIRECTORIO [--grande] [--drs-realista]\n");
        return 2;
    }
    bool grande = false, realista = false;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--grande") grande = true;
        else if (a == "--drs-realista") realista = true;
        else {
            fprintf(stderr, "gendata: opcion desconocida \"%s\"\n", a.c_str());
            return 2;
        }
    }
    try {
        int n = assets::generarDatosPrueba(argv[1]);
        if (n < 0) {
            fprintf(stderr, "gendata: no se pudo escribir en %s\n", argv[1]);
            return 1;
        }
        if (grande && assets::generarGrande(argv[1], 26) < 0) n = -1;
        if (realista && assets::generarDrsRealista(argv[1]) < 0) n = -1;
        if (n < 0) {
            fprintf(stderr, "gendata: no se pudo escribir en %s\n", argv[1]);
            return 1;
        }
        printf("GENDATA OK: %d archivos en %s%s%s\n", n, argv[1],
               grande ? " [+grande.dat]" : "", realista ? " [+interfac_estilo.drs]" : "");
        return 0;
    } catch (const assets::ErrorDatos& e) {
        fprintf(stderr, "gendata: %s\n", e.mensaje.c_str());
        return 1;
    }
}
