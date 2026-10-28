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
#include "assets/paleta.h"
#include "assets/png.h"
#include "assets/slp.h"
#include "platform/log.h"
#include "platform/metrics.h"
#include "render/framebuffer.h"
#include "version.h"

namespace {

constexpr int FB_W = 960;
constexpr int FB_H = 540;

struct Opciones {
    bool selftest = false;               // N cuadros sin ventana -> captura + registro
    bool demoM1 = false;                 // genera, chequea y decodifica antes del bucle
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
    acumular(e, "ASSETCHECK",
             "gen/ + import/: " + std::to_string(r.ok) + " OK, " +
             std::to_string(r.fallidos) + " FALLIDOS\n" + r.informe);
    platform::Log::write("ASSETCHECK: %d OK, %d FALLIDOS", r.ok, r.fallidos);
    // El informe completo queda en archivo para que el usuario lo comparta.
    FILE* f = fopen((e.dirBase + "/m1_assetcheck.txt").c_str(), "wb");
    if (f) {
        fwrite(r.informe.data(), 1, r.informe.size(), f);
        fclose(f);
    }
}

// Busca un SLP y una paleta: primero los archivos reales importados, luego
// los sinteticos generados. Devuelve las rutas encontradas o vacio.
struct ParSprite {
    std::string slp, pal, origen;
};

ParSprite buscarSpriteYPaleta(const EstadoM1& e) {
    ParSprite p;
    // SLP suelto importado.
    for (const std::string& ruta : assets::listarArchivos(e.dirImport)) {
        if (ruta.size() > 4 && ruta.compare(ruta.size() - 4, 4, ".slp") == 0 && p.slp.empty()) {
            p.slp = ruta;
            p.origen = "import";
        }
    }
    // Paleta suelta importada.
    for (const std::string& ruta : assets::listarArchivos(e.dirImport)) {
        if (ruta.size() > 4 && ruta.compare(ruta.size() - 4, 4, ".pal") == 0 && p.pal.empty()) {
            p.pal = ruta;
        }
    }
    // DRS importado: primer SLP y primera paleta (bina 50500) que contenga.
    for (const std::string& ruta : assets::listarArchivos(e.dirImport)) {
        if (p.slp.empty() || p.pal.empty()) {
            try {
                const std::vector<uint8_t> bytes = assets::cargarArchivo(ruta);
                if (assets::detectarTipo(ruta, bytes) != assets::TipoArchivo::Drs) continue;
                assets::LectorDRS drs(bytes.data(), bytes.size(), ruta);
                if (p.slp.empty()) {
                    for (const assets::EntradaDRS& en : drs.info().archivos) {
                        if (en.extension == "slp") {
                            p.slp = ruta + "::" + std::to_string(en.id) + ".slp";
                            p.origen = ruta;
                            break;
                        }
                    }
                }
                if (p.pal.empty()) {
                    for (const assets::EntradaDRS& en : drs.info().archivos) {
                        if (en.extension == "bina" && en.id >= 50500 && en.id < 50600) {
                            p.pal = ruta + "::" + std::to_string(en.id) + ".bina";
                            break;
                        }
                    }
                }
            } catch (const assets::ErrorDatos&) {
                continue;  // se reporta en ASSETCHECK; aqui se ignora
            }
        }
    }
    // Sinteticos de reserva.
    if (p.slp.empty()) {
        p.slp = e.dirGen + "/prueba.slp";
        p.origen = "sintetico";
    }
    if (p.pal.empty()) p.pal = e.dirGen + "/prueba.pal";
    return p;
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
        const ParSprite par = buscarSpriteYPaleta(e);
        const std::vector<uint8_t> bytesSlp = cargarRecursoSlp(par.slp);
        assets::DecodificadorSLP slp(bytesSlp.data(), bytesSlp.size(), par.slp);
        if (e.marcoActual >= slp.numMarcos()) e.marcoActual = 0;
        const assets::MarcoSLP marco = slp.decodificarMarco(e.marcoActual);
        const std::vector<uint8_t> bytesPal = cargarRecursoPaleta(par.pal);
        const assets::Paleta pal = assets::leerPaleta(bytesPal.data(), bytesPal.size());
        const int base = 16 * e.jugadroActual;  // AoK/AoE1: 16 por jugador (verificar con dat real)
        componerSprite(marco, pal, base, e);
        e.spriteInfo = "marco " + std::to_string(e.marcoActual) + "/" +
                       std::to_string(slp.numMarcos() - 1) + " " +
                       std::to_string(marco.ancho) + "x" + std::to_string(marco.alto) +
                       " jugador " + std::to_string(e.jugadroActual) + " (" + par.origen + ")";
        // PNG de evidencia en dumps/.
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
        const std::string rutaPng = e.dirDumps + "/dump_marco" +
                                    std::to_string(e.marcoActual) + "_p" +
                                    std::to_string(e.jugadroActual) + ".png";
        std::string errorPng;
        if (assets::escribirPngArchivo(rutaPng, e.spriteAncho, e.spriteAlto, pixeles, &errorPng)) {
            acumular(e, "DUMP SLP", "OK: " + rutaPng);
        } else {
            acumular(e, "DUMP SLP", "sprite en pantalla; PNG fallo: " + errorPng);
        }
        e.marcoActual = (e.marcoActual + 1) % std::max(1, slp.numMarcos());
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

// Dos filas abajo: generacion/chequeo/importacion y dump/compartir.
const Boton kBotones[5] = {
    {"GENERAR", 16, FB_H - 160, 288, 64},
    {"ASSETCHECK", 316, FB_H - 160, 288, 64},
    {"IMPORTAR", 616, FB_H - 160, 288, 64},
    {"DUMP SLP", 16, FB_H - 84, 288, 64},
    {"COMPARTIR", 316, FB_H - 84, 288, 64},
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

    // Demo M1 para autoprueba en CI: ejercita el flujo completo sin interaccion.
    if (op.demoM1) {
        accionGenerar(estado);
        accionAssetcheck(estado);
        accionDump(estado);
        accionDump(estado);  // segundo marco: comprueba el ciclo
    }

    bool salir = false;
    int cuadros = 0;
    std::string ultimoEvento = "(ninguno)";

    while (!salir) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                salir = true;
                platform::Log::write("evento: SDL_QUIT");
            } else if (ev.type == SDL_FINGERDOWN || ev.type == SDL_FINGERUP ||
                       ev.type == SDL_FINGERMOTION) {
                char buf[96];
                snprintf(buf, sizeof(buf), "dedo (%.0f, %.0f)",
                         ev.tfinger.x * FB_W, ev.tfinger.y * FB_H);
                ultimoEvento = buf;
                if (ev.type == SDL_FINGERDOWN) {
                    const float x = ev.tfinger.x * FB_W, y = ev.tfinger.y * FB_H;
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
                                            versionSdl() + "\n" + estado.informe + "\nLOG:\n";
                        for (const std::string& l : platform::Log::recentLines(30)) texto += l + "\n";
#ifdef __ANDROID__
                        const bool okShare = compartirTexto(texto);
                        platform::Log::write("COMPARTIR: %s",
                                             okShare ? "intent lanzado" : "fallo (sin intent)");
#endif
                    }
                }
            } else if (ev.type == SDL_MOUSEBUTTONDOWN) {
                char buf[96];
                snprintf(buf, sizeof(buf), "clic (%d, %d)", ev.button.x, ev.button.y);
                ultimoEvento = buf;
                const float x = static_cast<float>(ev.button.x), y = static_cast<float>(ev.button.y);
                if (enBoton(0, x, y)) accionGenerar(estado);
                else if (enBoton(1, x, y)) accionAssetcheck(estado);
                else if (enBoton(3, x, y)) accionDump(estado);
            }
        }

