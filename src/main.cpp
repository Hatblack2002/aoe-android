// M1 - lectura de datos del AoE original (spec §6 M1).
// Mantiene lo del M0 (version, FPS/RAM, registro, COMPARTIR LOG) y anade la
// pantalla de herramientas: GENERAR (datos sinteticos), ASSETCHECK (valida
// gen/ + import/), IMPORTAR (selector SAF para los archivos reales del
// usuario), DUMP SLP (decodifica un marco y lo muestra + PNG) y COMPARTIR
// (envia el informe por ACTION_SEND).
//
// Capas respetadas (spec §4): platform + render aqui; la logica de lectura
// vive en assets/ y no conoce SDL.

#include <SDL.h>
#ifdef __ANDROID__
#include <jni.h>          // tipos JNI; SDL no los incluye
#include <SDL_system.h>   // SDL_AndroidGetJNIEnv/GetActivity devuelven void*
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <sys/types.h>

#include "assets/chequeo.h"
#include "assets/dat.h"
#include "assets/drs.h"
#include "assets/formato.h"
#include "assets/gen.h"
#include "assets/inflate.h"
#include "assets/paleta.h"
#include "assets/png.h"
#include "assets/slp.h"
#include "game/mapa.h"
#include "platform/log.h"
#include "platform/metrics.h"
#include "render/framebuffer.h"
#include "render/isometrico.h"
#include "version.h"

namespace {

constexpr int FB_W = 960;
constexpr int FB_H = 540;

struct Opciones {
    bool selftest = false;               // N cuadros sin ventana -> captura + registro
    bool demoM1 = false;                 // genera, chequea y decodifica antes del bucle
    bool demoM2 = false;                 // M2: mapa isometrico con camara -> PNG de evidencia
    std::string datM2;                   // ruta a empires.dat para el tamano de mosaico (demo)
    int cuadros = 120;
    std::string captura = "selftest.bmp";
    std::string dirLog;
};

Opciones parseArgs(int argc, char** argv) {
    Opciones op;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--selftest") {
            op.selftest = true;
        } else if (a == "--m1-demo") {
            op.demoM1 = true;
        } else if (a == "--m2-demo") {
            op.demoM2 = true;
        } else if (a == "--dat" && i + 1 < argc) {
            op.datM2 = argv[++i];
        } else if (a == "--frames" && i + 1 < argc) {
            op.cuadros = atoi(argv[++i]);
        } else if (a == "--screenshot" && i + 1 < argc) {
            op.captura = argv[++i];
        } else if (a == "--logdir" && i + 1 < argc) {
            op.dirLog = argv[++i];
        }
    }
    return op;
}

std::string versionSdl() {
    SDL_version v;
    SDL_GetVersion(&v);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d.%d.%d", v.major, v.minor, v.patch);
    return buf;
}

// Donde guardar el registro, por orden de preferencia (igual que M0).
std::string rutaDeLog(const Opciones& op) {
    if (!op.dirLog.empty()) {
        std::string dir = op.dirLog;
        if (dir.back() != '/') dir += '/';
        return dir + "game.log";
    }
#ifdef __ANDROID__
    const Uint32 estado = SDL_AndroidGetExternalStorageState();
    if (estado & SDL_ANDROID_EXTERNAL_STORAGE_WRITE) {
        const char* ext = SDL_AndroidGetExternalStoragePath();
        if (ext) return std::string(ext) + "/game.log";
    }
#endif
    char* pref = SDL_GetPrefPath("aoe-mobile", "logs");
    if (pref) {
        std::string ruta = std::string(pref) + "game.log";
        SDL_free(pref);
        return ruta;
    }
    return std::string();
}

#ifdef __ANDROID__

// Bandera que activa Java al terminar de copiar los archivos importados.
bool g_importacionTerminada = false;

} // namespace

// Llamado por MainActivity (JNI) tras copiar los archivos elegidos.
extern "C" JNIEXPORT void JNICALL
Java_com_aoemobile_engine_MainActivity_nativeOnFilesImported(JNIEnv*, jclass, jint cuantos) {
    platform::Log::write("IMPORTAR: %d archivo(s) copiado(s) por Java", cuantos);
    g_importacionTerminada = true;
}

namespace {

// Abre el selector de archivos de Android (SAF) por reflexion sobre MainActivity.
bool pedirImportacion() {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    if (!env) return false;
    jobject actividad = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!actividad) return false;
    bool ok = false;
    jclass clase = env->GetObjectClass(actividad);
    if (clase) {
        jmethodID metodo = env->GetMethodID(clase, "startFilePickerFromNative", "()V");
        if (metodo) {
            env->CallVoidMethod(actividad, metodo);
            ok = env->ExceptionCheck() != JNI_TRUE;
            if (env->ExceptionCheck() == JNI_TRUE) env->ExceptionClear();
        } else {
            platform::Log::write("IMPORTAR: metodo startFilePickerFromNative no encontrado");
            if (env->ExceptionCheck() == JNI_TRUE) env->ExceptionClear();
        }
        env->DeleteLocalRef(clase);
    }
    env->DeleteLocalRef(actividad);
    return ok;
}

// Comparte texto con la app que el usuario elija (ACTION_SEND + EXTRA_TEXT).
bool compartirTexto(const std::string& texto) {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    if (!env) return false;
    jobject actividad = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!actividad) return false;

    bool ok = false;
    jclass claseIntent = env->FindClass("android/content/Intent");
    if (claseIntent) {
        jmethodID ctor = env->GetMethodID(claseIntent, "<init>", "(Ljava/lang/String;)V");
        jobject intent = ctor ? env->NewObject(claseIntent, ctor,
                                               env->NewStringUTF("android.intent.action.SEND"))
                              : nullptr;
        jmethodID setType =
            env->GetMethodID(claseIntent, "setType", "(Ljava/lang/String;)Landroid/content/Intent;");
        jmethodID putExtra = env->GetMethodID(
            claseIntent, "putExtra", "(Ljava/lang/String;Ljava/lang/CharSequence;)Landroid/content/Intent;");
        if (intent && setType && putExtra) {
            env->CallObjectMethod(intent, setType, env->NewStringUTF("text/plain"));
            jstring clave = env->NewStringUTF("android.intent.extra.TEXT");
            jstring valor = env->NewStringUTF(texto.c_str());
            env->CallObjectMethod(intent, putExtra, clave, valor);

            jclass claseActividad = env->GetObjectClass(actividad);
            jmethodID lanzar =
                env->GetMethodID(claseActividad, "startActivity", "(Landroid/content/Intent;)V");
            if (lanzar) env->CallVoidMethod(actividad, lanzar, intent);

            ok = (env->ExceptionCheck() != JNI_TRUE);
            if (env->ExceptionCheck() == JNI_TRUE) env->ExceptionClear();
            if (claseActividad) env->DeleteLocalRef(claseActividad);
            if (clave) env->DeleteLocalRef(clave);
            if (valor) env->DeleteLocalRef(valor);
        }
        if (intent) env->DeleteLocalRef(intent);
        env->DeleteLocalRef(claseIntent);
    } else if (env->ExceptionCheck() == JNI_TRUE) {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(actividad);
    return ok;
}

#endif // __ANDROID__

// ---------------------------------------------------------------
// Estado de la pantalla M1.
// ---------------------------------------------------------------
struct EstadoM1 {
    std::string dirBase;       // raiz de archivos (Android: external files)
    std::string dirGen;        // datos sinteticos
    std::string dirImport;     // archivos reales del usuario
    std::string dirDumps;      // PNG de salida
    std::string informe;       // ultimo informe (compartible)
    bool haySprite = false;    // sprite decodificado en memoria
    std::vector<uint8_t> spriteRgba;
    int spriteAncho = 0, spriteAlto = 0;
    std::string spriteInfo;
    bool spriteReal = false;   // ORIGEN: true = archivos reales; false = sintetico
    std::string spriteCausa;   // por que cayo al sintetico (o vacio si es REAL)
    int marcoActual = 0;       // para ciclar marcos con DUMP SLP
    int jugadroActual = 1;     // jugador para el color
};

