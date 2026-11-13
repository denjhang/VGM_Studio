#!/usr/bin/env bash
# 批量转换：Spindizzy II 整张专辑（YM2151 → YM2413）。
#
# 每首曲子一个独立目录：out/<游戏名>/<曲名>/
#     song.json            ① 原芯片 JSON（源）
#     song.ym2413.json     ② 目标芯片 JSON
#     song.ym2413.vgm      ③ 目标芯片 VGM（可试听）
#     import.log           ① 的日志
#     convert.log          ② + ③ 的日志（含音色库命中/未命中明细）
#
# 音色来源优先级（opm2opll 内部）：
#   --library 跨曲目乐器库（按 **YM2151 FM 参数**匹配，不是指纹）
#     → --fur 本曲工程按 chXinsY 名字匹配
#       → ROM 自动匹配
#
# 用法：
#   cli/tools/batch_convert.sh [源目录] [输出目录]
set -u
cd "$(dirname "$0")/../.." || exit 1

B=cli/build
LIB=docs/instrument-library.json
SRC="${1:-vgm/Spindizzy_II_(Sharp_X68000)}"
OUT="${2:-out/Spindizzy_II}"

mkdir -p "$OUT"

ok=0
fail=0
for f in "$SRC"/*.vgm; do
    base=$(basename "$f" .vgm)
    dir="$OUT/$base"
    mkdir -p "$dir"

    if ! "$B/vgm2json.exe" "$f" -o "$dir/song.json" --no-raw > "$dir/import.log" 2>&1; then
        echo "IMPORT FAIL  $base"; fail=$((fail+1)); continue
    fi
    if ! "$B/opm2opll.exe" "$dir/song.json" -o "$dir/song.ym2413.json" \
            --library="$LIB" --transpose=-12 --vgm="$dir/song.ym2413.vgm" \
            > "$dir/convert.log" 2>&1; then
        echo "CONVERT FAIL $base"; fail=$((fail+1)); continue
    fi
    echo "OK           $base"
    ok=$((ok+1))
done

echo
echo "done: $ok ok, $fail failed -> $OUT"
