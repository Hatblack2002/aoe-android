#include "assets/dat.h"
#include "assets/formato.h"

#include <cstdio>

namespace assets {

namespace {

// ---- Tamanos fijados por el layout ROR (ver cabecera del .h) ----
constexpr int kTerrainsFijos = 32;
constexpr int kTerrainBorders = 16;
constexpr int kTileSizes = 19;

// TerrainAnimation (openage terrain.py) = 23 bytes.
void saltarAnimacionTerreno(LectorLE& r) {
    r.saltar(1, "terrain.is_animated");
    r.saltar(2 + 2, "terrain.anim_frame_count/pause_frame_count");
    r.saltar(4 + 4, "terrain.interval/pause_between_loops");
    r.saltar(2 + 2, "terrain.frame/draw_frame");
    r.saltar(4, "terrain.animate_last");
    r.saltar(1 + 1, "terrain.frame_changed/drawn");
}

// FrameData (openage graphic.py/terrain.py) = 6 bytes.
void saltarFrameData(LectorLE& r, const char* ctx) {
    r.saltar(2 + 2 + 2, ctx);
}

// Un Terrain del layout ROR: campo a campo (terrain.py, rama no-DE/SWGB).
void saltarTerrain(LectorLE& r) {
    r.saltar(2, "terrain.enabled/random");
    r.saltar(13 + 13, "terrain.internal_name/filename");
    r.saltar(4 + 4 + 4, "terrain.slp_id/shape_ptr/sound_id");
    r.saltar(5 + 2, "terrain.map_color*/passable/impassable");
    saltarAnimacionTerreno(r);
    saltarFrameData(r, "terrain.elevation_graphics x19");  // x19 se repite abajo
    for (int i = 1; i < kTileSizes; ++i) saltarFrameData(r, "terrain.elevation_graphics");
    r.saltar(2 + 2 + 2, "terrain.replacement/to_draw0/to_draw1");
    r.saltar(2 * kTerrainsFijos, "terrain.borders i16[32]");
    r.saltar(2 * 30 + 2 * 30, "terrain.unit_id/density");
    r.saltar(30, "terrain.placement_flag");
    r.saltar(2 + 2, "terrain.units_used_count/phantom");
}

void saltarTerrainBorder(LectorLE& r) {
    r.saltar(2, "border.enabled/random");
    r.saltar(13 + 13, "border.internal_name/filename");
    r.saltar(4 + 4 + 4, "border.slp_id/shape_ptr/sound_id");
    r.saltar(3, "border.color");
    saltarAnimacionTerreno(r);
    for (int i = 0; i < kTileSizes * 12; ++i) saltarFrameData(r, "border.frames");
    r.saltar(2 + 2 + 2, "border.draw_tile/underlay/border_style");
}

// UnitCommand no-DE (unit.py): 59 bytes.
void saltarComandoUnidad(LectorLE& r) {
    r.saltar(2 * 18 + 4 * 4 + 7, "unit.command");
}

// Unidad completa por tipo (ramas ROR de unit.py). Extrae lo util.
Unidad leerUnidad(LectorLE& r, int8_t tipo, unsigned long inicioCiv, std::size_t tamTotal,
                  const std::string& archivo) {
    Unidad u;
    u.tipo = tipo;

    const uint16_t lenNombre = r.u16("unidad: name_length");
    u.id = r.i16("unidad: id0");
    r.saltar(2 + 2, "unidad: language_dll_name/creation");
    u.clase = r.i16("unidad: unit_class");
    u.graficoQuieto = r.i16("unidad: idle_graphic0");
    u.graficoMuerte = r.i16("unidad: dying_graphic");
    r.saltar(2, "unidad: undead_graphic");
    r.saltar(1, "unidad: death_mode");
    u.hitPoints = r.i16("unidad: hit_points");
    r.saltar(4, "unidad: line_of_sight");
    r.saltar(1, "unidad: garrison_capacity");
    u.radioX = r.f32("unidad: radius_x");
    u.radioY = r.f32("unidad: radius_y");
    r.saltar(4, "unidad: radius_z");
    r.saltar(2, "unidad: train_sound_id");
    r.saltar(2, "unidad: dead_unit_id");
    r.saltar(1 + 1, "unidad: placement_mode/can_be_built_on");
    r.saltar(2, "unidad: icon_id");
    r.saltar(1, "unidad: hidden_in_editor");
    r.saltar(2, "unidad: old_portrait_icon_id");
    r.saltar(1, "unidad: enabled");
    r.saltar(2 * 4, "unidad: placement_terrains");
    r.saltar(4 + 4, "unidad: clearance_size_x/y");
    r.saltar(1 + 1, "unidad: elevation_mode/visible_in_fog");
    r.saltar(2, "unidad: terrain_restriction");
    r.saltar(1, "unidad: fly_mode");
    r.saltar(2, "unidad: resource_capacity");
    r.saltar(4, "unidad: resource_decay");
    r.saltar(1 + 1 + 1 + 1, "unidad: blast/combat/interaction/map_draw");
    r.saltar(4, "unidad: attack_reaction");
    r.saltar(1, "unidad: minimap_color");
    r.saltar(4 + 4 + 4, "unidad: language_dll_help/hotkey/hot_keys");
    r.saltar(1 + 1 + 1 + 1, "unidad: recyclable/auto_gather/doppel/resource_drop");
    r.saltar(1 + 1, "unidad: trait/civilization_id");
    r.saltar(2, "unidad: attribute_piece");
    r.saltar(1, "unidad: editor_selection_color");
    r.saltar(4 * 3, "unidad: selection_shape_x/y/z");
    for (int i = 0; i < 3; ++i) {
        r.saltar(2, "resource_storage.type");
        r.saltar(4, "resource_storage.amount");
        r.saltar(1, "resource_storage.used_mode");
    }
    const int8_t numDanos = static_cast<int8_t>(r.u8("unidad: damage_graphic_count"));
    if (numDanos < 0 || numDanos > 64) {
        throw ErrorDatos{archivo + ": unidad con damage_graphic_count imposible (" +
                         std::to_string(numDanos) + ")"};
    }
    r.saltar(static_cast<std::size_t>(numDanos) * 5, "unidad: damage_graphics");
    r.saltar(2 + 2, "unidad: selection/dying_sound");
    r.saltar(1 + 1, "unidad: old_attack_mode/convert_terrain");
    if (lenNombre > 128) {
        throw ErrorDatos{archivo + ": unidad con nombre sospechosamente largo (" +
                         std::to_string(lenNombre) + " bytes)"};
    }
    u.nombre = r.cadena(lenNombre, "unidad: name");
    r.i16("unidad: id1");

    // Campos por tipo (jerarquia de unit.py, ramas ROR).
    switch (tipo) {
        case 10:
        case 90:
            break;  // UnitObject / TreeUnit
        case 20:
        case 25:
            u.velocidad = r.f32("unidad: speed");
            break;  // Animated/Doppelganger
        case 30: {
            u.velocidad = r.f32("unidad: speed");
            u.graficoMover = r.i16("unidad: move_graphics");
            r.saltar(2, "unidad: run_graphics");
            r.saltar(4, "unidad: turn_speed");
            r.saltar(1, "unidad: old_size_class");
            r.saltar(2, "unidad: trail_unit_id");
            r.saltar(1, "unidad: trail_opsions");
            r.saltar(4, "unidad: trail_spacing");
            r.saltar(1, "unidad: old_move_algorithm");
            break;
        }
        case 40: {
            u.velocidad = r.f32("unidad: speed");
            u.graficoMover = r.i16("unidad: move_graphics");
            r.saltar(2 + 4 + 1 + 2 + 1 + 4 + 1, "unidad: moving restante");
            r.saltar(2, "unidad: default_task_id");
            r.saltar(4, "unidad: search_radius");
            u.ritmoTrabajo = r.f32("unidad: work_rate");
            r.saltar(2 * 2, "unidad: drop_sites");
            r.saltar(1, "unidad: task_group");
            r.saltar(2 + 2, "unidad: command/stop_sound");
            r.saltar(1, "unidad: run_pattern");
            const uint16_t numComandos = r.u16("unidad: unit_command_count");
            for (uint16_t c = 0; c < numComandos; ++c) saltarComandoUnidad(r);
            break;
        }
        case 60: {
            u.velocidad = r.f32("unidad: speed");
            u.graficoMover = r.i16("unidad: move_graphics");
            r.saltar(2 + 4 + 1 + 2 + 1 + 4 + 1, "unidad: moving restante");
            r.saltar(2, "unidad: default_task_id");
            r.saltar(4, "unidad: search_radius");
            u.ritmoTrabajo = r.f32("unidad: work_rate");
            r.saltar(2 * 2, "unidad: drop_sites");
            r.saltar(1, "unidad: task_group");
            r.saltar(2 + 2, "unidad: command/stop_sound");
            r.saltar(1, "unidad: run_pattern");
            const uint16_t numComandos = r.u16("unidad: unit_command_count");
            for (uint16_t c = 0; c < numComandos; ++c) saltarComandoUnidad(r);
            r.saltar(1, "unidad: default_armor");
            r.saltar(1 + 1 + 1 + 1 + 1, "unidad: projectile_type/smart/drop/penetration/aoe");
            r.saltar(4, "unidad: projectile_arc");
            break;
        }
        case 70:
        case 80: {
            u.velocidad = r.f32("unidad: speed");
            u.graficoMover = r.i16("unidad: move_graphics");
            r.saltar(2 + 4 + 1 + 2 + 1 + 4 + 1, "unidad: moving restante");
            r.saltar(2, "unidad: default_task_id");
            r.saltar(4, "unidad: search_radius");
            u.ritmoTrabajo = r.f32("unidad: work_rate");
            r.saltar(2 * 2, "unidad: drop_sites");
            r.saltar(1, "unidad: task_group");
            r.saltar(2 + 2, "unidad: command/stop_sound");
            r.saltar(1, "unidad: run_pattern");
            const uint16_t numComandos = r.u16("unidad: unit_command_count");
            for (uint16_t c = 0; c < numComandos; ++c) saltarComandoUnidad(r);
            r.saltar(1, "unidad: default_armor");
            for (int i = 0; i < 3; ++i) {
                u.costos[i].tipo = r.i16("costo: type");
                u.costos[i].cantidad = r.i16("costo: amount");
                u.costos[i].activo = r.i16("costo: enabled");
            }
            r.saltar(4, "unidad: projectile_min_count");
            r.saltar(1, "unidad: projectile_max_count");
            r.saltar(4 * 3, "unidad: spawning_area w/l/rand");
            r.saltar(4, "unidad: projectile_id1");
            r.saltar(4, "unidad: special_graphic_id");
            r.saltar(1, "unidad: special_activation");
            r.saltar(2, "unidad: pierce_armor_displayed");
            if (tipo == 80) {
                u.graficoConstruccion = r.i16("unidad: construction_graphic_id");
                r.saltar(2, "unidad: construction_sound_id");
            }
            break;
        }
        default: {
            char b[64];
            snprintf(b, sizeof(b), "tipo de unidad desconocido %d (offset civ 0x%lx)", tipo,
                     static_cast<unsigned long>(r.pos() - inicioCiv));
            throw ErrorDatos{archivo + ": " + b};
        }
    }
    (void)tamTotal;
    return u;
}

} // namespace

ResumenDat leerDat(const uint8_t* datos, unsigned long tam, const std::string& nombreArchivo) {
    ResumenDat res;
    res.tamDescomprimido = tam;
    LectorLE r(datos, tam, 0, nombreArchivo);

    res.versionStr = r.cadena(8, "dat: versionstr");
    res.restriccionesTerreno = r.u16("dat: terrain_restriction_count");
    res.numTerrenos = r.u16("dat: terrain_count");
    if (res.restriccionesTerreno > 128 || res.numTerrenos > 128) {
        throw ErrorDatos{nombreArchivo + ": conteos de terreno imposibles (rest=" +
                         std::to_string(res.restriccionesTerreno) + ", ter=" +
                         std::to_string(res.numTerrenos) + ")"};
    }
    r.saltar(4u * res.restriccionesTerreno, "dat: float_ptr_terrain_tables");
    // TerrainRestriction x count: float[terrain_count] cada una (ROR).
    r.saltar(4u * res.numTerrenos * res.restriccionesTerreno, "dat: terrain_restrictions");

    // PlayerColor ROR: char[30] + id i16 + resource_id i16 + minimap u8 + type u8 = 36 B.
    res.numColoresJugador = r.u16("dat: player_color_count");
    if (res.numColoresJugador > 16) {
        throw ErrorDatos{nombreArchivo + ": player_color_count imposible (" +
                         std::to_string(res.numColoresJugador) + ")"};
    }
    r.saltar(36u * res.numColoresJugador, "dat: player_colors");

    // Sonidos ROR: sound_id i16, play_delay i16, file_count u16, cache_time i32,
    // SoundItem x file_count (char[13] + i32 + i16 = 19 B).
    res.numSonidos = r.u16("dat: sound_count");
    if (res.numSonidos > 8192) {
        throw ErrorDatos{nombreArchivo + ": sound_count imposible (" +
                         std::to_string(res.numSonidos) + ")"};
    }
    for (uint16_t s = 0; s < res.numSonidos; ++s) {
        r.i16("sound: sound_id");
        r.i16("sound: play_delay");
        const uint16_t fileCount = r.u16("sound: file_count");
        r.i32("sound: cache_time");
        r.saltar(static_cast<std::size_t>(fileCount) * 19, "sound: items");
    }

    // Graficos: conteo + punteros (los cuerpos van por offset y no se caminan
    // en M1: el motor los toma por id desde graphics.drs).
    res.numGraficos = r.u16("dat: graphic_count");
    if (res.numGraficos > 16384) {
        throw ErrorDatos{nombreArchivo + ": graphic_count imposible (" +
                         std::to_string(res.numGraficos) + ")"};
    }
    for (uint16_t g = 0; g < res.numGraficos; ++g) {
        const uint32_t ptr = r.u32("dat: graphic_ptrs");
        if (ptr != 0 && ptr >= tam) {
            throw ErrorDatos{nombreArchivo + ": graphic_ptr fuera de rango (" +
                             std::to_string(ptr) + " >= " + std::to_string(tam) + ")"};
        }
    }

    // Cabecera del mapa.
    r.saltar(4, "dat: virt_function_ptr");
    r.saltar(4, "dat: map_pointer");
    res.mapaAncho = r.i32("dat: map_width");
    res.mapaAlto = r.i32("dat: map_height");
    res.mundoAncho = r.i32("dat: world_width");
    res.mundoAlto = r.i32("dat: world_height");

    for (int i = 0; i < kTileSizes; ++i) {
        r.saltar(6, "dat: tile_sizes");
    }
    r.saltar(2, "dat: padding1");

    for (int t = 0; t < kTerrainsFijos; ++t) saltarTerrain(r);
    for (int b = 0; b < kTerrainBorders; ++b) saltarTerrainBorder(r);
    r.saltar(4, "dat: map_row_offset");

    // 13 int16: terrain_count_additional, borders_used, max_terrain, tile_width,
    // tile_height, tile_half_height, tile_half_width, elev_height, current_row,
    // current_column, block_beginn_row, block_end_row, block_begin/end_column.
    r.u16("dat: terrain_count_additional");
    r.u16("dat: borders_used");
    r.i16("dat: max_terrain");
    res.tileAncho = r.i16("dat: tile_width");
    res.tileAlto = r.i16("dat: tile_height");
    res.tileMediaAlto = r.i16("dat: tile_half_height");
    res.tileMediaAncho = r.i16("dat: tile_half_width");
    res.alturaElevacion = r.i16("dat: elev_height");
    r.saltar(2 * 6, "dat: current_row..block_end_column");

    r.saltar(4 + 4 + 4, "dat: any_frame_change/search_map_ptr/rows_ptr");
    r.saltar(1 + 1, "dat: map_visible_flag/fog_flag");
    r.saltar(2, "dat: terrain_blob0");
    r.saltar(4 * 5, "dat: terrain_blob1");

    // Random maps: MapInfo (18 x i32 = 72 B) + Map (14 x i32 = 56 B + listas).
    res.numMapas = r.u32("dat: random_map_count");
    r.u32("dat: random_map_ptr");
    if (res.numMapas > 64) {
        throw ErrorDatos{nombreArchivo + ": random_map_count imposible (" +
                         std::to_string(res.numMapas) + ")"};
    }
    for (uint32_t m = 0; m < res.numMapas; ++m) {
        r.saltar(4 * 18, "mapinfo: fijo");
    }
    for (uint32_t m = 0; m < res.numMapas; ++m) {
        r.saltar(4 * 9, "map: fijo");  // borders x4 + usage/water/base/coverage/unused
        uint32_t baseZonas = r.u32("map: base_zone_count");
        r.saltar(4, "map: base_zone_ptr");
        uint32_t mapTerr = r.u32("map: map_terrain_count");
        r.saltar(4, "map: map_terrain_ptr");
        uint32_t mapUnid = r.u32("map: map_unit_count");
        r.saltar(4, "map: map_unit_ptr");
        uint32_t mapElev = r.u32("map: map_elevation_count");
        r.saltar(4, "map: map_elevation_ptr");
        if (baseZonas > 256 || mapTerr > 256 || mapUnid > 256 || mapElev > 256) {
            throw ErrorDatos{nombreArchivo + ": conteos de mapa aleatorio imposibles (" +
                             std::to_string(baseZonas) + "/" + std::to_string(mapTerr) + "/" +
                             std::to_string(mapUnid) + "/" + std::to_string(mapElev) + ")"};
        }
        r.saltar(44u * baseZonas, "map: base_zones (MapLand)");
        r.saltar(24u * mapTerr, "map: terrains (MapTerrain)");
        r.saltar(44u * mapUnid, "map: units (MapUnit)");
        r.saltar(24u * mapElev, "map: elevations (MapElevation)");
    }

    // Paquetes de efectos (ROR): nombre char[31] + conteo + efectos de 11 B.
    res.numPaquetesEfectos = r.u32("dat: effect_bundle_count");
    if (res.numPaquetesEfectos > 8192) {
        throw ErrorDatos{nombreArchivo + ": effect_bundle_count imposible (" +
                         std::to_string(res.numPaquetesEfectos) + ")"};
    }
    for (uint32_t e = 0; e < res.numPaquetesEfectos; ++e) {
        r.saltar(31, "effect_bundle: name");
        const uint16_t nEfectos = r.u16("effect_bundle: effect_count");
        r.saltar(11u * nEfectos, "effect_bundle: effects");
    }

    // (ROR: sin unit_headers) -> civilizaciones con la tabla de unidades.
    res.numCivs = r.u16("dat: civ_count");
    if (res.numCivs == 0 || res.numCivs > 32) {
        throw ErrorDatos{nombreArchivo + ": civ_count imposible (" +
                         std::to_string(res.numCivs) + ")"};
    }
    for (uint16_t c = 0; c < res.numCivs; ++c) {
        Civilizacion civ;
        const unsigned long inicioCiv = r.pos();
        civ.tipoJugador = static_cast<int8_t>(r.u8("civ: player_type"));
        civ.nombre = r.cadena(20, "civ: name");
        civ.numRecursos = r.u16("civ: resources_count");
        if (civ.numRecursos > 512) {
            throw ErrorDatos{nombreArchivo + ": civ \"" + civ.nombre +
                             "\" con resources_count imposible (" +
                             std::to_string(civ.numRecursos) + ")"};
        }
        r.saltar(2, "civ: tech_tree_id");
        r.saltar(4u * civ.numRecursos, "civ: resources");
        r.saltar(1, "civ: icon_set");
        civ.numUnidades = r.u16("civ: unit_count");
        if (civ.numUnidades > 4096) {
            throw ErrorDatos{nombreArchivo + ": civ \"" + civ.nombre +
                             "\" con unit_count imposible (" + std::to_string(civ.numUnidades) +
                             ")"};
        }
        std::vector<int32_t> offsets(civ.numUnidades);
        for (uint16_t u = 0; u < civ.numUnidades; ++u) {
            offsets[u] = r.i32("civ: unit_offsets");
        }
        // Las unidades viven en civ_inicio + offset (offset 0 = hueco vacio).
        for (uint16_t u = 0; u < civ.numUnidades; ++u) {
            const int32_t off = offsets[u];
            if (off <= 0) continue;
            const std::size_t destino = inicioCiv + static_cast<std::size_t>(off);
            if (destino >= tam) {
                throw ErrorDatos{nombreArchivo + ": unit_offset fuera de rango en civ \"" +
                                 civ.nombre + "\" (" + std::to_string(off) + ")"};
            }
            r.buscar(destino, "civ: salto a unidad");
            const int8_t tipo = static_cast<int8_t>(r.u8("unidad: unit_type"));
            civ.unidades.push_back(
                leerUnidad(r, tipo, inicioCiv, tam, nombreArchivo));
        }
        res.civs.push_back(std::move(civ));
    }

    res.numInvestigaciones = r.u16("dat: research_count");
    // La cola (investigaciones, sonido final, aleatoriedad) no aporta a M1:
    // se deja sin caminar y se reporta.
    res.bytesSinParsear = tam - r.pos();
    return res;
}

} // namespace assets