std::string rutaDeLogM1(const EstadoM1&, const Opciones& op) { return rutaDeLog(op); }

// mkdir -p de un nivel (los padres existen: es la carpeta de la app).
void crearDir(const std::string& ruta) {
#ifdef _WIN32
    _mkdir(ruta.c_str());
#else
    mkdir(ruta.c_str(), 0755);
#endif
}

// Anade una seccion al informe acumulado (con tope de memoria).
void acumular(EstadoM1& e, const std::string& titulo, const std::string& texto) {
    std::string seccion = "== " + titulo + " ==\n" + texto + "\n";
    if (e.informe.size() + seccion.size() > 24000) e.informe.clear();
    e.informe += seccion;
}

// Termina la ruta en `sufijo` ignorando mayusculas/minisculas (el usuario
// importa "EMPIRES.DAT", "Empires.dat", ...; ext4 distingue).
bool terminaEnCi(const std::string& s, const char* sufijo) {
    const std::size_t n = strlen(sufijo);
    if (s.size() < n) return false;
    for (std::size_t i = 0; i < n; ++i) {
        const char a = s[s.size() - n + i];
        const char b = sufijo[i];
        const char ca = a >= 'A' && a <= 'Z' ? static_cast<char>(a + 32) : a;
        const char cb = b >= 'A' && b <= 'Z' ? static_cast<char>(b + 32) : b;
        if (ca != cb) return false;
    }
    return true;
}

std::string nombreDeRuta(const std::string& ruta) {
    const std::size_t b = ruta.find_last_of('/');
    return b == std::string::npos ? ruta : ruta.substr(b + 1);
}

// Quita el prefijo largo del directorio de un mensaje de error (las rutas
// completas comen espacio de pantalla y no aportan: ya estan en ASSETCHECK).
std::string acortarMensaje(const std::string& msg, const std::string& prefijo) {
    if (prefijo.empty() || prefijo.size() + 1 > msg.size()) return msg;
    std::string s = msg;
    const std::string conBarra = prefijo + (prefijo.back() == '/' ? "" : "/");
    std::size_t pos = 0;
    while ((pos = s.find(conBarra, pos)) != std::string::npos) {
        s.replace(pos, conBarra.size(), "");
        pos += 1;
    }
    return s;
}

// Mensaje corto con nombre del archivo, sin duplicarlo si ya empieza asi.
std::string conNombre(const std::string& nombre, const std::string& msg,
                      const std::string& prefijo) {
    const std::string corto = acortarMensaje(msg, prefijo);
    if (corto.rfind(nombre, 0) == 0) return corto;
    return nombre + ": " + corto;
}

void componerSprite(const assets::MarcoSLP& marco, const assets::Paleta& pal, int base,
                    EstadoM1& estado) {
    estado.spriteRgba.assign(static_cast<std::size_t>(marco.ancho) * marco.alto * 4, 0);
    for (int y = 0; y < marco.alto; ++y) {
        for (int x = 0; x < marco.ancho; ++x) {
            const assets::PixelSLP& p =
                marco.pixeles[static_cast<std::size_t>(y) * marco.ancho + x];
            uint8_t* d = estado.spriteRgba.data() +
                         (static_cast<std::size_t>(y) * marco.ancho + x) * 4;
            switch (p.tipo) {
                case assets::PixelTipo::Normal:
                    d[0] = pal[p.indice].r; d[1] = pal[p.indice].g; d[2] = pal[p.indice].b;
                    d[3] = 255;
                    break;
                case assets::PixelTipo::Jugador:
                case assets::PixelTipo::ContornoJugador: {
                    const int indice = base + (p.tipo == assets::PixelTipo::ContornoJugador ? 0 : p.indice);
                    if (indice >= 0 && indice <= 255) {
                        d[0] = pal[indice].r; d[1] = pal[indice].g; d[2] = pal[indice].b;
                    }
                    d[3] = 255;
                    break;
                }
                case assets::PixelTipo::Sombra:
                    d[0] = d[1] = d[2] = 40;
                    d[3] = 255;
                    break;
                case assets::PixelTipo::Contorno2:
                    d[0] = pal[243].r; d[1] = pal[243].g; d[2] = pal[243].b;
                    d[3] = 255;
                    break;
                default:
                    d[3] = 0;  // transparente
            }
        }
    }
    estado.spriteAncho = marco.ancho;
    estado.spriteAlto = marco.alto;
    estado.haySprite = true;
}

// Acciones de los botones (compartidas por escritorio y Android).
void accionGenerar(EstadoM1& e) {
    try {
        crearDir(e.dirGen);
        const int n = assets::generarDatosPrueba(e.dirGen);
        if (n < 0) {
            acumular(e, "GENERAR", "ERROR: no se pudo escribir en " + e.dirGen);
        } else {
            acumular(e, "GENERAR",
                     "OK: " + std::to_string(n) + " archivos sinteticos en " + e.dirGen);
        }
    } catch (const assets::ErrorDatos& err) {
        e.informe += std::string("ERROR: ") + err.mensaje + "\n";
    }
    platform::Log::write("GENERAR: accion ejecutada");
}

void accionAssetcheck(EstadoM1& e) {
    const assets::ResumenChequeo r =
        assets::chequearDirectorios({e.dirGen, e.dirImport});
    // El informe ya trae resumen por directorio, causas completas y evidencia
    // hex de los fallos (chequeo.cpp).
    acumular(e, "ASSETCHECK", r.informe);
    platform::Log::write("ASSETCHECK: %d OK, %d FALLIDOS", r.ok, r.fallidos);
    // El informe completo queda en archivo para que el usuario lo comparta.
    FILE* f = fopen((e.dirBase + "/m1_assetcheck.txt").c_str(), "wb");
    if (f) {
        fwrite(r.informe.data(), 1, r.informe.size(), f);
        fclose(f);
    }
}

// Busca candidatos de sprite y paleta entre los archivos REALES importados.
// Nunca cae al sintetico en silencio: todo intento fallido queda en `notas`
// con su causa completa (spec §2, principio 1).
struct Candidatos {
    std::vector<std::string> slps;  // "ruta.slp" o "ruta.drs::id.slp", en orden
    std::string paleta;             // "ruta.pal" o "ruta.drs::id.bina"
    std::string infoPaleta;         // de donde salio la paleta
    std::vector<std::string> notas; // errores y decisiones, para el informe
    bool hayReales = false;
};

