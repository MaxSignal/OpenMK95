#!/bin/sh
# Determinism check of spec 11 §E.4: load the save file written by the original runtime for
# パンダとウサギ, start the first battle at once and compare the first two damage messages with
# the ones the original showed (33 to the frog, then 3 to the first party member).
#
# usage: tools/check_battle.sh [PANDA_GAME_DIR] [SAVE_FILE]
#   PANDA_GAME_DIR  folder of the game パンダとウサギ        (default: $OPENMK95_PANDA)
#   SAVE_FILE       testdata/panda_original_runtime.sav from the clean-room specification package
#                   (default: $OPENMK95_TESTDATA/panda_original_runtime.sav)
#   BIN             player binary (default: build/openmk95)
set -eu
GAME=${1:-${OPENMK95_PANDA:-}}
SAVE=${2:-${OPENMK95_TESTDATA:-testdata}/panda_original_runtime.sav}
BIN=${BIN:-build/openmk95}
if [ -z "$GAME" ] || [ ! -d "$GAME" ] || [ ! -f "$SAVE" ]; then
    echo "usage: $0 PANDA_GAME_DIR SAVE_FILE   (or set OPENMK95_PANDA and OPENMK95_TESTDATA)" >&2
    exit 2
fi

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir "$WORK/game"
cp -R "$GAME"/. "$WORK/game/"
find "$WORK/game" -maxdepth 1 -iname '*.sav' -exec rm -f {} +
cp "$SAVE" "$WORK/game/test.sav"

# Title: the first key shows the menu; Down + Enter = 続きから; Enter = the only save file.
cat > "$WORK/script.txt" <<'EOF'
wait 3000
press Return
wait 2000
press Down
wait 500
press Return
wait 1500
press Return
wait 4000
repeat 30 press Return
wait 2000
quit
EOF

OPENMK95_LOG=1 SDL_AUDIODRIVER=dummy "$BIN" --headless --virtual-time --midi none \
    --script "$WORK/script.txt" --test-battle 1 "$WORK/game" 2> "$WORK/log.txt" || true

# Damage numbers in the order they were shown.
DAMAGE=$(grep -o '[0-9][0-9]*ﾎﾟｲﾝﾄのﾀﾞﾒｰｼﾞ' "$WORK/log.txt" | sed 's/ﾎﾟｲﾝﾄ.*//' | head -2 | tr '\n' ' ')
echo "first two damage numbers: $DAMAGE"
if [ "$DAMAGE" = "33 3 " ]; then
    echo "OK: matches the original runtime"
else
    echo "MISMATCH: expected 33 3" >&2
    cat "$WORK/log.txt" >&2
    exit 1
fi
