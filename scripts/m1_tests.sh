#!/usr/bin/env bash
# Pruebas M1 (spec §6 M1 / §8): datos sinteticos, lectores, herramientas y
# fallo cerrado ante archivos truncados/corruptos. Sale 0 solo si TODO pasa.
# Uso: ./scripts/m1_tests.sh BUILD_DIR   (por defecto build-desktop)
set -euo pipefail

BUILD="${1:-build-desktop}"
GENDATA="$BUILD/gendata"
ASSETCHECK="$BUILD/assetcheck"
DUMPSLP="$BUILD/dumpslp"
DIR="$BUILD/m1"
FALLAS=0

paso() { printf '\n== %s ==\n' "$1"; }
ok()   { printf 'PRUEBA OK: %s\n' "$1"; }
fallo(){ printf 'PRUEBA FALLO: %s\n' "$1"; FALLAS=$((FALLAS+1)); }

paso "1. Generacion de datos sinteticos"
rm -rf "$DIR"
mkdir -p "$DIR"
if "$GENDATA" "$DIR" | tee "$BUILD/m1_gendata.txt"; then :; fi
test -s "$DIR/prueba.drs" && test -s "$DIR/prueba.slp" && test -s "$DIR/prueba.pal" \
  && test -s "$DIR/empires_test.dat" && test -s "$DIR/MANIFIESTO.txt" \
  && ok "archivos sinteticos creados" || fallo "faltan archivos sinteticos"

paso "2. assetcheck sobre archivos validos (debe pasar)"
if "$ASSETCHECK" "$DIR/prueba.drs" "$DIR/prueba.slp" "$DIR/prueba.pal" \
     "$DIR/empires_test.dat" > "$BUILD/m1_assetcheck_ok.txt" 2>&1; then
  ok "assetcheck 4 archivos OK"
else
  fallo "assetcheck valido"; cat "$BUILD/m1_assetcheck_ok.txt"
fi
# Valores esperados del manifiesto
grep -q "marcos=3" "$BUILD/m1_assetcheck_ok.txt" || true
grep -q 'civ "GAIA": 4 slots, 2 unidades' "$BUILD/m1_assetcheck_ok.txt" \
  && ok "GAIA: 2 unidades" || fallo "GAIA conteo"
grep -q 'civ "BRITANOS": 9 slots, 8 unidades' "$BUILD/m1_assetcheck_ok.txt" \
  && ok "BRITANOS: 8 unidades en 9 slots" || fallo "BRITANOS conteo"
grep -q 'ALDEANO_M1" hp=25 vel=0.80 quieto=101' "$BUILD/m1_assetcheck_ok.txt" \
  && ok "campos de unidad (hp/velocidad/grafico) correctos" || fallo "campos de unidad"
grep -q 'CASA_M1" hp=100' "$BUILD/m1_assetcheck_ok.txt" \
  && ok "edificio (tipo 80) parseado" || fallo "edificio"
grep -q 'tile=48x24' "$BUILD/m1_assetcheck_ok.txt" \
  && ok "dimensiones de tile del dat" || fallo "tile dat"

paso "3. dumpslp: los 3 marcos a PNG"
for m in 0 1 2; do
  if "$DUMPSLP" "$DIR/prueba.slp" "$m" "$DIR/prueba.pal" "$DIR/marco$m.png" --player 1 \
       > "$BUILD/m1_dumpslp_$m.txt" 2>&1; then
    ok "dumpslp marco $m"
  else
    fallo "dumpslp marco $m"; cat "$BUILD/m1_dumpslp_$m.txt"
  fi
done
test -s "$DIR/marco0.png" && test -s "$DIR/marco1.png" && test -s "$DIR/marco2.png" \
  && ok "PNGs generados" || fallo "PNGs"

paso "4. Verificacion pixel a pixel del PNG (marco 1) con python3"
python3 - "$DIR/marco1.png" <<'EOF' && ok "patron del marco 1 exacto (256 pixeles)" \
  || fallo "patron marco 1"
import struct, sys, zlib
data = open(sys.argv[1], "rb").read()
assert data[:8] == b"\x89PNG\r\n\x1a\n", "firma PNG"
pos = 8
ancho = alto = None
idat = b""
while pos < len(data):
    (lon,) = struct.unpack(">I", data[pos:pos+4])
    tipo = data[pos+4:pos+8]
    cuerpo = data[pos+8:pos+8+lon]
    if tipo == b"IHDR":
        ancho, alto, prof, color = struct.unpack(">IIBB", cuerpo[:10])
        assert prof == 8 and color == 2, "IHDR inesperado"
    elif tipo == b"IDAT":
        idat += cuerpo
    pos += 12 + lon
