#!/usr/bin/env bash
# Investiga la última release estable de SDL2 con asset de tarball fuente,
# la descarga, calcula SHA-256 y extrae para inspección local.
set -euo pipefail

REPO_DIR="/home/z/my-project/aoe-android"
THIRD="$REPO_DIR/third_party"
mkdir -p "$THIRD"
cd "$THIRD"

SELECCION=$(python3 - <<'PY'
import json, urllib.request, os
token = open("/home/z/my-project/.ghtoken").read().strip()
url = "https://api.github.com/repos/libsdl-org/SDL/releases?per_page=60"
req = urllib.request.Request(url, headers={"User-Agent": "aoe-mobile-probe", "Authorization": f"token {token}"})
data = json.load(urllib.request.urlopen(req))
best = None
for r in data:
    tag = r.get("tag_name", "")
    if r.get("draft") or r.get("prerelease"):
        continue
    if not tag.startswith("release-2."):
        continue  # la spec pide SDL2
    ver = tag.replace("release-", "")
    asset = next((a["browser_download_url"] for a in r.get("assets", []) if a["name"] == f"SDL2-{ver}.tar.gz"), None)
    if asset:
        best = (tag, ver, asset)
        break
print("|".join(best))
PY
)
TAG=$(echo "$SELECCION" | cut -d'|' -f1)
VER=$(echo "$SELECCION" | cut -d'|' -f2)
URL=$(echo "$SELECCION" | cut -d'|' -f3)
echo "elegida: tag=$TAG version=$VER url=$URL"

TARBALL="SDL2-${VER}.tar.gz"
if [ ! -d "SDL2-${VER}" ]; then
  echo "descargando $URL ..."
  curl -fsSL -o "$TARBALL" "$URL"
  tar -xzf "$TARBALL"
fi

echo "== sha256 =="
sha256sum "$TARBALL"

echo "== cmake minimo requerido por SDL =="
grep -m1 "cmake_minimum_required" "SDL2-${VER}/CMakeLists.txt"

echo "== archivos java de SDL =="
ls "SDL2-${VER}/android-project/app/src/main/java/org/libsdl/app/"

echo "== opciones cmake utiles presentes =="
grep -oE "option\(SDL_(AUDIO|VIDEO|RENDER|EVENTS|JOYSTICK|HAPTIC|POWER|TIMERS|FILE|CPUINFO|DLOPEN|TESTS|TEST_LIBRARY|SHARED|STATIC|X11|WAYLAND|OPENGL|OPENGLES|VULKAN|PTHREADS|LIBC)[A-Z_]*" "SDL2-${VER}/CMakeLists.txt" | sort -u | head -40