Candidatos recolectarCandidatos(const EstadoM1& e) {
    Candidatos c;
    const std::vector<std::string> importados = assets::listarArchivos(e.dirImport);
    if (importados.empty()) {
        c.notas.push_back("import/ esta vacio: no hay archivos reales (toca IMPORTAR)");
        return c;
    }

    // 1) SLP sueltos importados (extension case-insensitive).
    for (const std::string& ruta : importados) {
        if (terminaEnCi(ruta, ".slp")) {
            c.slps.push_back(ruta);
        }
    }
    if (!c.slps.empty()) {
        c.notas.push_back("SLP suelto(s) importado(s): " +
                          std::to_string(c.slps.size()));
    }

    // 2) Paleta suelta importada (.pal).
    for (const std::string& ruta : importados) {
        if (terminaEnCi(ruta, ".pal") && c.paleta.empty()) {
            c.paleta = ruta;
            c.infoPaleta = nombreDeRuta(ruta) + " (suelta)";
        }
    }

    // 3) DRS importados: primeros SLPs del primero que abra + paleta bina.
    for (const std::string& ruta : importados) {
        if (!terminaEnCi(ruta, ".drs")) continue;
        try {
            const std::vector<uint8_t> bytes = assets::cargarArchivo(ruta);
            if (assets::detectarTipo(ruta, bytes) != assets::TipoArchivo::Drs) {
                c.notas.push_back(nombreDeRuta(ruta) + ": no es un DRS por contenido");
                continue;
            }
            assets::LectorDRS drs(bytes.data(), bytes.size(), ruta);
            int candidatosDrs = 0;
            for (const assets::EntradaDRS& en : drs.info().archivos) {
                if (en.extension == "slp" && candidatosDrs < 12) {
                    c.slps.push_back(ruta + "::" + std::to_string(en.id) + ".slp");
                    ++candidatosDrs;
                }
            }
            if (candidatosDrs > 0) {
                c.notas.push_back(nombreDeRuta(ruta) + ": " +
                                  std::to_string(drs.info().archivos.size()) +
                                  " recurso(s); probando " + std::to_string(candidatosDrs) +
                                  " sprite(s) SLP");
            } else {
                c.notas.push_back(nombreDeRuta(ruta) + ": abre pero no contiene SLPs");
            }
            if (c.paleta.empty()) {
                const assets::EntradaDRS* pal = nullptr;
                for (const assets::EntradaDRS& en : drs.info().archivos) {
                    if (en.extension == "bina" && en.id >= 50500 && en.id < 50600) {
                        pal = &en;
                        break;
                    }
                }
                if (!pal) {
                    for (const assets::EntradaDRS& en : drs.info().archivos) {
                        if (en.extension == "bina") {
                            pal = &en;
                            break;
                        }
                    }
                }
                if (pal) {
                    c.paleta = ruta + "::" + std::to_string(pal->id) + ".bina";
                    c.infoPaleta = nombreDeRuta(ruta) + " id " + std::to_string(pal->id);
                }
            }
        } catch (const assets::ErrorDatos& err) {
            c.notas.push_back(conNombre(nombreDeRuta(ruta), err.mensaje, e.dirImport));
        }
    }
    c.hayReales = !c.slps.empty();
    return c;
}

// Carga (SLP|DRS::id) -> bytes del recurso.
std::vector<uint8_t> cargarRecursoSlp(const std::string& ruta) {
    const std::size_t sep = ruta.find("::");
    if (sep == std::string::npos) {
        return assets::cargarArchivo(ruta);
    }
    const std::string drsRuta = ruta.substr(0, sep);
    const std::string resto = ruta.substr(sep + 2);
    const std::size_t punto = resto.find('.');
    const uint32_t id = static_cast<uint32_t>(strtoul(resto.substr(0, punto).c_str(), nullptr, 10));
    const std::vector<uint8_t> bytes = assets::cargarArchivo(drsRuta);
    assets::LectorDRS drs(bytes.data(), bytes.size(), drsRuta);
    const assets::EntradaDRS* entrada = drs.buscar(id, "slp");
    if (!entrada) {
        throw assets::ErrorDatos{drsRuta + ": no contiene el recurso " + std::to_string(id) + ".slp"};
    }
    return drs.extraer(*entrada);
}

std::vector<uint8_t> cargarRecursoPaleta(const std::string& ruta) {
    const std::size_t sep = ruta.find("::");
    if (sep == std::string::npos) {
        return assets::cargarArchivo(ruta);
    }
    const std::string drsRuta = ruta.substr(0, sep);
    const std::string resto = ruta.substr(sep + 2);
    const std::size_t punto = resto.find('.');
    const uint32_t id = static_cast<uint32_t>(strtoul(resto.substr(0, punto).c_str(), nullptr, 10));
    const std::vector<uint8_t> bytes = assets::cargarArchivo(drsRuta);
    assets::LectorDRS drs(bytes.data(), bytes.size(), drsRuta);
    const assets::EntradaDRS* entrada = drs.buscar(id, "bina");
    if (!entrada) {
        throw assets::ErrorDatos{drsRuta + ": no contiene la paleta " + std::to_string(id)};
    }
    return drs.extraer(*entrada);
}

// Comodines de tamano para el informe del sprite.
static int estado_spriteTamano(const EstadoM1& e) { return e.spriteAncho * e.spriteAlto; }

void accionDump(EstadoM1& e) {
    try {
        const Candidatos cand = recolectarCandidatos(e);
        std::vector<std::string> fallosReales;

        assets::MarcoSLP marco;
        bool obtenido = false;
        std::string fuenteSlp;
        int totalMarcos = 0;

        // 1) Probar los candidatos reales en orden, hasta uno que decodifique.
        for (const std::string& candidato : cand.slps) {
            if (obtenido) break;
            try {
                const std::vector<uint8_t> bytesSlp = cargarRecursoSlp(candidato);
                assets::DecodificadorSLP slp(bytesSlp.data(), bytesSlp.size(), candidato);
                if (slp.numMarcos() < 1) {
                    fallosReales.push_back(nombreDeRuta(candidato) + ": 0 marcos");
                    continue;
                }
                if (e.marcoActual >= slp.numMarcos()) e.marcoActual = 0;
                marco = slp.decodificarMarco(e.marcoActual);
                if (marco.ancho <= 0 || marco.alto <= 0 ||
                    static_cast<long>(marco.ancho) * marco.alto > 64L * 1024 * 1024) {
                    fallosReales.push_back(nombreDeRuta(candidato) + ": marco " +
                                           std::to_string(e.marcoActual) + " de tamano " +
                                           std::to_string(marco.ancho) + "x" +
                                           std::to_string(marco.alto));
                    continue;
                }
                obtenido = true;
                fuenteSlp = candidato;
                totalMarcos = slp.numMarcos();
            } catch (const assets::ErrorDatos& err) {
                fallosReales.push_back(
                    conNombre(nombreDeRuta(candidato), err.mensaje, e.dirImport));
            }
        }

        // 2) Paleta: real si se puede; sintetica de reserva con aviso.
        std::string infoPaleta;
        bool palReal = false;
        assets::Paleta paletaUsada{};
        if (!cand.paleta.empty()) {
            try {
                const std::vector<uint8_t> bytesPal = cargarRecursoPaleta(cand.paleta);
                paletaUsada = assets::leerPaleta(bytesPal.data(), bytesPal.size());
                infoPaleta = cand.infoPaleta;
                palReal = true;
            } catch (const assets::ErrorDatos& err) {
                fallosReales.push_back(conNombre("paleta " + nombreDeRuta(cand.paleta),
                                                 err.mensaje, e.dirImport));
            }
        }
        if (!palReal) {
            const std::string rutaPal = e.dirGen + "/prueba.pal";
            const std::vector<uint8_t> bytesPal = assets::cargarArchivo(rutaPal);
            paletaUsada = assets::leerPaleta(bytesPal.data(), bytesPal.size());
            infoPaleta = "sintetica (reserva)";
        }

        // 3) Componer el sprite REAL, o el sintetico de reserva con la causa.
        std::string texto;
        if (obtenido) {
            e.spriteReal = true;
            e.spriteCausa.clear();
            const int base = 16 * e.jugadroActual;  // AoK/AoE1: 16 por jugador (verificar con dat real)
            componerSprite(marco, paletaUsada, base, e);
            e.spriteInfo = "marco " + std::to_string(e.marcoActual) + "/" +
                           std::to_string(totalMarcos - 1) + " " +
                           std::to_string(marco.ancho) + "x" + std::to_string(marco.alto) +
                           " jugador " + std::to_string(e.jugadroActual) +
                           " | sprite: " + nombreDeRuta(fuenteSlp) +
                           " | paleta: " + infoPaleta;

            texto = "ORIGEN: REAL\n  sprite: " + nombreDeRuta(fuenteSlp) +
                    " marco " + std::to_string(e.marcoActual) + "/" +
                    std::to_string(totalMarcos - 1) + " (" + std::to_string(marco.ancho) +
                    "x" + std::to_string(marco.alto) + ")" +
                    "\n  paleta: " + infoPaleta;
        } else {
            e.spriteReal = false;
            e.spriteCausa.clear();
            for (const std::string& n : cand.notas) e.spriteCausa += (e.spriteCausa.empty() ? "" : "; ") + n;
            for (const std::string& f : fallosReales)
                e.spriteCausa += (e.spriteCausa.empty() ? "" : "; ") + f;

            // Reserva sintetica (mismo flujo de siempre).
            const std::string rutaSlp = e.dirGen + "/prueba.slp";
            const std::vector<uint8_t> bytesSlp = assets::cargarArchivo(rutaSlp);
            assets::DecodificadorSLP slp(bytesSlp.data(), bytesSlp.size(), rutaSlp);
            if (e.marcoActual >= slp.numMarcos()) e.marcoActual = 0;
            marco = slp.decodificarMarco(e.marcoActual);
            const int base = 16 * e.jugadroActual;
            componerSprite(marco, paletaUsada, base, e);
            totalMarcos = slp.numMarcos();
            e.spriteInfo = "marco " + std::to_string(e.marcoActual) + "/" +
                           std::to_string(slp.numMarcos() - 1) + " " +
                           std::to_string(marco.ancho) + "x" + std::to_string(marco.alto) +
                           " jugador " + std::to_string(e.jugadroActual) +
                           " | sprite: prueba.slp (sintetico) | paleta: " + infoPaleta;

            texto = "ORIGEN: SINTETICO (reserva)\n  causa: " +
                    (e.spriteCausa.empty() ? "(desconocida)" : e.spriteCausa) +
                    "\n  sprite: prueba.slp marco " + std::to_string(e.marcoActual);
        }

        // PNG de evidencia en dumps/ (el nombre dice el origen).
        std::vector<assets::RGB> pixeles;
        pixeles.reserve(estado_spriteTamano(e));
        for (int y = 0; y < e.spriteAlto; ++y) {
            for (int x = 0; x < e.spriteAncho; ++x) {
                const uint8_t* d = e.spriteRgba.data() +
                                   (static_cast<std::size_t>(y) * e.spriteAncho + x) * 4;
                assets::RGB c{d[0], d[1], d[2]};
                if (d[3] == 0) {
                    const int ajedrez = ((x / 4 + y / 4) % 2) ? 210 : 170;
                    c = {static_cast<uint8_t>(ajedrez), static_cast<uint8_t>(ajedrez),
                         static_cast<uint8_t>(ajedrez)};
                }
                pixeles.push_back(c);
            }
        }
        const std::string rutaPng = e.dirDumps + "/dump_" +
                                    (e.spriteReal ? std::string("real_") : std::string("sint_")) +
                                    "marco" + std::to_string(e.marcoActual) + "_p" +
                                    std::to_string(e.jugadroActual) + ".png";
        std::string errorPng;
        if (assets::escribirPngArchivo(rutaPng, e.spriteAncho, e.spriteAlto, pixeles, &errorPng)) {
            texto += "\n  PNG: " + nombreDeRuta(rutaPng);
        } else {
            texto += "\n  PNG fallo: " + errorPng;
        }
        for (const std::string& n : cand.notas) texto += "\n  nota: " + n;
        acumular(e, "DUMP SLP", texto);
        e.marcoActual = (e.marcoActual + 1) % std::max(1, totalMarcos > 0 ? totalMarcos : 1);
    } catch (const assets::ErrorDatos& err) {
        acumular(e, "DUMP SLP", "ERROR: " + err.mensaje);
        e.haySprite = false;
    }
}