#ifdef __ANDROID__
        if (g_importacionTerminada) {
            g_importacionTerminada = false;
            accionAssetcheck(estado);  // re-chequeo automatico tras importar
        }
#endif

        platform::Metrics::beginFrame();

        // Composicion por software.
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
            fb.drawText(24, y, "SPRITE: " + estado.spriteInfo, 0xF0, 0xC9, 0x4A, 1);
            // Escala x2 si cabe; si no, x1.
            int escala = 1;
            if (estado.spriteAncho * 2 * 2 <= 440 && estado.spriteAlto * 2 * 2 <= 290) escala = 2;
            const int wPx = estado.spriteAncho * escala * 2;
            const int hPx = estado.spriteAlto * escala * 2;
            fb.fillRect(24, y + 18, 452, 292, 0x0A, 0x1A, 0x20);
            fb.blitRgba(24 + (452 - wPx) / 2, y + 18 + (292 - hPx) / 2,
                        estado.spriteAncho, estado.spriteAlto, estado.spriteRgba.data(),
                        escala * 2);
        }
        {
            // Informe a la derecha (lineas recortadas a lo visible).
            const int lx = 488;
            int ly = y;
            fb.drawText(lx, ly, "INFORME:", 0x6C, 0xC5, 0xC7, 1);
            ly += 14;
            std::vector<std::string> lineas;
            std::string actual;
            for (char c : estado.informe) {
                if (c == '\n') {
                    lineas.push_back(actual);
                    actual.clear();
                } else {
                    actual += c;
                }
            }
            if (!actual.empty()) lineas.push_back(actual);
            const int maxLineas = 22;
            const int inicio = lineas.size() > static_cast<std::size_t>(maxLineas)
                                   ? static_cast<int>(lineas.size()) - maxLineas
                                   : 0;
            for (int i = inicio; i < static_cast<int>(lineas.size()); ++i) {
                fb.drawText(lx, ly, lineas[static_cast<std::size_t>(i)].substr(0, 58), 0xF2, 0xF2, 0xE6, 1);
                ly += 12;
                if (ly > FB_H - 170) break;
            }
        }

        // Botones.
        for (int i = 0; i < 5; ++i) {
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
