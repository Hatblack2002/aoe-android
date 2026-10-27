#!/usr/bin/env bash
# Descarga SDL2 con versión y SHA-256 FIJADOS (nada de "última versión" a ciegas),
# lo extrae en third_party/ y copia los archivos Java de SDL al proyecto Android.
# El repositorio nunca versiona el código de SDL; se recupera en cada build de CI.
set -euo pipefail

SDL_VERSION="2.32.10"
SDL_SHA256="5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165"
SDL_URL="https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VERSION}/SDL2-${SDL_VERSION}.tar.gz"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
THIRD="$ROOT/third_party"
mkdir -p "$THIRD"
cd "$THIRD"

if [ ! -d "SDL2-${SDL_VERSION}" ]; then
  echo "[fetch_sdl] descargando SDL2 ${SDL_VERSION} ..."
  curl -fsSL -o "SDL2-${SDL_VERSION}.tar.gz" "$SDL_URL"
  echo "[fetch_sdl] verificando SHA-256 ..."
  echo "${SDL_SHA256}  SDL2-${SDL_VERSION}.tar.gz" | sha256sum -c -
  tar -xzf "SDL2-${SDL_VERSION}.tar.gz"
else
  echo "[fetch_sdl] SDL2 ${SDL_VERSION} ya presente"
fi

# Enlace estable para CMake: third_party/SDL
ln -sfn "SDL2-${SDL_VERSION}" "$THIRD/SDL"

# SDL trae su propia plantilla android-project con un gradle-wrapper.jar que
# NUNCA usamos (el CI instala Gradle via setup-gradle). Se retira para que la
# validacion de wrappers del CI no lo escanee y no ensucie el workspace.
rm -rf "SDL2-${SDL_VERSION}/android-project/gradle" \
       "SDL2-${SDL_VERSION}/android-project/gradlew" \
       "SDL2-${SDL_VERSION}/android-project/gradlew.bat"

# Archivos Java que SDLActivity necesita dentro del proyecto Android
JAVA_SRC="$THIRD/SDL2-${SDL_VERSION}/android-project/app/src/main/java/org/libsdl/app"
JAVA_DST="$ROOT/android-project/app/src/main/java/org/libsdl/app"
mkdir -p "$JAVA_DST"
cp -v "$JAVA_SRC"/*.java "$JAVA_DST/"

echo "[fetch_sdl] SDL2 ${SDL_VERSION} lista: third_party/SDL -> $(readlink "$THIRD/SDL")"
