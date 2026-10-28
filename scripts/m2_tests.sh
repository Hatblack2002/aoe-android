#!/bin/bash
# Pruebas M2 - mapa visible (spec §6 M2). Todo con datos sinteticos propios;
# nada del juego entra al repositorio (spec §8).
#
# Cobertura:
#   1. gendata genera el fixture (incluido empires_test.dat para el tamano de mosaico)
#   2. demo M2 con --dat: mapa 64x64, 4 terrenos, tile 48x24 LEIDO DEL DAT,
#      camara que recorre y choca con los limites, mosaicos dibujados, PNG ok
#   3. verificaciones del registro (anti-pantalla-negra incluida)
#   4. PNG de evidencia: cabecera 960x540 + tamano no trivial
#   5. determinismo: dos ejecuciones -> PNG byte a byte identico
#   6. guarda anti-negro: con el dibujo suprimido el binario sale con error
#   7. --dat apuntando a un archivo corrupto -> fallo con causa explicita
set -u

BUILD="${1:-build-desktop}"
DIR="$BUILD/m2"
AOE="$BUILD/aoe"
FALLOS=0

paso() { echo ""; echo "== M2: $1 =="; }
fallo() { echo "  FALLO: $1"; FALLOS=$((FALLOS+1)); }
ok() { echo "  ok: $1"; }

paso "1. fixture sintetico (gendata)"
rm -rf "$DIR"
mkdir -p "$DIR" "$BUILD/m2run1" "$BUILD/m2run2"
if "$BUILD/gendata" "$DIR" > "$BUILD/m2_gendata.txt" 2>&1; then
  ok "gendata: $(tail -1 "$BUILD/m2_gendata.txt")"
else
  fallo "gendata fallo"; cat "$BUILD/m2_gendata.txt"; exit 1
fi

paso "2. demo M2 con --dat sintetico"
if SDL_VIDEODRIVER=dummy "$AOE" --m2-demo --frames 40 \
     --screenshot "$BUILD/m2_mapa.bmp" --dat "$DIR/empires_test.dat" \
     --logdir "$BUILD/m2run1" > "$BUILD/m2_demo.txt" 2>&1; then
  ok "binario termino con exit 0"
else
  fallo "demo M2 termino mal"; tail -20 "$BUILD/m2_demo.txt"; exit 1
fi

paso "3. verificaciones del registro"
LOG="$BUILD/m2run1/game.log"
grep -q "mapa de prueba 64x64 (4096 tiles)" "$LOG" \
  && ok "mapa 64x64 en memoria (4096 tiles)" \
  || fallo "no aparece el mapa 64x64"
for t in pasto agua orilla tierra; do
  grep -q "$t=[1-9]" "$LOG" && ok "terreno presente: $t" || fallo "falta terreno $t"
done
grep -q "tile leido del dat: 48x24 (mitad 24x12" "$LOG" \
  && ok "tile 48x24 (mitad 24x12) LEIDO DEL DAT, no de memoria" \
  || fallo "el tile no se leyo del dat"
grep -qE "MOSAICOS.*dibujados=0( |$)|FATAL: 0 mosaicos" "$LOG" \
  && fallo "hay cuadros con 0 mosaicos dibujados" \
  || ok "todos los cuadros dibujaron mosaicos"
grep -q "ajustes de camara por limites: [1-9]" "$LOG" \
  && ok "camara ajustada por los limites (clamp activo)" \
  || fallo "la camara nunca toco los limites"
grep -q "fallidos=0" "$LOG" \
  && ok "0 mosaicos fallidos con causa registrada" \
  || fallo "hubo mosaicos fallidos"
grep -q "pixel central RGB(" "$LOG" \
  && ! grep -q "FATAL: el pixel central es el color de fondo" "$LOG" \
  && ok "verificacion anti-pantalla-negra: pixel central != fondo" \
  || fallo "pixel central sospechoso de pantalla negra"
grep -q "veredicto OK" "$LOG" && ok "veredicto del motor: OK" || fallo "sin veredicto OK"
grep -q "evidencia PNG .* (ok)" "$LOG" && ok "PNG de evidencia escrito" || fallo "PNG no escrito"

paso "4. PNG de evidencia"
PNG="$BUILD/m2_mapa.bmp.png"
test -s "$PNG" && ok "PNG existe ($(stat -c%s "$PNG") bytes)" || fallo "PNG vacio o ausente"
python3 - "$PNG" <<'EOF'
import struct, sys
d = open(sys.argv[1], "rb").read()
assert d[:8] == b"\x89PNG\r\n\x1a\n", "no es PNG"
w, h = struct.unpack(">II", d[16:24])
assert (w, h) == (960, 540), f"dimensiones inesperadas {w}x{h}"
print(f"  ok: cabecera PNG valida 960x540 ({len(d)} bytes)")
EOF
[ $? -eq 0 ] || fallo "cabecera PNG invalida"

paso "5. determinismo (dos ejecuciones -> mismo PNG)"
SDL_VIDEODRIVER=dummy "$AOE" --m2-demo --frames 40 \
  --screenshot "$BUILD/m2_b.bmp" --dat "$DIR/empires_test.dat" \
  --logdir "$BUILD/m2run2" > /dev/null 2>&1
if cmp -s "$PNG" "$BUILD/m2_b.bmp.png"; then
  ok "PNG identico byte a byte en dos ejecuciones"
else
  fallo "el render no es determinista"
fi

paso "6. guarda anti-pantalla-negra (dibujo suprimido -> error)"
rm -rf "$BUILD/m2run_neg"; mkdir -p "$BUILD/m2run_neg"
set +e
AOE_M2_TEST_NEGRO=1 SDL_VIDEODRIVER=dummy "$AOE" --m2-demo --frames 10 \
  --screenshot "$BUILD/m2_negro.bmp" --dat "$DIR/empires_test.dat" \
  --logdir "$BUILD/m2run_neg" > /dev/null 2>&1
NEG=$?
set -e
if [ "$NEG" -ne 0 ] && grep -q "FATAL: 0 mosaicos dibujados" "$BUILD/m2run_neg/game.log"; then
  ok "sin dibujo -> exit $NEG con FATAL y causa (nunca negro callado)"
else
  fallo "la guarda anti-negro no disparo (exit=$NEG)"
fi

paso "7. --dat corrupto -> fallo con causa explicita"
head -c 400 "$DIR/empires_test.dat" > "$DIR/dat_truncado.dat"
rm -rf "$BUILD/m2run_bad"; mkdir -p "$BUILD/m2run_bad"
set +e
SDL_VIDEODRIVER=dummy "$AOE" --m2-demo --frames 5 \
  --screenshot "$BUILD/m2_bad.bmp" --dat "$DIR/dat_truncado.dat" \
  --logdir "$BUILD/m2run_bad" > /dev/null 2>&1
BAD=$?
set -e
if [ "$BAD" -ne 0 ] && grep -q "FATAL: --dat" "$BUILD/m2run_bad/game.log"; then
  ok "dat truncado rechazado (exit=$BAD) con causa en el registro"
else
  fallo "el dat truncado no fue rechazado (exit=$BAD)"
fi

echo ""
if [ "$FALLOS" -eq 0 ]; then
  echo "M2: TODAS LAS PRUEBAS OK"
  exit 0
else
  echo "M2: $FALLOS PRUEBA(S) FALLARON"
  exit 1
fi
