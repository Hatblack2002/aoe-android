#pragma once
// Lector de empires.dat (AoE1 original / Rise of Rome, layout ROR).
// FORMATO VERIFICADO contra openage (no de memoria):
//   - Compresion: DEFLATE crudo RFC 1951, wbits=-15
//     (openage convert/service/read/gamedata.py: zlib.decompress(data, -15))
//   - Estructura: openage convert/value_object/read/media/datfile/
//     empiresdat.py + civ.py + unit.py + terrain.py + maps.py + tech.py +
//     playercolor.py + sound.py, ramas ROR.
//   - Constantes fijas del layout ROR: 32 terrains, 16 terrain borders,
//     19 tile sizes.
//
// El lector CAMINA toda la estructura validando cada campo (fallo cerrado
// con offset exacto) y extrae lo que el motor necesita: mapa de tiles y la
// tabla de unidades/edificios por civilizacion.
//
// NOTA HONESTA (spec §2/§11): el layout esta verificado para Rise of Rome.
// Para el AoE1 original (1997) puede diferir en alguna constante; si eso
// pasa, assetcheck lo reportara con el desplazamiento exacto y se ajustara
// con evidencia del archivo real, no adivinando.

#include <cstdint>
#include <string>
#include <vector>

namespace assets {

struct CostoUnidad {
    int16_t tipo = 0;
    int16_t cantidad = 0;
    int16_t activo = 0;
};

struct Unidad {
    int8_t tipo = 0;              // 10 obj, 20 anim, 25 doppel, 30 mover,
                                  // 40 accion, 60 misil, 70 vivo, 80 edificio, 90 arbol
    std::string nombre;
    int16_t id = 0;
    int16_t clase = 0;            // unit_class
    int16_t hitPoints = 0;
    float radioX = 0.0f, radioY = 0.0f;
    int16_t graficoQuieto = 0;    // idle_graphic0 (SLP en graphics.drs)
    int16_t graficoMuerte = 0;    // dying_graphic
    float velocidad = 0.0f;       // tipo >= 20
    int16_t graficoMover = 0;     // tipo >= 30 (move_graphics)
    float ritmoTrabajo = 0.0f;    // tipo >= 40 (work_rate)
    CostoUnidad costos[3] = {};   // tipo >= 70 (resource_cost x3)
    int16_t graficoConstruccion = 0;  // tipo >= 80
};

struct Civilizacion {
    std::string nombre;
    int8_t tipoJugador = 0;
    uint16_t numRecursos = 0;
    uint16_t numUnidades = 0;  // tamano de la tabla (incluye huecos con offset 0)
    std::vector<Unidad> unidades;
};

struct ResumenDat {
    std::string versionStr;
    uint16_t restriccionesTerreno = 0;
    uint16_t numTerrenos = 0;       // terrain_count (usados)
    uint16_t numColoresJugador = 0;
    uint16_t numSonidos = 0;
    uint16_t numGraficos = 0;
    int32_t mapaAncho = 0, mapaAlto = 0;
    int32_t mundoAncho = 0, mundoAlto = 0;
    int16_t tileAncho = 0, tileAlto = 0;
    int16_t tileMediaAncho = 0, tileMediaAlto = 0, alturaElevacion = 0;
    uint32_t numMapas = 0;
    uint32_t numPaquetesEfectos = 0;
    uint16_t numCivs = 0;
    uint16_t numInvestigaciones = 0;
    std::vector<Civilizacion> civs;
    unsigned long tamDescomprimido = 0;
    unsigned long bytesSinParsear = 0;  // cola (investigaciones y posteriores)
};

// Lee e interpreta el dat (recibe los bytes ya INFLADOS). Lanza ErrorDatos
// ante cualquier inconsistencia.
ResumenDat leerDat(const uint8_t* datos, unsigned long tam, const std::string& nombreArchivo);

} // namespace assets
