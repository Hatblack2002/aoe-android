cmake_minimum_required(VERSION 3.22)
project(aoe_mobile LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT ANDROID)
  add_compile_options(-Wall -Wextra)
endif()

# SDL2 se descarga con scripts/fetch_sdl.sh (version y SHA-256 fijados).
add_subdirectory(third_party/SDL EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------
# Capa assets (spec §4): lectores de DRS/SLP/paleta/dat, inflador
# DEFLATE, escritor PNG y generador de datos sinteticos.
# NO depende de SDL ni de platform (a proposito).
# ---------------------------------------------------------------
add_library(aoe_assets STATIC
  src/assets/formato.cpp
  src/assets/inflate.cpp
  src/assets/png.cpp
  src/assets/paleta.cpp
  src/assets/slp.cpp
  src/assets/drs.cpp
  src/assets/dat.cpp
  src/assets/gen.cpp
  src/assets/chequeo.cpp
)
target_include_directories(aoe_assets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

set(AOE_SOURCES
  src/main.cpp
  src/game/mapa.cpp
  src/platform/log.cpp
  src/platform/metrics.cpp
  src/render/framebuffer.cpp
  src/render/isometrico.cpp
)

# SDL expone distintos nombres de target segun version/plataforma.
set(AOE_SDL_LIB "")
if(TARGET SDL2::SDL2)
  set(AOE_SDL_LIB SDL2::SDL2)
elseif(TARGET SDL2)
  set(AOE_SDL_LIB SDL2)
else()
  set(AOE_SDL_LIB SDL2::SDL2-static)
endif()

if(ANDROID)
  # Android: SDLActivity carga libmain.so (nombre por defecto de SDL).
  add_library(main SHARED ${AOE_SOURCES})
  set(AOE_TARGET main)
else()
  # Escritorio: binario para autopruebas sin ventana (CI).
  add_executable(aoe ${AOE_SOURCES})
  set(AOE_TARGET aoe)
endif()

target_include_directories(${AOE_TARGET} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(${AOE_TARGET} PRIVATE aoe_assets ${AOE_SDL_LIB})
if(TARGET SDL2::SDL2main)
  target_link_libraries(${AOE_TARGET} PRIVATE SDL2::SDL2main)
endif()

# ---------------------------------------------------------------
# Herramientas de inspeccion y prueba (spec §8). Solo escritorio:
# en Android sus funciones corren dentro de la app.
# ---------------------------------------------------------------
if(NOT ANDROID)
  add_executable(assetcheck tools/assetcheck.cpp)
  target_link_libraries(assetcheck PRIVATE aoe_assets)

  add_executable(dumpslp tools/dumpslp.cpp)
  target_link_libraries(dumpslp PRIVATE aoe_assets)

  add_executable(gendata tools/gendata.cpp)
  target_link_libraries(gendata PRIVATE aoe_assets)
endif()