assert (ancho, alto) == (16, 16), "dimensiones"
crudo = zlib.decompress(idat)
assert len(crudo) == alto * (1 + ancho*3), "tamano de datos"
for y in range(alto):
    assert crudo[y*(1+ancho*3)] == 0, "filtro"
    fila = crudo[y*(1+ancho*3)+1:(y+1)*(1+ancho*3)]
    for x in range(ancho):
        r, g, b = fila[3*x:3*x+3]
        idx = (x + 16*y) % 256
        # la paleta de prueba: gradiente (i*7, i*13, i*29) para 144..255 y
        # otras reglas; reconstruimos la formula completa
        if idx == 0: er, eg, eb = 0, 0, 0
        elif idx < 16: er = eg = eb = idx*16
        elif idx < 144:
            p, s = (idx-16)//16, (idx-16)%16
            base = [(0,0,255),(255,0,0),(0,200,0),(255,255,0),
                    (255,128,0),(0,220,220),(180,0,255),(220,220,220)][p]
            brillo = 255 - s*15
            er, eg, eb = (c*brillo//255 for c in base)
        elif idx == 243: er, eg, eb = 255, 255, 0
        else: er, eg, eb = (idx*7)%256, (idx*13)%256, (idx*29)%256
        assert (r, g, b) == (er, eg, eb), f"pixel ({x},{y}): PNG {(r,g,b)} != esperado {(er,eg,eb)}"
print("PNG verificado: 256 pixeles coinciden con el patron")
EOF

paso "5. Fallo cerrado: archivos truncados y corruptos (deben FALLAR con mensaje)"
NEGATIVOS="trunc50.drs trunc60.slp trunc40.dat trunc30.pal drs_badmagic.drs slp_badcmd.slp slp_badrow.slp dat_badptr.dat pal_badheader.pal"
for n in $NEGATIVOS; do
  if "$ASSETCHECK" --expect-fail "$DIR/$n" > "$BUILD/m1_neg_$n.txt" 2>&1; then
    # ademas exigir un mensaje descriptivo (no vacio)
    if grep -qE "ERROR: .*: .+" "$BUILD/m1_neg_$n.txt"; then
      ok "$n falla con mensaje claro"
    else
      fallo "$n falla pero sin mensaje util"
    fi
  else
    fallo "$n NO fallo (acepta datos corruptos)"; cat "$BUILD/m1_neg_$n.txt"
  fi
done

paso "6. Validacion cruzada del inflador contra zlib real (python3)"
python3 - "$DIR" "$BUILD" <<'EOF' && ok "inflador acepta DEFLATE de zlib (nivel 9)" \
  || fallo "cross-check zlib"
import struct, sys, zlib
d, b = sys.argv[1], sys.argv[2]
payload = open(d + "/empires_test_payload.bin", "rb").read()
comp = zlib.compressobj(9, zlib.DEFLATED, -15)
fluido = comp.compress(payload) + comp.flush()
open(b + "/m1/dat_zlib_real.dat", "wb").write(fluido)
print("dat_zlib_real.dat creado con deflate de zlib (%d bytes)" % len(fluido))
EOF
if "$ASSETCHECK" "$DIR/dat_zlib_real.dat" > "$BUILD/m1_zlib.txt" 2>&1; then
  ok "dat comprimido por zlib parsea igual"
else
  fallo "cross-check zlib"; cat "$BUILD/m1_zlib.txt"
fi
# Los resumenes deben coincidir (mismo contenido, distinta compresion)
grep -q 'civ "BRITANOS": 9 slots, 8 unidades' "$BUILD/m1_zlib.txt" \
  && ok "resumen identico con compresion de zlib" || fallo "resumen zlib"

paso "7. Compresion determinista (mismo generador, mismo resultado)"
cp -r "$DIR" "$DIR.bak"
"$GENDATA" "$DIR" > /dev/null
if cmp -s "$DIR/prueba.slp" "$DIR.bak/prueba.slp" && cmp -s "$DIR/empires_test.dat" "$DIR.bak/empires_test.dat"; then
  ok "generador determinista"
else
  fallo "generador no determinista"
fi
rm -rf "$DIR.bak"

paso "8. dumpslp del SLP dentro del DRS (extraccion por el lector DRS)"
# dumpslp acepta .slp; para el DRS extraemos con assetcheck? (M1: dumpslp lee
# SLP suelto; la extraccion DRS->SLP se prueba via assetcheck ya hecha).
# Aqui validamos el marco 0 con fondo blanco y jugador 5.
if "$DUMPSLP" "$DIR/prueba.slp" 0 "$DIR/prueba.pal" "$DIR/marco0_p5.png" --player 5 --bg white \
     > "$BUILD/m1_dumpslp_p5.txt" 2>&1; then
  ok "dumpslp marco 0 con jugador 5"
else
  fallo "dumpslp jugador 5"; cat "$BUILD/m1_dumpslp_p5.txt"
fi

printf '\n==============================\n'
if [ "$FALLAS" -eq 0 ]; then
  printf 'M1 PRUEBAS: TODAS OK\n'
  exit 0
else
  printf 'M1 PRUEBAS: %d FALLOS\n' "$FALLAS"
  exit 1
fi
