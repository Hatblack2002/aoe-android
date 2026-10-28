#pragma once
// Chequeo de archivos de datos (spec §6 M1 / §8): valida DRS/SLP/paletas/dat,
// cuenta recursos y devuelve informes de texto listos para pantalla o para
// compartir. Lo usan la herramienta assetcheck (escritorio) y la app
// (Android), garantizando el mismo criterio en ambos lados.

#include <cstdint>
#include <string>
#include <vector>

namespace assets {

// Tipos reconocidos.
enum class TipoArchivo { Desconocido, Drs, Slp, Paleta, Dat };

const char* nombreTipo(TipoArchivo t);

// Deteccion: extension primero, contenido despues.
TipoArchivo detectarTipo(const std::string& ruta, const std::vector<uint8_t>& bytes);

// Carga un archivo completo a memoria. Lanza ErrorDatos si no se puede leer.
std::vector<uint8_t> cargarArchivo(const std::string& ruta);

// Valida un archivo ya cargado y devuelve el informe (multilinea).
// Lanza ErrorDatos ante cualquier dato invalido (fallo cerrado).
std::string chequearDatos(TipoArchivo tipo, const std::vector<uint8_t>& bytes,
                          const std::string& nombreVisible);

// Comodin: detecta + valida un archivo de disco.
std::string chequearArchivo(const std::string& ruta);

// Resultado de recorrer varios directorios.
struct ResumenChequeo {
    int ok = 0;
    int fallidos = 0;
    std::string informe;  // linea por archivo, con causa en los fallidos
};

// Lista los archivos regulares de `dir` (sin subdirectorios), ordenados.
std::vector<std::string> listarArchivos(const std::string& dir);

// Recorre los directorios y chequea cada archivo.
ResumenChequeo chequearDirectorios(const std::vector<std::string>& dirs);

} // namespace assets
