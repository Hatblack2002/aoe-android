#include "assets/gen.h"
#include "assets/formato.h"
#include "assets/inflate.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace assets {

namespace {

// ------------------------------------------------------------------
// Escritor binario little-endian.
// ------------------------------------------------------------------
struct Escritor {
    std::vector<uint8_t> b;

    void u8(unsigned v) { b.push_back(static_cast<uint8_t>(v)); }
    void i8(int v) { b.push_back(static_cast<uint8_t>(v)); }
    void u16(unsigned v) {
        b.push_back(static_cast<uint8_t>(v));
        b.push_back(static_cast<uint8_t>(v >> 8));
    }
    void i16(int v) { u16(static_cast<unsigned>(v) & 0xFFFFu); }
    void u32(unsigned long v) {
        b.push_back(static_cast<uint8_t>(v));
        b.push_back(static_cast<uint8_t>(v >> 8));
        b.push_back(static_cast<uint8_t>(v >> 16));
        b.push_back(static_cast<uint8_t>(v >> 24));
    }
    void i32(long v) { u32(static_cast<unsigned long>(v) & 0xFFFFFFFFul); }
    void f32(float f) {
        uint32_t bits;
        memcpy(&bits, &f, 4);
        u32(bits);
    }
    void fijas(const char* s, std::size_t total) {
        const std::size_t len = strlen(s);
        if (len > total) throw ErrorDatos{std::string("cadena demasiado larga: ") + s};
        for (std::size_t i = 0; i < total; ++i) u8(i < len ? s[i] : 0);
    }
    void crudo(const std::vector<uint8_t>& datos) {
        b.insert(b.end(), datos.begin(), datos.end());
    }
    void poner32(std::size_t donde, unsigned long v) {
        b[donde] = static_cast<uint8_t>(v);
        b[donde + 1] = static_cast<uint8_t>(v >> 8);
        b[donde + 2] = static_cast<uint8_t>(v >> 16);
        b[donde + 3] = static_cast<uint8_t>(v >> 24);
    }
    std::size_t pos() const { return b.size(); }
};

bool guardar(const std::string& ruta, const std::vector<uint8_t>& datos) {
    std::ofstream f(ruta, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(datos.data()),
            static_cast<std::streamsize>(datos.size()));
    return static_cast<bool>(f);
}

bool guardarTexto(const std::string& ruta, const std::string& texto) {
    const std::vector<uint8_t> datos(texto.begin(), texto.end());
    return guardar(ruta, datos);
}

// ------------------------------------------------------------------
// Paleta JASC-PAL determinista.
// ------------------------------------------------------------------
std::string paletaJasc() {
    std::string t = "JASC-PAL\r\n0100\r\n256\r\n";
    char linea[64];
    for (int i = 0; i < 256; ++i) {
        int r = 0, g = 0, b = 0;
        if (i == 0) {
            r = g = b = 0;
        } else if (i < 16) {
            r = g = b = i * 16;
        } else if (i >= 16 && i < 144) {
            const int p = (i - 16) / 16;   // jugador 0..7
            const int s = (i - 16) % 16;   // sombra 0..15
            const int base[8][3] = {
                {0, 0, 255}, {255, 0, 0}, {0, 200, 0}, {255, 255, 0},
                {255, 128, 0}, {0, 220, 220}, {180, 0, 255}, {220, 220, 220}};
            const int brillo = 255 - s * 15;
            r = base[p][0] * brillo / 255;
            g = base[p][1] * brillo / 255;
            b = base[p][2] * brillo / 255;
        } else if (i == 243) {
            r = 255; g = 255; b = 0;
        } else {
            r = (i * 7) % 256;
            g = (i * 13) % 256;
            b = (i * 29) % 256;
        }
        snprintf(linea, sizeof(linea), "%d %d %d\r\n", r, g, b);
        t += linea;
    }
    return t;
}

// ------------------------------------------------------------------
// Comandos SLP: generadores de flujos que suman exactamente `n` pixeles.
// ------------------------------------------------------------------
// Dibujo corto encadenado (max 63 px por comando).
void dibujoCorto(std::vector<uint8_t>& ops, int n, int& indice) {
    while (n > 0) {
        const int trozo = n > 60 ? 60 : n;
        ops.push_back(static_cast<uint8_t>(trozo << 2));
        for (int k = 0; k < trozo; ++k) ops.push_back(static_cast<uint8_t>((indice++) & 0xFF));
        n -= trozo;
    }
}

void saltoCorto(std::vector<uint8_t>& ops, int n) {
    if (n <= 0) return;
    if (n <= 63) {
        ops.push_back(static_cast<uint8_t>((n << 2) | 0x01));
    } else {
        ops.push_back(0x01);  // contador en el byte siguiente
        ops.push_back(static_cast<uint8_t>(n));
    }
}

// Patron determinista del marco 1: indice(x,y) = (x + 16*y) mod 256.
std::vector<uint8_t> opsPatron(int contenido, int x0, int y) {
    std::vector<uint8_t> ops;
    int indice = x0 + 16 * y;
    dibujoCorto(ops, contenido, indice);
    ops.push_back(0x0F);
    return ops;
}

// ------------------------------------------------------------------
// SLP 2.0N de prueba: 3 marcos (120x32, 16x16, 1x1).
// ------------------------------------------------------------------
struct ConstructorSLP {
    Escritor out;
    std::size_t offCmdMarco1Fila0 = 0;
    std::size_t offCmdMarco1Fila1 = 0;
};

ConstructorSLP construirSLP() {
    std::size_t offCmdFila0 = 0, offCmdFila1 = 0;
    struct MarcoInfo {
        int ancho, alto, anclaX, anclaY;
    };
    const MarcoInfo infos[3] = {{120, 32, 60, 16}, {16, 16, 8, 8}, {1, 1, 0, 0}};
    const int numMarcos = 3;

    struct DatosMarco {
        Escritor contornos, tablaCmds, cmds;
        int ancho = 0, alto = 0;
    };
    DatosMarco m[3];
    for (int f = 0; f < numMarcos; ++f) {
        m[f].ancho = infos[f].ancho;
        m[f].alto = infos[f].alto;
    }

    // --- marco 0: ejercita todos los comandos ---
    std::vector<std::vector<std::size_t>> offsFilas(numMarcos);
    {
        const int A = 120;
        for (int y = 0; y < 32; ++y) {
            const int izq = y % 3, der = y % 2;
            const int contenido = A - izq - der;
            std::vector<uint8_t> ops;
            int indice = 1;
            switch (y) {
                case 0:
                    // Fila totalmente transparente: sin comandos.
                    break;
                case 1:
                    // Relleno 0x07 con contador en el byte siguiente.
                    ops.push_back(0x07);
                    ops.push_back(static_cast<uint8_t>(contenido));
                    ops.push_back(10);
                    break;
                case 2:
                    // Relleno de color de jugador 0x0A.
                    ops.push_back(0x0A);
                    ops.push_back(static_cast<uint8_t>(contenido));
                    ops.push_back(3);
                    break;
                case 3:
                    // Dibujo corto + salto corto con contador "next" + dibujo.
                    dibujoCorto(ops, 50, indice);
                    ops.push_back(0x01);  // skip con contador next
                    ops.push_back(20);
                    dibujoCorto(ops, contenido - 70, indice);
                    break;
                case 4: {
                    // Dibujo largo (greater draw 0x02) de 100 + salto corto.
                    const int nLargo = 100;
                    ops.push_back(static_cast<uint8_t>(0x02 | ((nLargo >> 4) & 0xF0)));
                    ops.push_back(static_cast<uint8_t>(nLargo & 0xFF));
                    for (int k = 0; k < nLargo; ++k) {
                        ops.push_back(static_cast<uint8_t>(80 + k % 64));
                    }
                    saltoCorto(ops, contenido - nLargo);
                    break;
                }
                case 5: {
                    // Salto largo (greater skip 0x03) de 70 + dibujo corto.
                    const int nSalto = 70;
                    ops.push_back(static_cast<uint8_t>(0x03 | ((nSalto >> 4) & 0xF0)));
                    ops.push_back(static_cast<uint8_t>(nSalto & 0xFF));
                    dibujoCorto(ops, contenido - nSalto, indice);
                    break;
                }
                case 6:
                    // Color de jugador por pixel (0x06) con contador next.
                    ops.push_back(0x06);
                    ops.push_back(static_cast<uint8_t>(contenido));
                    for (int k = 0; k < contenido; ++k) ops.push_back(static_cast<uint8_t>(k % 10));
                    break;
                case 7: {
                    // Sombra (0x0B) en dos trozos con contadores next.
                    const int mitad = contenido / 2;
                    ops.push_back(0x0B);
                    ops.push_back(static_cast<uint8_t>(mitad));
                    ops.push_back(0x0B);
                    ops.push_back(static_cast<uint8_t>(contenido - mitad));
                    break;
                }
                case 8:
                    // Contornos: 0x4E (1px) + 0x5E relleno + 0x6E (1px) + 0x7E relleno 1px.
                    ops.push_back(0x4E);
                    ops.push_back(0x5E);
                    ops.push_back(static_cast<uint8_t>(contenido - 3));
                    ops.push_back(0x6E);
                    ops.push_back(0x7E);
                    ops.push_back(1);
                    break;
                default:
                    // Resto: patron simple.
                    ops = opsPatron(contenido, 0, y);
                    break;
            }
            ops.push_back(0x0F);  // fin de fila (para los casos que no lo pusieron)
            m[0].contornos.u16(static_cast<unsigned>(y == 0 ? 0x8000 : izq));
            m[0].contornos.u16(static_cast<unsigned>(y == 0 ? 0x8000 : der));
            m[0].tablaCmds.u32(0);  // hueco: se parchea con offset absoluto
            offsFilas[0].push_back(m[0].cmds.pos());
            m[0].cmds.crudo(ops);
        }
    }

    // --- marco 1: patron exacto indice = (x + 16y) mod 256 ---
    {
        for (int y = 0; y < 16; ++y) {
            m[1].contornos.u16(0);
            m[1].contornos.u16(0);
            m[1].tablaCmds.u32(0);
            offsFilas[1].push_back(m[1].cmds.pos());
            m[1].cmds.crudo(opsPatron(16, 0, y));
        }
    }

    // --- marco 2: 1x1 ---
    {
        m[2].contornos.u16(0);
        m[2].contornos.u16(0);
        m[2].tablaCmds.u32(0);
        offsFilas[2].push_back(m[2].cmds.pos());
        m[2].cmds.u8(0x04);  // lesser draw 1
        m[2].cmds.u8(200);
        m[2].cmds.u8(0x0F);
    }

    // Ensamblar archivo.
    ConstructorSLP c;
    Escritor& out = c.out;
    out.fijas("2.0N", 4);
    out.i32(numMarcos);
    out.fijas("AoE Mobile M1 synthetic", 24);
    const std::size_t offInfos = out.pos();
    for (int f = 0; f < numMarcos; ++f) {
        out.u32(0); out.u32(0); out.u32(0); out.u32(0x10);
        out.i32(infos[f].ancho);
        out.i32(infos[f].alto);
        out.i32(infos[f].anclaX);
        out.i32(infos[f].anclaY);
    }
    struct Bloque {
        std::size_t off, tamContornos, tamTabla;
    };
    Bloque bloques[3];
    for (int f = 0; f < numMarcos; ++f) {
        bloques[f].off = out.pos();
        bloques[f].tamContornos = m[f].contornos.b.size();
        bloques[f].tamTabla = m[f].tablaCmds.b.size();
        out.crudo(m[f].contornos.b);
        out.crudo(m[f].tablaCmds.b);
        out.crudo(m[f].cmds.b);
    }
    for (int f = 0; f < numMarcos; ++f) {
        const std::size_t base = offInfos + static_cast<std::size_t>(f) * 32;
        // La tabla de comandos vive justo tras la tabla de contornos; los
        // comandos, tras la tabla (doc slp-files.md).
        out.poner32(base + 0, bloques[f].off + bloques[f].tamContornos);
        out.poner32(base + 4, bloques[f].off);
        // Tabla de comandos: desplazamientos ABSOLUTOS en el archivo
        // (verificado contra slp-files.md y slp.pyx, que los usan tal cual).
        const std::size_t baseTabla = bloques[f].off + bloques[f].tamContornos;
        const std::size_t baseCmds = baseTabla + bloques[f].tamTabla;
        for (std::size_t y = 0; y < offsFilas[f].size(); ++y) {
            out.poner32(baseTabla + y * 4, baseCmds + offsFilas[f][y]);
        }
    }
    const std::size_t inicioCmdsMarco1 =
        bloques[1].off + bloques[1].tamContornos + bloques[1].tamTabla;
    c.offCmdMarco1Fila0 = inicioCmdsMarco1 + offsFilas[1][0];
    c.offCmdMarco1Fila1 = inicioCmdsMarco1 + offsFilas[1][1];
    return c;
}

// ------------------------------------------------------------------
// DRS con el SLP (tabla ' pls') y la paleta (tabla 'anib').
// ------------------------------------------------------------------
std::vector<uint8_t> construirDRS(const std::vector<uint8_t>& slp,
                                  const std::vector<uint8_t>& pal) {
    const unsigned long offInfo0 = 88;
    const unsigned long offInfo1 = 100;
    const unsigned long offDatos = 112;

    Escritor out;
    out.fijas("AoE Mobile M1 datos sinteticos", 40);
    out.fijas("1.00", 4);
    out.fijas("tribe", 12);
    out.i32(2);
    out.i32(static_cast<long>(offDatos));
    // Tabla 0: ' pls' (SLP) con los bytes tal cual van en el archivo.
    out.u8(' '); out.u8('p'); out.u8('l'); out.u8('s');
    out.i32(static_cast<long>(offInfo0));
    out.i32(1);
    // Tabla 1: 'anib' (binario/paleta).
    out.u8('a'); out.u8('n'); out.u8('i'); out.u8('b');
    out.i32(static_cast<long>(offInfo1));
    out.i32(1);
    out.u32(70001);
    out.u32(offDatos);
    out.u32(slp.size());
    out.u32(50500);
    out.u32(offDatos + slp.size());
    out.u32(pal.size());
    if (out.pos() != offDatos) {
        throw ErrorDatos{"generador: layout DRS inconsistente (" +
                         std::to_string(out.pos()) + " != 112)"};
    }
    out.crudo(slp);
    out.crudo(pal);
    return out.b;
}

// ------------------------------------------------------------------
// dat ROR de prueba.
// ------------------------------------------------------------------
struct UnidadPrueba {
    int8_t tipo;
    const char* nombre;
    int16_t id;
    int16_t hp;
    float velocidad;
    int16_t graficoQuieto;
    int16_t graficoMover;
    int16_t graficoConstruccion;
    int numComandos;
};

// Unidad completa: byte de tipo + UnitObject ROR + campos por tipo.
void escribirUnidad(Escritor& out, const UnidadPrueba& u) {
    out.i8(u.tipo);  // unit_type (lo lee el lector al entrar al bloque)
    const std::string nombre = u.nombre;
    out.u16(static_cast<unsigned>(nombre.size()));
    out.i16(u.id);
    out.u16(0); out.u16(0);                          // language_dll
    out.i16(0);                                      // unit_class
    out.i16(u.graficoQuieto);                        // idle_graphic0
    out.i16(-1); out.i16(-1);                        // dying/undead_graphic
    out.i8(0);                                       // death_mode
    out.i16(u.hp);                                   // hit_points
    out.f32(4.0f);                                   // line_of_sight
    out.i8(0);                                       // garrison_capacity
    out.f32(0.5f); out.f32(0.5f); out.f32(0.5f);     // radius
    out.i16(-1); out.i16(-1);                        // train_sound/dead_unit
    out.i8(0); out.i8(0);                            // placement/can_be_built_on
    out.i16(-1); out.i8(0); out.i16(-1);             // icon/hidden/old_portrait
    out.i8(1);                                       // enabled
    out.i16(-1); out.i16(-1); out.i16(-1); out.i16(-1);  // placement terrains
    out.f32(1.0f); out.f32(1.0f);                    // clearance_size
    out.i8(0); out.i8(0);                            // elevation_mode/visible_in_fog
    out.i16(0);                                      // terrain_restriction
    out.i8(0); out.i16(0); out.f32(0.0f);            // fly_mode/resource_capacity/decay
    out.i8(0); out.i8(0); out.i8(0); out.i8(0);      // blast/combat/interaction/map_draw
    out.f32(0.0f);                                   // attack_reaction
    out.i8(0);                                       // minimap_color
    out.i32(0); out.i32(0); out.i32(0);              // language_dll/hotkeys
    out.i8(0); out.i8(0); out.i8(0); out.i8(0);      // recyclable/.../drop
    out.u8(0); out.i8(0); out.i16(0);                // trait/civ_id/attribute_piece
    out.u8(0);                                       // editor_selection_color
    out.f32(0.5f); out.f32(0.5f); out.f32(0.5f);     // selection_shape
    for (int i = 0; i < 3; ++i) {                    // resource_storage x3 (9 B c/u)
        out.i16(-1); out.f32(0.0f); out.i8(0);
    }
    out.i8(0);                                       // damage_graphic_count
    out.i16(-1); out.i16(-1);                        // selection/dying_sound
    out.i8(0); out.i8(0);                            // old_attack_mode/convert_terrain
    out.crudo(std::vector<uint8_t>(nombre.begin(), nombre.end()));
    out.i16(u.id);                                   // id1

    const bool tieneVelocidad = u.tipo >= 20 && u.tipo != 10 && u.tipo != 90;
    if (tieneVelocidad) out.f32(u.velocidad);        // speed (Animated)
    const bool tieneMover = u.tipo == 30 || u.tipo == 40 || u.tipo == 60 ||
                            u.tipo == 70 || u.tipo == 80;
    if (tieneMover) {                                // MovingUnit (ROR)
        out.i16(u.graficoMover);                     // move_graphics
        out.i16(-1);                                 // run_graphics
        out.f32(10.0f);                              // turn_speed
        out.i8(0);                                   // old_size_class
        out.i16(-1);                                 // trail_unit_id
        out.u8(0);                                   // trail_opsions
        out.f32(0.0f);                               // trail_spacing
        out.i8(0);                                   // old_move_algorithm
    }
    const bool tieneAccion = u.tipo == 40 || u.tipo == 60 || u.tipo == 70 || u.tipo == 80;
    if (tieneAccion) {                               // ActionUnit
        out.i16(-1);                                 // default_task_id
        out.f32(4.0f);                               // search_radius
        out.f32(1.0f);                               // work_rate
        out.i16(-1); out.i16(-1);                    // drop_sites[2]
        out.i8(0);                                   // task_group
        out.i16(-1); out.i16(-1);                    // command/stop_sound
        out.i8(0);                                   // run_pattern
        out.u16(static_cast<unsigned>(u.numComandos));
        for (int c = 0; c < u.numComandos; ++c) {    // UnitCommand (59 B)
            out.u16(1); out.i16(1);                  // used/command_id
            out.i8(0); out.i16(0);                   // is_default/type
            out.i16(0); out.i16(-1); out.i16(-1);    // class_id/unit_id/terrain_id
            out.i16(-1); out.i16(-1); out.i16(-1);   // resource_in/mult/out
            out.i16(-1);                             // unused_resource
            out.f32(1.0f); out.f32(0.0f); out.f32(0.0f);  // work_value1/2/range
            out.i8(0); out.f32(0.0f);                // search_mode/search_time
            out.i8(0); out.i8(0);                    // enable_targeting/combat_flag
            out.i16(-1); out.i16(0);                 // gather_type/work_mode2
            out.i8(0); out.i8(0); out.i8(0);         // owner/carry_check/state_build
            out.i16(-1); out.i16(-1);                // move/proceed_sprite
            out.i16(-1); out.i16(-1);                // work/carry_sprite
            out.i16(-1); out.i16(-1);                // gather/deposit_sound
        }
    }
    const bool tieneArmor = u.tipo == 60 || u.tipo == 70 || u.tipo == 80;
    if (tieneArmor) out.u8(0);                       // default_armor (ProjectileUnit ROR)
    if (u.tipo == 60) {                              // MissileUnit
        out.i8(0); out.i8(0); out.i8(0); out.i8(0); out.i8(0);
        out.f32(0.0f);
    }
    const bool tieneVivo = u.tipo == 70 || u.tipo == 80;
    if (tieneVivo) {                                 // LivingUnit
        out.i16(3); out.i16(50); out.i16(1);         // costos: comida 50
        out.i16(2); out.i16(30); out.i16(1);         // madera 30
        out.i16(-1); out.i16(-1); out.i16(-1);       // tercer costo vacio
        out.f32(1.0f);                               // projectile_min_count
        out.i8(0);                                   // projectile_max_count
        out.f32(0.0f); out.f32(0.0f); out.f32(0.0f); // spawning_area
        out.i32(-1);                                 // projectile_id1
        out.i32(-1);                                 // special_graphic_id
        out.i8(0);                                   // special_activation
        out.i16(0);                                  // pierce_armor_displayed
    }
    if (u.tipo == 80) {                              // BuildingUnit
        out.i16(u.graficoConstruccion);              // construction_graphic_id
        out.i16(-1);                                 // construction_sound_id
    }
}

std::vector<uint8_t> construirDatPayload() {
    Escritor out;
    out.fijas("AOM1TEST", 8);
    // --- terrenos ---
    const unsigned trc = 2, tc = 3;
    out.u16(trc);
    out.u16(tc);
    for (unsigned i = 0; i < trc; ++i) out.i32(0);
    for (unsigned i = 0; i < trc * tc; ++i) out.f32(1.0f);
    // --- colores de jugador (36 B c/u en ROR) ---
    out.u16(9);
    for (unsigned p = 0; p < 9; ++p) {
        char nombre[32];
        snprintf(nombre, sizeof(nombre), "JUGADOR_%u", p);
        out.fijas(nombre, 30);
        out.i16(static_cast<int>(p));
        out.i16(0);
        out.u8(0);
        out.u8(0);
    }
    // --- sonidos ---
    out.u16(1);
    out.i16(0); out.i16(0); out.u16(2); out.i32(0);
    out.fijas("snd_a.wav", 13); out.i32(100); out.i16(50);
    out.fijas("snd_b.wav", 13); out.i32(101); out.i16(50);
    // --- graficos: conteo + punteros (los cuerpos van al final del archivo,
    // en la cola que el lector no camina, como en el dat real que los
    // alcanza por puntero) ---
    out.u16(2);
    const std::size_t offPtrs = out.pos();
    out.u32(0); out.u32(0);
    // --- mapa ---
    out.i32(0); out.i32(0);
    out.i32(64); out.i32(64); out.i32(64); out.i32(64);
    for (int i = 0; i < 19; ++i) { out.i16(48); out.i16(24); out.i16(0); }
    out.i16(0);
    // --- Terrain x32 ---
    for (int t = 0; t < 32; ++t) {
        out.i8(t < 8 ? 1 : 0);
        out.i8(0);
        char nom[16];
        snprintf(nom, sizeof(nom), "TERR_%d", t);
        out.fijas(nom, 13);
        snprintf(nom, sizeof(nom), "terr%d.slp", t);
        out.fijas(nom, 13);
        out.i32(t < 8 ? 15000 + t : -1);
        out.i32(0);
        out.i32(-1);
        out.u8((t * 40) % 256); out.u8((t * 30) % 256); out.u8((t * 20) % 256);
        out.u8((t * 10) % 256); out.u8((t * 5) % 256);
        out.i8(1); out.i8(0);
        out.i8(0); out.i16(0); out.i16(0); out.f32(0.0f); out.f32(0.0f);
        out.i16(0); out.i16(0); out.f32(0.0f); out.i8(0); out.i8(0);
        for (int f = 0; f < 19; ++f) { out.i16(1); out.i16(1); out.i16(0); }
        out.i16(-1); out.i16(-1); out.i16(-1);
        for (int b = 0; b < 32; ++b) out.i16(-1);
        for (int u2 = 0; u2 < 30; ++u2) out.i16(-1);
        for (int u2 = 0; u2 < 30; ++u2) out.i16(0);
        for (int u2 = 0; u2 < 30; ++u2) out.i8(0);
        out.i16(0);
        out.i16(-1);
    }
    // --- TerrainBorder x16 ---
    for (int b = 0; b < 16; ++b) {
        out.i8(0); out.i8(0);
        char nom[16];
        snprintf(nom, sizeof(nom), "BORD_%d", b);
        out.fijas(nom, 13);
        out.fijas("", 13);
        out.i32(-1); out.i32(0); out.i32(-1);
        out.u8(0); out.u8(0); out.u8(0);
        out.i8(0); out.i16(0); out.i16(0); out.f32(0.0f); out.f32(0.0f);
        out.i16(0); out.i16(0); out.f32(0.0f); out.i8(0); out.i8(0);
        for (int f = 0; f < 19 * 12; ++f) { out.i16(1); out.i16(1); out.i16(0); }
        out.i16(0); out.i16(-1); out.i16(0);
    }
    out.i32(0);  // map_row_offset
    out.u16(0); out.u16(0); out.i16(7);
    out.i16(48); out.i16(24); out.i16(12); out.i16(24); out.i16(6);
    for (int i = 0; i < 6; ++i) out.i16(0);
    out.i32(0); out.i32(0); out.i32(0);
    out.i8(0); out.i8(0);
    out.u8(0); out.u8(0);
    for (int i = 0; i < 5; ++i) out.u32(0);
    // --- mapas aleatorios ---
    out.u32(1); out.u32(0);
    for (int i = 0; i < 18; ++i) out.i32(0);  // MapInfo
    for (int i = 0; i < 9; ++i) out.i32(0);   // Map fijo: borders x4 + usage/water/base/coverage/unused
    out.u32(1); out.i32(0);   // base_zone_count/ptr
    out.u32(1); out.i32(0);   // map_terrain_count/ptr
    out.u32(1); out.i32(0);   // map_unit_count/ptr
    out.u32(1); out.i32(0);   // map_elevation_count/ptr
    // base_zones: 1 MapLand (44 B)
    out.i32(0); out.i32(0); out.i32(0); out.i32(3);
    out.i8(0); out.i8(0); out.i16(0);
    out.i32(0); out.i32(0);
    out.i8(100); out.i8(0); out.i16(0);
    out.i32(10); out.i32(0); out.i32(0);
    // map_terrains: 1 MapTerrain (24 B)
    out.i32(100); out.i32(0); out.i32(5); out.i32(0); out.i32(0); out.i32(0);
    // map_units: 1 MapUnit (44 B)
    out.i32(4); out.i32(-1);
    out.i8(0); out.i8(0); out.i16(0);
    out.i32(5); out.i32(0); out.i32(3); out.i32(4);
    out.i32(0); out.i32(0); out.i32(2); out.i32(8);
    // map_elevations: 1 MapElevation (24 B)
    out.i32(100); out.i32(0); out.i32(3); out.i32(0); out.i32(1); out.i32(0);
    // --- paquetes de efectos ---
    out.u32(1);
    out.fijas("BUNDLE_M1", 31);
    out.u16(2);
    for (int e = 0; e < 2; ++e) { out.u8(1); out.i16(1); out.i16(2); out.i16(3); out.f32(1.5f); }
    // --- civilizaciones ---
    out.u16(2);
    const UnidadPrueba unidadesGaia[2] = {
        {90, "ARBOL_M1", 90, 20, 0.0f, 150, 0, 0, 0},
        {10, "ORO_GAIA_M1", 11, -1, 0.0f, -1, 0, 0, 0},
    };
    const unsigned slotsGaia[2] = {1, 3};  // huecos en 0 y 2
    const UnidadPrueba unidadesBrit[8] = {
        {10, "ORO_M1", 10, -1, 0.0f, -1, 0, 0, 0},
        {20, "FAROLA_M1", 20, 5, 0.0f, 100, 0, 0, 0},
        {25, "DOBLE_M1", 25, 5, 0.0f, 100, 0, 0, 0},
        {30, "ALDEANO_M1", 30, 25, 0.8f, 101, 102, 0, 0},
        {40, "MILITE_M1", 40, 40, 0.9f, 110, 111, 0, 1},
        {60, "FLECHA_M1", 60, 1, 3.0f, 120, 0, 0, 0},
        {70, "ARQUERO_M1", 70, 30, 0.9f, 130, 131, 0, 1},
        {80, "CASA_M1", 80, 100, 0.0f, 140, 0, 141, 1},
    };
    const unsigned slotsBrit[8] = {0, 1, 2, 3, 4, 5, 6, 7};  // hueco en 8
    struct CivPrueba {
        const char* nombre;
        int8_t tipoJugador;
        unsigned numRecursos;
        const UnidadPrueba* unidades;
        const unsigned* slots;
        unsigned numUnidades;
        unsigned totalSlots;
    };
    const CivPrueba civs[2] = {
        {"GAIA", 0, 0, unidadesGaia, slotsGaia, 2, 4},
        {"BRITANOS", 1, 4, unidadesBrit, slotsBrit, 8, 9},
    };
    for (int c = 0; c < 2; ++c) {
        const std::size_t inicioCiv = out.pos();
        out.i8(civs[c].tipoJugador);
        out.fijas(civs[c].nombre, 20);
        out.u16(civs[c].numRecursos);
        out.i16(-1);
        for (unsigned rr = 0; rr < civs[c].numRecursos; ++rr) out.f32(100.0f + rr);
        out.i8(0);
        out.u16(civs[c].totalSlots);
        const std::size_t offOffsets = out.pos();
        for (unsigned s = 0; s < civs[c].totalSlots; ++s) out.i32(0);
        for (unsigned u2 = 0; u2 < civs[c].numUnidades; ++u2) {
            const std::size_t offUnidad = out.pos() - inicioCiv;
            escribirUnidad(out, civs[c].unidades[u2]);
            out.poner32(offOffsets + static_cast<std::size_t>(civs[c].slots[u2]) * 4, offUnidad);
        }
    }
    out.u16(3);  // research_count
    for (int i = 0; i < 24; ++i) out.u8(0xAA);
    // Cuerpos ficticios de los 2 graficos, en la cola sin parsear.
    for (int g = 0; g < 2; ++g) {
        const std::size_t offCuerpo = out.pos();
        for (int k = 0; k < 16; ++k) out.u8(static_cast<uint8_t>(0xC0 + g));
        out.poner32(offPtrs + static_cast<std::size_t>(g) * 4, offCuerpo);
    }
    return out.b;
}

} // namespace