// ---------------------------------------------------------------
// Botones: regiones grandes (>= 48 px, spec §7).
// ---------------------------------------------------------------
struct Boton {
    const char* etiqueta;
    int x, y, w, h;
};

// ---------------------------------------------------------------
// M2: mapa isometrico (spec §6 M2). El mapa vive en game/mapa; aqui
// solo se compone el cuadro, se mueve la camara y se deja constancia
// de todo en el overlay y el registro.
// ---------------------------------------------------------------
struct EstadoM2 {
    game::Mapa mapa;
    render::TamTile tam;
    render::Camara camara;
    render::LimitesCamara limites;
    render::StatsIso stats;
    std::string informe;     // compartible, como el de M1
    bool activo = false;     // pantalla de mapa abierta
    bool overlay = true;     // modo de depuracion obligatorio
    bool arrastrando = false;
    float ultimoX = 0.0f, ultimoY = 0.0f;
};

// Anade una seccion al informe M2 (con tope de memoria).
void acumularM2(EstadoM2& e, const std::string& titulo, const std::string& texto) {
    std::string seccion = "== " + titulo + " ==\n" + texto + "\n";
    if (e.informe.size() + seccion.size() > 24000) e.informe.clear();
    e.informe += seccion;
}

// Tamano de mosaico desde un empires.dat (real o sintetico): se INFLA y se
// lee la estructura; el valor 48x24 no se asume, se lee del archivo.
bool tamDesdeDat(const std::string& rutaDat, render::TamTile& tam, std::string* detalle) {
    try {
        const std::vector<uint8_t> bytes = assets::cargarArchivo(rutaDat);
        const std::vector<uint8_t> datos = assets::inflar(bytes.data(), bytes.size(),
                                                          64u * 1024 * 1024);
        const assets::ResumenDat dat = assets::leerDat(datos.data(), datos.size(), rutaDat);
        render::TamTile t;
        t.ancho = dat.tileAncho;
        t.alto = dat.tileAlto;
        t.medioAncho = dat.tileMediaAncho;
        t.medioAlto = dat.tileMediaAlto;
        t.origen = rutaDat;
        if (!t.valido()) {
            *detalle = "tile del dat invalido: " + std::to_string(t.ancho) + "x" +
                       std::to_string(t.alto) + " (mitad " + std::to_string(t.medioAncho) + "x" +
                       std::to_string(t.medioAlto) + ")";
            return false;
        }
        tam = t;
        char buf[192];
        snprintf(buf, sizeof(buf),
                 "tile leido del dat: %dx%d (mitad %dx%d, elevacion %d)", dat.tileAncho,
                 dat.tileAlto, dat.tileMediaAncho, dat.tileMediaAlto, dat.alturaElevacion);
        *detalle = buf;
        return true;
    } catch (const assets::ErrorDatos& err) {
        *detalle = std::string("fallo al leer el dat: ") + err.mensaje;
        return false;
    }
}

