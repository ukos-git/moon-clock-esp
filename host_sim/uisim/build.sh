#!/usr/bin/env bash
# UI simulator build: the REAL device screens.c + styles.c + fonts + the
# vendored LVGL 9.6 (SW, RGB565, 466x466) + the portable moon core +
# generated host assets, compiled for the host.
#
# Output:  host_sim/uisim/build/uisim
# Objects: host_sim/uisim/build/obj/<repo-relative path>.o (incremental)
#
# Usage:  ./build.sh                build only
#         ./build.sh run [screen ..]  build, then snapshot PNGs into out/
#         --panel N  render for the N x N panel (default 466; the P4's
#                    720 builds into its own build-<N>/ dir so the object
#                    cache never mixes panel sizes)
set -euo pipefail
cd "$(dirname "$0")"   # host_sim/uisim

# Parse --panel out of the arguments (any position; "run" stays in ARGS).
PANEL=466
ARGS=()
while [ $# -gt 0 ]; do
    case "$1" in
    --panel)
        PANEL="${2:?--panel needs a size}"
        shift 2
        ;;
    *)
        ARGS+=("$1")
        shift
        ;;
    esac
done
set -- "${ARGS[@]}"

ROOT="$(cd ../.. && pwd)"
LV="$ROOT/managed_components/lvgl__lvgl"
MOON="$ROOT/components/moon"
UI="$ROOT/components/ui"
BUILD="$PWD/build"
JOBS="$(nproc)"

# The font set and the moon asset target follow the panel (the device
# CMakeLists do the same switch on CONFIG_BOARD_P4): 466 -> fonts/ + the
# S3's 864x432 texture, anything else -> fonts_p4/ + the P4's 2048x1024
# texture (out/<t>/moon_assets_gen.c + generated/<t>/moon_consts.h). The
# symbol names are identical, screens.c is shared.
FONT_DIR="$UI/fonts"
MOON_ASSET_TARGET=s3
if [ "$PANEL" != 466 ]; then
    FONT_DIR="$UI/fonts_p4"
    MOON_ASSET_TARGET=p4
fi
MOON_GEN_C="$ROOT/assets/images/moon/out/$MOON_ASSET_TARGET/moon_assets_gen.c"
if [ ! -f "$MOON_GEN_C" ]; then
    echo "missing $MOON_GEN_C -- regenerate: assets/images/moon/.venv/bin/python -m moon.assets --target $MOON_ASSET_TARGET" >&2
    exit 1
fi

CC=${CC:-gcc}
# NOTE: the device screens.c uses an implicit atoi declaration (works in the
# IDF build; flagged here). The sim compiles the device code UNMODIFIED, so
# implicit-decl stays a warning, not an error.
# -DPANEL_SIZE is ALWAYS defined (even at 466): ui_px.h falls back to the
# BSP's BSP_LCD_H_RES only when it is not (device builds).
CFLAGS="-std=gnu11 -O2 -Wall -Wno-unused-parameter -Wno-unused-function -Wno-implicit-function-declaration -fno-strict-aliasing -fcommon -DPANEL_SIZE=$PANEL"
if [ "$PANEL" != 466 ]; then
    BUILD="$PWD/build-$PANEL"
    # The non-466 panel IS the P4 board: compile the screens'
    # CONFIG_BOARD_P4 blocks (the settings hub's clock icon, the P4's
    # manual time-entry entry point) into the sim too.
    CFLAGS="$CFLAGS -DCONFIG_BOARD_P4=1"
fi

INCLUDES=(
    # host stubs first: lv_conf.h + the esp_err/esp_log stand-ins must win
    "-I$PWD"
    "-I$PWD/.."
    "-I$LV/include/lvgl"
    "-I$LV/include"
    "-I$UI"
    "-I$ROOT/components/clock/include"
    "-I$ROOT/components/companion/include"
    "-I$ROOT/components/wifi/include"
    "-I$MOON/include"
    "-I$MOON/generated/$MOON_ASSET_TARGET"
    "-I$MOON/core/erfa"
    "-I$ROOT"
)

SRC_LIST="$BUILD/sources.txt"
OBJ_LIST="$BUILD/objects.txt"

# obj for <abs src> -> <abs obj path> (mirrored tree under build/obj)
obj_for() { printf '%s/obj/%s.o' "$BUILD" "${1#"$ROOT"/}"; }

# The per-file compile command as one env string (arrays don't cross into
# `bash -c` children): FLAGS <src> -o <out>
FLAGS="$CC $CFLAGS ${INCLUDES[*]} -c"
export FLAGS BUILD ROOT
export -f obj_for

echo "== collecting sources =="
mkdir -p "$BUILD/obj"
{
    find "$LV/src" -name '*.c' ! -path '*/drivers/*'
    echo "$UI/screens.c"; echo "$UI/styles.c"; echo "$UI/images.c"
    find "$FONT_DIR" -name '*.c'
    printf '%s\n' \
        "$MOON/core/moon_astro.c" "$MOON/core/moon_bake.c" \
        "$MOON/core/moon_render.c" "$MOON/core/moon_fixp.c" \
        "$MOON/core/moon_event.c"
    find "$MOON/core/erfa" -name '*.c'
    echo "$MOON/host/moon_host_assets.c"
    echo "$MOON_GEN_C"
    echo "$PWD/stubs.c"; echo "$PWD/main.c"; echo "$PWD/stb_impl.c"
} | sort -u > "$SRC_LIST"

echo "== compiling $(wc -l < "$SRC_LIST") TUs ($JOBS jobs) =="
# parallel incremental compile: one bash -c per source
xargs -P "$JOBS" -I{} bash -c '
    src="{}"
    out="$(obj_for "$src")"
    mkdir -p "$(dirname "$out")"
    if [ ! -f "$out" ] || [ "$src" -nt "$out" ]; then
        $FLAGS "$src" -o "$out" || { echo "FAILED: $src" >&2; exit 1; }
    fi' < "$SRC_LIST"

echo "== linking =="
while IFS= read -r src; do echo "$(obj_for "$src")"; done < "$SRC_LIST" > "$OBJ_LIST"
$CC $CFLAGS @"$OBJ_LIST" -o "$BUILD/uisim" -lm
echo "== done: $BUILD/uisim =="

if [ "${1:-}" = "run" ]; then
    shift
    "$BUILD/uisim" "$@"
fi
