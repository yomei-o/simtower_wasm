#!/bin/sh
# Turn a SIMTOWER.EXE into the resource pack the port expects.
#
# Four upstream tools in a row.  Note the third argument to build_resource_pack:
# the catalog names resources by their raw filename, so the asset directory it
# wants is the raw extraction, not the catalogued one.
#
# Verified against the 16-bit Windows 3.1 SIMTOWER.EXE: 499 resources, 6089216
# bytes packed.
set -e
EXE=${1:?usage: build_assets.sh /path/to/SIMTOWER.EXE [upstream-dir]}
UP=${2:-upstream}
OUT=$UP/original
GEN=$UP/port/generated

mkdir -p "$OUT" "$GEN"
python "$UP/tools/inspect_ne.py"            "$EXE" --extract-resources "$OUT/raw" --json "$OUT/ne_report.json"
python "$UP/tools/decode_win16_resources.py" "$OUT/raw" "$OUT/decoded"
python "$UP/tools/catalog_assets.py"         "$OUT/ne_report.json" "$OUT/raw" "$OUT/assets"
python "$UP/tools/build_resource_pack.py"    "$OUT/assets/catalog.json" "$OUT/raw" \
        "$GEN/original_resources.pack" \
        "$GEN/original_resources.generated.hpp" \
        "$GEN/original_resources.rc.in"
echo "resource pack in $GEN"
