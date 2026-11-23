#!/usr/bin/env bash
# Generates a deliberately out-of-tune synthetic sung phrase, renders it through
# NovaPitch with several settings and plots the pitch before/after.
#   ./tools/demo/run_demo.sh [build-dir] [out-dir]
# Needs: python3 with numpy + matplotlib, and a build of NovaPitchRender.
set -euo pipefail
BUILD="${1:-build}"
OUT="${2:-demo-out}"
HERE="$(cd "$(dirname "$0")" && pwd)"
RENDER="$(find "$BUILD/tools" -name 'NovaPitchRender*' -type f \( -perm -u+x -o -name '*.exe' \) | head -1)"
RENDER="$(cd "$(dirname "$RENDER")" && pwd)/$(basename "$RENDER")"
mkdir -p "$OUT"
cd "$OUT"
python3 "$HERE/make_voice.py" 1_original_desafinado.wav
"$RENDER" 1_original_desafinado.wav 2_natural.wav key=9 scale=1 retuneSpeed=60 naturalHold=50
"$RENDER" 1_original_desafinado.wav 3_hard_tune.wav key=9 scale=1 retuneSpeed=0
"$RENDER" 1_original_desafinado.wav 4_transpuesto_+12.wav key=9 scale=1 retuneSpeed=30 transpose=12
"$RENDER" 1_original_desafinado.wav 5_transpuesto_-7.wav key=9 scale=1 retuneSpeed=30 transpose=-7
python3 "$HERE/plot_pitch.py" comparacion_pitch.png | tee resumen.txt
