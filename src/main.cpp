// M0 - esqueleto del motor AoE Mobile.
// Muestra un color, la version del programa, FPS/RAM y las ultimas lineas del
// registro (spec §6 M0). El registro se guarda en archivo y, en Android, se
// puede compartir como texto con un boton grande en pantalla (spec §7).
//
// Capas respetadas (spec §4): platform (SDL, registro, metricas) y render
// (composicion por software) no se mezclan con logica de juego (aun no hay sim).

#include <SDL.h>
#ifdef __ANDROID__
#include <jni.h>          // tipos JNI (JNIEnv, jobject...); SDL no lo incluye
#include <SDL_system.h>   // SDL_AndroidGetJNIEnv/GetActivity devuelven void*
#endif
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "platform/log.h"
#include "platform/metrics.h"
#include "render/framebuffer.h"
#include "version.h"

namespace {

constexpr int FB_W = 960;
constexpr int FB_H = 540;

struct Opciones {
    bool selftest = false;               // N cuadros sin ventana -> captura + registro
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

// Donde guardar el registro, por orden de preferencia:
//  Android: almacenamiento externo de la app (Android/data/<pkg>/files) -> el
//  usuario puede abrirlo/copiarlo con el explorador de archivos.
//  Escritorio: directorio de preferencias de SDL.
//  --logdir (autoprueba en CI) invalida lo anterior.
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

std::string versionSdl() {
    SDL_version v;
    SDL_GetVersion(&v);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d.%d.%d", v.major, v.minor, v.patch);
    return buf;
}

#ifdef __ANDROID__
// Comparte texto con la app que el usuario elija (ACTION_SEND + EXTRA_TEXT).
// Sin FileProvider ni permisos: suficiente para enviar el registro (M0) y mas
// adelante cualquier evidencia de texto que las herramientas produzcan.
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
#endif

// Region tactil del boton COMPARTIR LOG: bien grande, abajo a la derecha (spec §7).
bool enBotonCompartir(float x, float y) {
    return x > static_cast<float>(FB_W) - 300.0f && y > static_cast<float>(FB_H) - 90.0f;
}

} // namespace

int main(int argc, char* argv[]) {
    const Opciones op = parseArgs(argc, argv);

    // Primero el registro: todo lo demas debe quedar anotado.
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
                if (ev.type == SDL_FINGERDOWN &&
                    enBotonCompartir(ev.tfinger.x * FB_W, ev.tfinger.y * FB_H)) {
                    std::string texto = std::string(AOE_VERSION_STRING) + " | SDL " + versionSdl() + "\n";
                    texto += "LOG:\n";
                    for (const std::string& l : platform::Log::recentLines(40)) texto += l + "\n";
#ifdef __ANDROID__
                    const bool okShare = compartirTexto(texto);
                    platform::Log::write("COMPARTIR LOG: %s",
                                         okShare ? "intent lanzado" : "fallo (sin intent)");
#endif
                }
            } else if (ev.type == SDL_MOUSEBUTTONDOWN) {
                char buf[96];
                snprintf(buf, sizeof(buf), "clic (%d, %d)", ev.button.x, ev.button.y);
                ultimoEvento = buf;
            }
        }

        platform::Metrics::beginFrame();

        // Composicion por software: fondo + franja + texto embebido.
        fb.clear(0x11, 0x2C, 0x33);
        fb.fillRect(0, 0, FB_W, 6, 0x3D, 0x8B, 0x7A);

        int y = 28;
        fb.drawText(24, y, AOE_VERSION_STRING, 0xF2, 0xF2, 0xE6, 2);
        y += 30;
        fb.drawText(24, y,
                    "SDL " + versionSdl() + " | C++17 | render por software | " AOE_MILESTONE,
                    0x9F, 0xB3, 0xB8, 1);
        y += 20;

        char buf[160];
        snprintf(buf, sizeof(buf), "FPS: %.1f | RAM: %.1f MB", platform::Metrics::fps(),
                 platform::Metrics::ramKb() / 1024.0);
        fb.drawText(24, y, buf, 0xF0, 0xC9, 0x4A, 1);
        y += 20;
        snprintf(buf, sizeof(buf), "LOG: %s", rutaLog.empty() ? "(no disponible)" : rutaLog.c_str());
        fb.drawText(24, y, buf, 0x9F, 0xB3, 0xB8, 1);
        y += 26;

        fb.drawText(24, y, "REGISTRO RECIENTE:", 0x6C, 0xC5, 0xC7, 1);
        y += 16;
        for (const std::string& l : platform::Log::recentLines(9)) {
            fb.drawText(24, y, l, 0xF2, 0xF2, 0xE6, 1);
            y += 14;
        }

        // Boton COMPARTIR LOG (region grande, >= 48 px equivalentes).
        fb.fillRect(FB_W - 292, FB_H - 78, 268, 56, 0x1D, 0x4E, 0x50);
        fb.fillRect(FB_W - 292, FB_H - 78, 268, 3, 0x3D, 0x8B, 0x7A);
        fb.drawText(FB_W - 268, FB_H - 56, "COMPARTIR LOG", 0xF2, 0xF2, 0xE6, 2);

        snprintf(buf, sizeof(buf), "ULTIMO EVENTO: %s", ultimoEvento.c_str());
        fb.drawText(24, FB_H - 40, buf, 0x8F, 0xD1, 0x9F, 1);

        // Fallo cerrado (spec §2, principio 3): si presentar falla, se explica y termina.
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
