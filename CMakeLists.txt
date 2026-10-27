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

set(AOE_SOURCES
  src/main.cpp
  src/platform/log.cpp
  src/platform/metrics.cpp
  src/render/framebuffer.cpp
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
target_link_libraries(${AOE_TARGET} PRIVATE ${AOE_SDL_LIB})
if(TARGET SDL2::SDL2main)
  target_link_libraries(${AOE_TARGET} PRIVATE SDL2::SDL2main)
endif()