int generarDatosPrueba(const std::string& dir) {
    int escritos = 0;
    auto ruta = [&](const std::string& nombre) { return dir + "/" + nombre; };

    const std::string palTexto = paletaJasc();
    const std::vector<uint8_t> palBytes(palTexto.begin(), palTexto.end());
    if (!guardar(ruta("prueba.pal"), palBytes)) return -1;
    ++escritos;

    const ConstructorSLP slp = construirSLP();
    if (!guardar(ruta("prueba.slp"), slp.out.b)) return -1;
    ++escritos;

    const std::vector<uint8_t> drs = construirDRS(slp.out.b, palBytes);
    if (!guardar(ruta("prueba.drs"), drs)) return -1;
    ++escritos;

    const std::vector<uint8_t> payload = construirDatPayload();
    if (!guardar(ruta("empires_test_payload.bin"), payload)) return -1;
    ++escritos;
    const std::vector<uint8_t> dat = desinflarAlmacenado(payload.data(), payload.size());
    if (!guardar(ruta("empires_test.dat"), dat)) return -1;
    ++escritos;

    auto trunca = [&](const std::vector<uint8_t>& v, unsigned pct) {
        return std::vector<uint8_t>(v.begin(),
                                    v.begin() + static_cast<long>(v.size() * pct / 100));
    };
    if (!guardar(ruta("trunc50.drs"), trunca(drs, 50))) {
        return -1;
    }
    ++escritos;
    if (!guardar(ruta("trunc60.slp"), trunca(slp.out.b, 60))) {
        return -1;
    }
    ++escritos;
    if (!guardar(ruta("trunc40.dat"), trunca(dat, 40))) {
        return -1;
    }
    ++escritos;
    if (!guardar(ruta("trunc30.pal"), trunca(palBytes, 30))) {
        return -1;
    }
    ++escritos;

    std::vector<uint8_t> malo = drs;
    malo[0] = 0x01;  // copyright con byte no imprimible
    if (!guardar(ruta("drs_badmagic.drs"), malo)) {
        return -1;
    }
    ++escritos;

    malo = slp.out.b;
    malo[slp.offCmdMarco1Fila0] = 0x8E;  // dither extendido: no soportado
    if (!guardar(ruta("slp_badcmd.slp"), malo)) {
        return -1;
    }
    ++escritos;

    malo = slp.out.b;
    malo[slp.offCmdMarco1Fila1] = 0x20;  // lesser draw de 8: fila corta
    if (!guardar(ruta("slp_badrow.slp"), malo)) {
        return -1;
    }
    ++escritos;

    // dat con unit_offset fuera de rango: parchear el payload y recomprimir.
    std::vector<uint8_t> payloadMalo = payload;
    std::size_t posBrit = 0;
    for (std::size_t i = 0; i + 8 < payloadMalo.size(); ++i) {
        if (memcmp(payloadMalo.data() + i, "BRITANOS", 8) == 0) {
            posBrit = i;
            break;
        }
    }
    if (posBrit == 0) return -1;
    // Desde "BRITANOS": resto del nombre (20-8) + player_type ya antes... el
    // orden es player_type(1) + nombre(20) + recursos_count(2) + tech_tree(2)
    // + recursos(4x4) + icon_set(1) + unit_count(2) + offsets[]. "BRITANOS"
    // empieza en +1 del inicio de la civ; los offsets estan en +44 del inicio
    // => +43 desde "BRITANOS".
    const std::size_t offPrimerOffset = posBrit + 43;
    if (offPrimerOffset + 4 > payloadMalo.size()) return -1;
    payloadMalo[offPrimerOffset] = 0x7F;
    payloadMalo[offPrimerOffset + 1] = 0x7F;
    payloadMalo[offPrimerOffset + 2] = 0x7F;
    payloadMalo[offPrimerOffset + 3] = 0x7F;  // 0x7F7F7F7F > cualquier tamano
    const std::vector<uint8_t> datMalo =
        desinflarAlmacenado(payloadMalo.data(), payloadMalo.size());
    if (!guardar(ruta("dat_badptr.dat"), datMalo)) {
        return -1;
    }
    ++escritos;

    const std::string palMala = "JASC-XXX\r\n0100\r\n256\r\n" + palTexto.substr(22);
    if (!guardarTexto(ruta("pal_badheader.pal"), palMala)) {
        return -1;
    }
    ++escritos;

    std::string man;
    man += "MANIFIESTO de datos sinteticos M1\n";
    man += "slp: marcos=3; marco1=16x16 patron indice=(x+16y) mod 256\n";
    man += "drs: tablas=2; recursos=2 (70001.slp, 50500.bina)\n";
    man += "dat: civs=2 (GAIA: 4 slots / 2 unidades; BRITANOS: 9 slots / 8 unidades)\n";
    man += "dat: mapa=64x64; tile=48x24; media_altura=12; media_anchura=24; elev=6\n";
    man += "dat: terrenos_habilitados=8; colores_jugador=9; sonidos=1; graficos=2\n";
    man += "corruptos: trunc50.drs trunc60.slp trunc40.dat trunc30.pal drs_badmagic.drs\n";
    man += "           slp_badcmd.slp slp_badrow.slp dat_badptr.dat pal_badheader.pal\n";
    guardarTexto(ruta("MANIFIESTO.txt"), man);
    ++escritos;
    return escritos;
}

} // namespace assets