// Abre el modo mapa: genera el mapa de prueba, resuelve el tamano de mosaico
// (dat forzado -> dat real importado -> dat sintetico -> valores documentados,
// dejando la causa en el registro) y centra la camara.
void accionMapa(EstadoM2& e2, const EstadoM1& dirs, const std::string& datForzado = "") {
    e2.mapa = game::generarMapaPrueba(64, 64);
    const game::ConteoTerreno conteo = game::contarTerrenos(e2.mapa);
    std::string texto = "mapa de prueba " + std::to_string(e2.mapa.ancho) + "x" +
                        std::to_string(e2.mapa.alto) + " (" +
                        std::to_string(e2.mapa.terreno.size()) + " tiles)\n" +
                        "  terreno: " + game::resumenTerrenos(conteo) + "\n";

    std::string detalle;
    bool okTam = false;
    // 0) dat forzado (ruta de prueba del CI): DEBE funcionar.
    if (!datForzado.empty()) {
        if (tamDesdeDat(datForzado, e2.tam, &detalle)) {
            okTam = true;
            e2.tam.origen += " (sintetico)";
        }
        texto += "  " + detalle + "\n";
    }
    // 1) dat real importado por el usuario (extension case-insensitive).
    if (!okTam && datForzado.empty()) {
        for (const std::string& ruta : assets::listarArchivos(dirs.dirImport)) {
            if (terminaEnCi(ruta, ".dat")) {
                if (tamDesdeDat(ruta, e2.tam, &detalle)) {
                    okTam = true;
                    break;
                }
                texto += "  " + detalle + "\n";
            }
        }
    }
    // 2) dat sintetico del generador (misma estructura ROR).
    if (!okTam && datForzado.empty()) {
        const std::string sintetico = dirs.dirGen + "/empires_test.dat";
        FILE* f = fopen(sintetico.c_str(), "rb");
        if (f) {
            fclose(f);
            if (tamDesdeDat(sintetico, e2.tam, &detalle)) {
                okTam = true;
                e2.tam.origen += " (sintetico)";
            } else {
                texto += "  " + detalle + "\n";
            }
        }
    }
    // 3) Reserva documentada solo cuando no se exigio dat (con --dat, un
    // fallo queda como tile INVALIDO: el overlay y el guardián lo reportan).
    if (!okTam && datForzado.empty()) {
        e2.tam = render::TamTile{};  // 48x24 documentado (spec de formatos)
        e2.tam.origen = "valores documentados (sin dat disponible)";
        detalle = "sin dat util; usando 48x24 documentado";
    } else if (!okTam) {
        e2.tam = render::TamTile{0, 0, 0, 0, "dat forzado no legible: " + detalle};
        detalle = "dat forzado no legible: " + detalle;
    }
    texto += "  " + detalle + "\n";

    e2.limites = render::limitesCamara(e2.mapa.ancho, e2.mapa.alto, e2.tam, FB_W, FB_H);
    // Camara inicial: centro del mapa en el centro de pantalla.
    const int cx = e2.mapa.ancho / 2, cy = e2.mapa.alto / 2;
    e2.camara.x = static_cast<float>(FB_W / 2 - (cx - cy) * e2.tam.medioAncho);
    e2.camara.y = static_cast<float>(FB_H / 2 - (cx + cy) * e2.tam.medioAlto);
    render::ajustarCamara(e2.limites, e2.camara);
    char buf[128];
    snprintf(buf, sizeof(buf), "  camara inicial: x=%.0f y=%.0f", e2.camara.x, e2.camara.y);
    texto += buf;

    acumularM2(e2, "MAPA (M2)", texto);
    platform::Log::write("MODO MAPA: %s", texto.c_str());
    e2.activo = true;
}

// Exporta el cuadro actual como PNG (evidencia, spec §6 M2).
bool exportarPng(const render::Framebuffer& fb, const std::string& ruta, std::string* error) {
    std::vector<assets::RGB> pixeles;
    pixeles.reserve(static_cast<std::size_t>(fb.width()) * fb.height());
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            const uint32_t p = fb.pixels()[static_cast<std::size_t>(y) * fb.width() + x];
            pixeles.push_back(assets::RGB{static_cast<uint8_t>((p >> 16) & 0xFF),
                                          static_cast<uint8_t>((p >> 8) & 0xFF),
                                          static_cast<uint8_t>(p & 0xFF)});
        }
    }
    return assets::escribirPngArchivo(ruta, fb.width(), fb.height(), pixeles, error);
}

// Botones de la pantalla de mapa (arriba a la derecha).
const Boton kBotonVolver{"VOLVER", FB_W - 148, 12, 136, 48};
const Boton kBotonDebug{"DEBUG ON/OFF", FB_W - 148, 68, 136, 48};

bool enBotonB(const Boton& b, float x, float y) {
    return x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h;
}

// Compone el cuadro completo del modo mapa: terreno isometrico + overlay de
// depuracion obligatorio + botones. Nunca deja negro sin explicacion: si
// nada se dibujo, la causa queda en pantalla (y el llamador decide si es
// fatal).
void componerFrameMapa(render::Framebuffer& fb, EstadoM2& e2, bool dibujarMosaicos = true) {
    fb.clear(0x08, 0x12, 0x18);

    render::dibujarTerreno(fb, e2.mapa, e2.tam, e2.camara, e2.stats, dibujarMosaicos);

    if (e2.stats.dibujados == 0) {
        // Guarda anti-pantalla-negra (spec §6 M2): si no se dibujo NADA, la
        // pantalla no se queda callada: banner con la causa.
        fb.fillRect(0, FB_H / 2 - 44, FB_W, 88, 0x5A, 0x14, 0x14);
        fb.drawText(24, FB_H / 2 - 24, "ERROR: 0 MOSAICOS DIBUJADOS", 0xFF, 0xE8, 0xE8, 2);
        fb.drawText(24, FB_H / 2 + 4,
                    "CAUSA: " + (e2.stats.ultimaCausa.empty() ? "(sin detalle)" : e2.stats.ultimaCausa),
                    0xFF, 0xD0, 0xD0, 1);
    }

    if (e2.overlay) {
        // Overlay de depuracion (spec §6 M2): mapa en memoria, mosaicos,
        // camara. Todo lo que el motor sabe, en pantalla.
        fb.fillRect(12, 12, 470, 118, 0x00, 0x00, 0x00);
        char l[128];
        int y = 20;
        snprintf(l, sizeof(l), "MAPA EN MEMORIA: %dx%d (%zu tiles)", e2.mapa.ancho,
                 e2.mapa.alto, e2.mapa.terreno.size());
        fb.drawText(20, y, l, 0xF2, 0xF2, 0xE6, 1); y += 14;
        const game::ConteoTerreno c = game::contarTerrenos(e2.mapa);
        fb.drawText(20, y, "TERRENO: " + game::resumenTerrenos(c), 0xC8, 0xE6, 0xC8, 1); y += 14;
        snprintf(l, sizeof(l), "TILE: %dx%d (mitad %dx%d) | %s", e2.tam.ancho,
                 e2.tam.alto, e2.tam.medioAncho, e2.tam.medioAlto, e2.tam.origen.c_str());
        const std::string lineaTile = l;  // recorta la ruta al nombre de archivo
        const std::size_t barra = lineaTile.find_last_of('/');
        fb.drawText(20, y, barra == std::string::npos
                               ? lineaTile
                               : lineaTile.substr(0, lineaTile.find(" | ") + 3) +
                                     lineaTile.substr(barra + 1),
                    0xF0, 0xC9, 0x4A, 1);
        y += 14;
        snprintf(l, sizeof(l), "CAMARA: x=%.0f y=%.0f", e2.camara.x, e2.camara.y);
        fb.drawText(20, y, l, 0x9F, 0xB3, 0xB8, 1); y += 14;
        snprintf(l, sizeof(l), "MOSAICOS: intentados=%d dibujados=%d fuera=%d fallidos=%d",
                 e2.stats.intentados, e2.stats.dibujados, e2.stats.fueraDeVista,
                 e2.stats.fallidos);
        fb.drawText(20, y, l, 0x8F, 0xD1, 0x9F, 1); y += 14;
        if (!e2.stats.ultimaCausa.empty()) {
            fb.drawText(20, y, "CAUSA: " + e2.stats.ultimaCausa, 0xFF, 0xA0, 0xA0, 1);
        }
    }

    // Botones de la vista de mapa.
    fb.fillRect(kBotonVolver.x, kBotonVolver.y, kBotonVolver.w, kBotonVolver.h, 0x1D, 0x4E, 0x50);
    fb.drawText(kBotonVolver.x + 24, kBotonVolver.y + 16, kBotonVolver.etiqueta, 0xF2, 0xF2, 0xE6, 2);
    fb.fillRect(kBotonDebug.x, kBotonDebug.y, kBotonDebug.w, kBotonDebug.h, 0x3E, 0x2E, 0x50);
    fb.drawText(kBotonDebug.x + 12, kBotonDebug.y + 16, kBotonDebug.etiqueta, 0xF2, 0xF2, 0xE6, 2);
}

