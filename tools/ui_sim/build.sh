#!/usr/bin/env bash
# Copyright (c) 2026 DankBuild - RadarAura. RadarAura Build Licence 1.0 - see LICENSE.md
# Build + run the host renderer, write PNGs into ./out.
# Needs a firmware build first (for managed_components/lvgl__lvgl and sdkconfig.h).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
GD="$HERE/../../firmware"
BUILD_DIR="${BUILD_DIR:-$GD/build}"
LV="$GD/managed_components/lvgl__lvgl"
OUT="$HERE/out"; mkdir -p "$OUT/obj"
CFLAGS=(-O1 -w -DLV_CONF_KCONFIG_EXTERNAL_INCLUDE='"sim_kconfig.h"'
        -DSIM_SDKCONFIG="\"$BUILD_DIR/config/sdkconfig.h\""
        -I"$HERE" -I"$LV" -I"$LV/src" -I"$GD/main")
objs=()
while IFS= read -r f; do
  o="$OUT/obj/$(echo "${f#$LV/}" | tr / _).o"
  [[ "$o" -nt "$f" ]] || gcc "${CFLAGS[@]}" -c "$f" -o "$o" &
  objs+=("$o")
done < <(find "$LV/src" -name '*.c')
wait
gcc "${CFLAGS[@]}" "$HERE/sim.c" "$GD/main/ui.c" "${objs[@]}" -lm -o "$OUT/ui_sim"
cd "$OUT" && ./ui_sim && python3 - <<'PY'
import glob, os
from PIL import Image
for p in sorted(glob.glob("*.ppm")):
    Image.open(p).save(p[:-4] + ".png"); os.remove(p)
PY
ls "$OUT"/*.png
