// assetcheck: valida y cuenta los datos (spec §6 M1 / §8).
// Uso:
//   assetcheck ARCHIVO...   valida cada archivo y suma el total
//   assetcheck --dir DIR ARCHIVO...   (etiqueta informativa; equivalente)
//   assetcheck --expect-fail ARCHIVO  sale 0 si TODOS fallan con mensaje claro
//                                      (pruebas negativas de truncado/corrupcion)
//
// La logica vive en src/assets/chequeo.cpp para que la app Android use el
// mismo criterio que esta herramienta.

#include <cstdio>
#include <string>
#include <vector>

#include "assets/chequeo.h"
#include "assets/formato.h"

int main(int argc, char** argv) {
    std::vector<std::string> archivos;
    bool expectFail = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--expect-fail") {
            expectFail = true;
        } else if (a == "--dir") {
            // Compatibilidad informativa: los archivos van a continuacion.
            continue;
        } else {
            archivos.push_back(a);
        }
    }
    if (archivos.empty()) {
        fprintf(stderr,
                "uso: assetcheck [--expect-fail] ARCHIVO...\n"
                "  valida DRS/SLP/paletas/dat y reporta conteos; fallo cerrado con causa.\n");
        return 2;
    }

    int ok = 0, fallidos = 0;
    for (const std::string& ruta : archivos) {
        try {
            const std::string informe = assets::chequearArchivo(ruta);
            printf("%s\n%s", ruta.c_str(), informe.c_str());
            ++ok;
        } catch (const assets::ErrorDatos& e) {
            printf("ERROR: %s: %s\n", ruta.c_str(), e.mensaje.c_str());
            ++fallidos;
        }
    }

    printf("ASSETCHECK: %d OK, %d FALLIDOS\n", ok, fallidos);
    if (expectFail) {
        // Prueba negativa: se ESPERA que todo falle con mensaje claro.
        const bool bien = fallidos > 0 && ok == 0;
        printf("EXPECT-FAIL: %s\n", bien ? "correcto (todos fallaron como debian)"
                                          : "FALLO (algun archivo paso)");
        return bien ? 0 : 1;
    }
    return fallidos == 0 ? 0 : 1;
}