// Demo M2 para CI (spec §6 M2: "imagen PNG generada en integracion continua
// con datos sinteticos"): mapa + camara que recorre una trayectoria que
// fuerza los limites, overlay activo, PNG + BMP de evidencia y verificacion
// anti-pantalla-negra. AOE_M2_TEST_NEGRO=1 suprime el dibujo para probar que
// la guarda falla con error (prueba negativa del propio guardián).
bool correrDemoM2(const Opciones& op, render::Framebuffer& fb, EstadoM2& e2, EstadoM1& dirs) {
    accionMapa(e2, dirs, op.datM2);
    if (!op.datM2.empty() && !e2.tam.origen.empty() &&
        e2.tam.origen.find("(sintetico)") == std::string::npos) {
        platform::Log::write("FATAL: --dat %s no se pudo usar", op.datM2.c_str());
        return false;
    }

    bool dibujar = getenv("AOE_M2_TEST_NEGRO") == nullptr;
    if (!dibujar) {
        platform::Log::write("M2: AOE_M2_TEST_NEGRO=1 -> dibujo suprimido (prueba de la guarda)");
    }

    const int nCuadros = op.cuadros > 0 ? op.cuadros : 60;
    int ajustesCamara = 0;
    for (int f = 0; f < nCuadros; ++f) {
        platform::Metrics::beginFrame();
        // Trayectoria: barrido sinusoidal amplio, deliberadamente mas alla de
        // los limites, para ejercitar el ajuste de camara.
        const double t = 6.28318530718 * static_cast<double>(f) / nCuadros;
        const float antesX = e2.camara.x, antesY = e2.camara.y;
        e2.camara.x += static_cast<float>(std::cos(t) * 90.0);
        e2.camara.y += static_cast<float>(std::sin(t * 2.0) * 60.0);
        render::ajustarCamara(e2.limites, e2.camara);
        if (e2.camara.x != antesX || e2.camara.y != antesY) ++ajustesCamara;

        componerFrameMapa(fb, e2, dibujar);
        if (!fb.present()) {
            platform::Log::write("FATAL: present en demo M2: %s", SDL_GetError());
            return false;
        }
        platform::Metrics::endFrame();

        if (f % (nCuadros / 4 == 0 ? 1 : nCuadros / 4) == 0) {
            platform::Log::write(
                "M2 cuadro %d/%d: camara x=%.0f y=%.0f | intentados=%d dibujados=%d "
                "fuera=%d fallidos=%d",
                f, nCuadros, e2.camara.x, e2.camara.y, e2.stats.intentados,
                e2.stats.dibujados, e2.stats.fueraDeVista, e2.stats.fallidos);
        }
    }
    platform::Log::write("M2: ajustes de camara por limites: %d (clamp activo)", ajustesCamara);

    // Verificacion anti-pantalla-negra sobre el propio bufer.
    const uint32_t pc = fb.pixels()[static_cast<std::size_t>(FB_H / 2) * FB_W + FB_W / 2];
    const int cr = static_cast<int>((pc >> 16) & 0xFF), cg = static_cast<int>((pc >> 8) & 0xFF),
              cb = static_cast<int>(pc & 0xFF);
    platform::Log::write("M2: pixel central RGB(%d,%d,%d) | fondo de referencia RGB(8,18,24)",
                         cr, cg, cb);

    // Evidencia: PNG (obligatorio) + BMP (captura pedida).
    const std::string png = op.captura + ".png";
    std::string errorPng;
    bool ok = exportarPng(fb, png, &errorPng);
    platform::Log::write("M2: evidencia PNG %s (%s)", png.c_str(), ok ? "ok" : errorPng.c_str());
    if (!ok) return false;
    if (!op.captura.empty()) {
        const bool okBmp = fb.saveBmp(op.captura);
        platform::Log::write("M2: captura BMP %s (%s)", op.captura.c_str(),
                             okBmp ? "ok" : "FALLO");
        ok = ok && okBmp;
    }

    // Veredicto: sin mosaicos dibujados la demo falla (guarda anti-negro).
    if (e2.stats.dibujados == 0) {
        platform::Log::write(
            "FATAL: 0 mosaicos dibujados tras %d cuadros | causa: %s", nCuadros,
            e2.stats.ultimaCausa.c_str());
        return false;
    }
    if (cr == 8 && cg == 18 && cb == 24) {
        platform::Log::write("FATAL: el pixel central es el color de fondo (pantalla negra)");
        return false;
    }
    platform::Log::write("M2: veredicto OK — mapa %dx%d, tile %dx%d desde %s, %d mosaicos en pantalla",
                         e2.mapa.ancho, e2.mapa.alto, e2.tam.ancho, e2.tam.alto,
                         e2.tam.origen.c_str(), e2.stats.dibujados);
    return ok;
}

// Dos filas abajo: generacion/chequeo/importacion y dump/compartir/mapa.
const Boton kBotones[6] = {
    {"GENERAR", 16, FB_H - 160, 288, 64},
    {"ASSETCHECK", 316, FB_H - 160, 288, 64},
    {"IMPORTAR", 616, FB_H - 160, 288, 64},
    {"DUMP SLP", 16, FB_H - 84, 288, 64},
    {"COMPARTIR", 316, FB_H - 84, 288, 64},
    {"MAPA (M2)", 616, FB_H - 84, 288, 64},
};

bool enBoton(int indice, float x, float y) {
    const Boton& b = kBotones[indice];
    return x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h;
}

} // namespace

int main(int argc, char* argv[]) {
    const Opciones op = parseArgs(argc, argv);

    const std::string rutaLog = rutaDeLog(op);
    platform::Log::init(rutaLog);
    platform::Log::write("== %s ==", AOE_VERSION_STRING);
#ifdef __ANDROID__
    platform::Log::write("SDL %s | plataforma: android", versionSdl().c_str());
#else
    platform::Log::write("SDL %s | plataforma: escritorio", versionSdl().c_str());
#endif
    if (!rutaLog.empty()) {
        platform::Log::write("archivo de registro: %s (%s)", rutaLog.c_str(),
                             platform::Log::fileOk() ? "ok" : "NO ESCRIBIBLE");
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
        platform::Log::write("FATAL: SDL_Init: %s", SDL_GetError());
        fprintf(stderr, "FATAL: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    const Uint32 flagsVentana = SDL_WINDOW_ALLOW_HIGHDPI | (op.selftest ? 0 : SDL_WINDOW_RESIZABLE);
    SDL_Window* ventana = SDL_CreateWindow(AOE_VERSION_STRING, SDL_WINDOWPOS_CENTERED,
                                           SDL_WINDOWPOS_CENTERED, FB_W, FB_H, flagsVentana);
    if (!ventana) {
        platform::Log::write("FATAL: SDL_CreateWindow: %s", SDL_GetError());
        fprintf(stderr, "FATAL: SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Renderer* renderer =
        SDL_CreateRenderer(ventana, -1, SDL_RENDERER_SOFTWARE | (op.selftest ? 0 : SDL_RENDERER_PRESENTVSYNC));
    if (!renderer) {
        platform::Log::write("FATAL: SDL_CreateRenderer: %s", SDL_GetError());
        fprintf(stderr, "FATAL: SDL_CreateRenderer: %s\n", SDL_GetError());
        return 1;
    }

    render::Framebuffer fb;
    if (!fb.init(renderer, FB_W, FB_H)) {
        platform::Log::write("FATAL: framebuffer: %s", SDL_GetError());
        fprintf(stderr, "FATAL: framebuffer: %s\n", SDL_GetError());
        return 1;
    }
    platform::Log::write("marco de dibujo %dx%d listo (composicion por software)", FB_W, FB_H);

    // Directorios M1.
    EstadoM1 estado;
#ifdef __ANDROID__
    const Uint32 estadoExt = SDL_AndroidGetExternalStorageState();
    if (estadoExt & SDL_ANDROID_EXTERNAL_STORAGE_WRITE) {
        const char* ext = SDL_AndroidGetExternalStoragePath();
        if (ext) estado.dirBase = ext;
    }
    if (estado.dirBase.empty()) estado.dirBase = "/sdcard/Android/data/com.aoemobile.engine/files";
#else
    {
        char* pref = SDL_GetPrefPath("aoe-mobile", "m1");
        if (pref) {
            estado.dirBase = pref;
            SDL_free(pref);
        }
    }
#endif
    while (!estado.dirBase.empty() && estado.dirBase.back() == '/') estado.dirBase.pop_back();
    estado.dirGen = estado.dirBase + "/gen";
    estado.dirImport = estado.dirBase + "/import";
    estado.dirDumps = estado.dirBase + "/dumps";
    crearDir(estado.dirGen);
    crearDir(estado.dirImport);
    crearDir(estado.dirDumps);
    platform::Log::write("datos: gen=%s import=%s dumps=%s", estado.dirGen.c_str(),
                         estado.dirImport.c_str(), estado.dirDumps.c_str());

    // Estado del modo mapa M2.
    EstadoM2 e2;

    // Demo M1 para autoprueba en CI: ejercita el flujo completo sin interaccion.
    if (op.demoM1) {
        accionGenerar(estado);
        accionAssetcheck(estado);
        accionDump(estado);
        accionDump(estado);  // segundo marco: comprueba el ciclo
    }

    // Demo M2 para CI: mapa isometrico + camara + PNG de evidencia. Termina
    // aqui mismo: su veredicto es el codigo de salida del proceso.
    if (op.demoM2) {
        const bool okM2 = correrDemoM2(op, fb, e2, estado);
        platform::Log::write("fin demo M2: %s | fps=%.1f | ram=%.1f MB",
                             okM2 ? "ok" : "FALLO", platform::Metrics::fps(),
                             platform::Metrics::ramKb() / 1024.0);
        SDL_Quit();
        return okM2 ? 0 : 1;
    }

    bool salir = false;
    int cuadros = 0;
    std::string ultimoEvento = "(ninguno)";

    auto manejarToqueMapa = [&](float x, float y, bool inicio) {
        if (inicio) {
            if (enBotonB(kBotonVolver, x, y)) {
                e2.activo = false;
                e2.arrastrando = false;
                platform::Log::write("MAPA: volver a herramientas");
            } else if (enBotonB(kBotonDebug, x, y)) {
                e2.overlay = !e2.overlay;
                platform::Log::write("MAPA: overlay de depuracion %s",
                                     e2.overlay ? "ON" : "OFF");
            } else {
                e2.arrastrando = true;
                e2.ultimoX = x;
                e2.ultimoY = y;
            }
        } else if (e2.arrastrando) {
            const float antesX = e2.camara.x, antesY = e2.camara.y;
            e2.camara.x += x - e2.ultimoX;
            e2.camara.y += y - e2.ultimoY;
            render::ajustarCamara(e2.limites, e2.camara);
            e2.ultimoX = x;
            e2.ultimoY = y;
            if (e2.camara.x != antesX || e2.camara.y != antesY) {
                char buf[96];
                snprintf(buf, sizeof(buf), "camara x=%.0f y=%.0f", e2.camara.x, e2.camara.y);
                ultimoEvento = buf;
            }
        }
    };

    while (!salir) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                salir = true;
                platform::Log::write("evento: SDL_QUIT");
            } else if (ev.type == SDL_FINGERDOWN || ev.type == SDL_FINGERUP ||
                       ev.type == SDL_FINGERMOTION) {
                const float x = ev.tfinger.x * FB_W, y = ev.tfinger.y * FB_H;
                char buf[96];
                snprintf(buf, sizeof(buf), "dedo (%.0f, %.0f)", x, y);
                ultimoEvento = buf;
                if (e2.activo) {
                    if (ev.type == SDL_FINGERDOWN) manejarToqueMapa(x, y, true);
                    else if (ev.type == SDL_FINGERMOTION) manejarToqueMapa(x, y, false);
                    else e2.arrastrando = false;
                    continue;
                }
                if (ev.type == SDL_FINGERDOWN) {
                    if (enBoton(0, x, y)) accionGenerar(estado);
                    else if (enBoton(1, x, y)) accionAssetcheck(estado);
                    else if (enBoton(2, x, y)) {
#ifdef __ANDROID__
                        if (pedirImportacion()) {
                            platform::Log::write("IMPORTAR: selector abierto");
                        } else {
                            platform::Log::write("IMPORTAR: fallo al abrir el selector");
                        }
#else
                        estado.informe = "IMPORTAR: solo disponible en Android";
                        platform::Log::write("%s", estado.informe.c_str());
#endif
                    } else if (enBoton(3, x, y)) accionDump(estado);
                    else if (enBoton(4, x, y)) {
                        std::string texto = std::string(AOE_VERSION_STRING) + " | SDL " +
                                            versionSdl() + "\n" + estado.informe;
                        if (!e2.informe.empty()) texto += "\n" + e2.informe;
                        texto += "\nLOG:\n";
                        for (const std::string& l : platform::Log::recentLines(30)) texto += l + "\n";
#ifdef __ANDROID__
                        const bool okShare = compartirTexto(texto);
                        platform::Log::write("COMPARTIR: %s",
                                             okShare ? "intent lanzado" : "fallo (sin intent)");
#endif
                    } else if (enBoton(5, x, y)) {
                        accionMapa(e2, estado);
                    }
                }
            } else if (ev.type == SDL_MOUSEBUTTONDOWN) {
                char buf[96];
                snprintf(buf, sizeof(buf), "clic (%d, %d)", ev.button.x, ev.button.y);
                ultimoEvento = buf;
                const float x = static_cast<float>(ev.button.x), y = static_cast<float>(ev.button.y);
                if (e2.activo) {
                    manejarToqueMapa(x, y, true);
                } else if (enBoton(0, x, y)) accionGenerar(estado);
                else if (enBoton(1, x, y)) accionAssetcheck(estado);
                else if (enBoton(3, x, y)) accionDump(estado);
                else if (enBoton(5, x, y)) accionMapa(e2, estado);
            } else if (ev.type == SDL_MOUSEBUTTONUP) {
                if (e2.activo) manejarToqueMapa(0, 0, false);  // corta el arrastre
            } else if (ev.type == SDL_MOUSEMOTION) {
                if (e2.activo && (ev.motion.state & SDL_BUTTON_LMASK)) {
                    manejarToqueMapa(static_cast<float>(ev.motion.x),
                                     static_cast<float>(ev.motion.y), false);
                }
            } else if (ev.type == SDL_KEYDOWN) {
                // Flechas para escritorio (en el telefono se arrastra con el dedo).
                const float paso = 48.0f;
                switch (ev.key.keysym.sym) {
                    case SDLK_LEFT:  e2.camara.x += paso; render::ajustarCamara(e2.limites, e2.camara); break;
                    case SDLK_RIGHT: e2.camara.x -= paso; render::ajustarCamara(e2.limites, e2.camara); break;
                    case SDLK_UP:    e2.camara.y += paso; render::ajustarCamara(e2.limites, e2.camara); break;
                    case SDLK_DOWN:  e2.camara.y -= paso; render::ajustarCamara(e2.limites, e2.camara); break;
                    default: break;
                }
            }
        }

#ifdef __ANDROID__
        if (g_importacionTerminada) {
            g_importacionTerminada = false;
            accionAssetcheck(estado);  // re-chequeo automatico tras importar
        }
#endif

        platform::Metrics::beginFrame();

        if (e2.activo) {
            // Vista de mapa: terreno + overlay de depuracion + botones propios.
            componerFrameMapa(fb, e2);
            if (!fb.present()) {
                platform::Log::write("FATAL: present: %s", SDL_GetError());
                fprintf(stderr, "FATAL: present: %s\n", SDL_GetError());
                return 1;
            }
            platform::Metrics::endFrame();
            ++cuadros;
            if (op.selftest && cuadros >= op.cuadros) salir = true;
            continue;
        }

        // Composicion por software (herramientas M1).
        fb.clear(0x11, 0x2C, 0x33);
        fb.fillRect(0, 0, FB_W, 6, 0x3D, 0x8B, 0x7A);

        int y = 24;
        fb.drawText(16, y, AOE_VERSION_STRING, 0xF2, 0xF2, 0xE6, 2);
        y += 24;
        fb.drawText(16, y,
                    "SDL " + versionSdl() + " | " AOE_MILESTONE
                    " | lectura DRS/SLP/paleta/dat | FPS " +
                        std::to_string(static_cast<int>(platform::Metrics::fps())) +
                        " | RAM " + std::to_string(platform::Metrics::ramKb() / 1024) + " MB",
                    0x9F, 0xB3, 0xB8, 1);
        y += 16;

        // Sprite decodificado (izquierda) e informe (derecha).
        if (estado.haySprite) {
            // Letrero del origen: REAL (verde) o SINTETICO (ambar). Nunca ambiguo.
            if (estado.spriteReal) {
                fb.drawText(24, y, "ORIGEN: REAL", 0x7F, 0xE0, 0x8F, 2);
            } else {
                fb.drawText(24, y, "ORIGEN: SINTETICO (reserva)", 0xF0, 0xC9, 0x4A, 2);
            }
            fb.drawText(24, y + 22,
                        ("SPRITE: " + estado.spriteInfo).substr(0, 56),
                        0xF0, 0xC9, 0x4A, 1);
            const int cajaX = 24, cajaY = y + 36, cajaW = 452, cajaH = 262;
            fb.fillRect(cajaX, cajaY, cajaW, cajaH, 0x0A, 0x1A, 0x20);
            // Escala: x4 si cabe, luego x2, x1; si aun asi no cabe, decima.
            int escala = 4;
            while (escala > 1 && (estado.spriteAncho * escala > cajaW ||
                                  estado.spriteAlto * escala > cajaH)) --escala;
            int decimar = 1;
            while ((estado.spriteAncho + decimar - 1) / decimar > cajaW ||
                   (estado.spriteAlto + decimar - 1) / decimar > cajaH) ++decimar;
            if (decimar == 1) {
                fb.blitRgba(cajaX + (cajaW - estado.spriteAncho * escala) / 2,
                            cajaY + (cajaH - estado.spriteAlto * escala) / 2,
                            estado.spriteAncho, estado.spriteAlto, estado.spriteRgba.data(),
                            escala);
            } else {
                // Decimacion vecino-mas-cercano para sprites enormes reales.
                const int wVis = (estado.spriteAncho + decimar - 1) / decimar;
                const int hVis = (estado.spriteAlto + decimar - 1) / decimar;
                std::vector<uint8_t> chico(static_cast<std::size_t>(wVis) * hVis * 4, 0);
                for (int vy = 0; vy < hVis; ++vy) {
                    for (int vx = 0; vx < wVis; ++vx) {
                        const uint8_t* o = estado.spriteRgba.data() +
                            (static_cast<std::size_t>(vy * decimar) * estado.spriteAncho +
                             vx * decimar) * 4;
                        uint8_t* d = chico.data() +
                            (static_cast<std::size_t>(vy) * wVis + vx) * 4;
                        d[0] = o[0]; d[1] = o[1]; d[2] = o[2]; d[3] = o[3];
                    }
                }
                fb.blitRgba(cajaX + (cajaW - wVis) / 2, cajaY + (cajaH - hVis) / 2,
                            wVis, hVis, chico.data(), 1);
            }
        }
        {
            // Informe a la derecha: lineas ENVUELTAS (nunca recortadas) y con
            // color segun su tipo, para que la causa completa se lea entera.
            const int lx = 488;
            const int anchoLinea = 58;  // 58 chars x 8 px = 464 < 472 px utiles
            int ly = y;
            fb.drawText(lx, ly, "INFORME:", 0x6C, 0xC5, 0xC7, 1);
            ly += 14;
            std::vector<std::string> lineas;
            {
                std::string linea;
                auto cortar = [&](const std::string& s) {
                    if (s.size() <= static_cast<std::size_t>(anchoLinea)) {
                        lineas.push_back(s);
                        return;
                    }
                    std::size_t i = 0;
                    while (i < s.size()) {
                        std::size_t fin = std::min(static_cast<std::size_t>(anchoLinea),
                                                   s.size() - i);
                        if (i + fin < s.size()) {
                            const std::size_t esp = s.rfind(' ', i + fin);
                            if (esp != std::string::npos &&
                                esp > i + static_cast<std::size_t>(anchoLinea) / 2) {
                                fin = esp - i;
                            }
                        }
                        lineas.push_back(s.substr(i, fin));
                        i += fin;
                        while (i < s.size() && s[i] == ' ') ++i;
                    }
                };
                for (char ch : estado.informe) {
                    if (ch == '\n') {
                        cortar(linea);
                        linea.clear();
                    } else {
                        linea += ch;
                    }
                }
                if (!linea.empty()) cortar(linea);
            }
            const int maxLineas = 24;
            const int inicio = lineas.size() > static_cast<std::size_t>(maxLineas)
                                   ? static_cast<int>(lineas.size()) - maxLineas
                                   : 0;
            for (int i = inicio; i < static_cast<int>(lineas.size()); ++i) {
                const std::string& l = lineas[static_cast<std::size_t>(i)];
                uint8_t cr = 0xF2, cg = 0xF2, cb = 0xE6;
                if (l.rfind("==", 0) == 0) {
                    cr = 0x6C; cg = 0xC5; cb = 0xC7;
                } else if (l.find("ERROR:") != std::string::npos ||
                           l.find("ERROR DEL VALIDADOR") != std::string::npos) {
                    cr = 0xFF; cg = 0x8A; cb = 0x8A;
                } else if (l.rfind("  causa:", 0) == 0 || l.rfind("causa:", 0) == 0) {
                    cr = 0xFF; cg = 0xB4; cb = 0xB4;
                } else if (l.rfind("  ", 0) == 0) {
                    cr = 0x9F; cg = 0xB3; cb = 0xB8;
                } else if (l.rfind("CORRUPTO OK", 0) == 0) {
                    cr = 0xA8; cg = 0xD8; cb = 0xA8;
                } else if (l.rfind("OK:", 0) == 0) {
                    cr = 0xCF; cg = 0xEA; cb = 0xCF;
                } else if (l.rfind("ASSETCHECK", 0) == 0 || l.rfind("ORIGEN:", 0) == 0) {
                    cr = 0xF0; cg = 0xC9; cb = 0x4A;
                }
                fb.drawText(lx, ly, l, cr, cg, cb, 1);
                ly += 12;
                if (ly > FB_H - 170) break;
            }
        }

        // Botones.
        for (int i = 0; i < 6; ++i) {
            const Boton& b = kBotones[i];
            fb.fillRect(b.x, b.y, b.w, b.h, 0x1D, 0x4E, 0x50);
            fb.fillRect(b.x, b.y, b.w, 3, 0x3D, 0x8B, 0x7A);
            fb.drawText(b.x + 16, b.y + b.h / 2 - 8, b.etiqueta, 0xF2, 0xF2, 0xE6, 2);
        }

        fb.drawText(16, FB_H - 172, "ULTIMO EVENTO: " + ultimoEvento, 0x8F, 0xD1, 0x9F, 1);

        if (!fb.present()) {
            platform::Log::write("FATAL: present: %s", SDL_GetError());
            fprintf(stderr, "FATAL: present: %s\n", SDL_GetError());
            return 1;
        }

        platform::Metrics::endFrame();
        ++cuadros;
        if (op.selftest && cuadros >= op.cuadros) salir = true;
    }

    bool ok = true;
    if (op.selftest) {
        ok = fb.saveBmp(op.captura);
        platform::Log::write("autoprueba: cuadros=%d captura=%s (%s)", cuadros, op.captura.c_str(),
                             ok ? "ok" : "FALLO");
    }
    platform::Log::write("fin: %d cuadros | fps=%.1f | ram=%.1f MB", cuadros,
                         platform::Metrics::fps(), platform::Metrics::ramKb() / 1024.0);
    SDL_Quit();
    return ok ? 0 : 1;
}
