#!/usr/bin/env bash
# 批量跑 41 个样例：vgminfo / vgm2json / jsondump / vgmroundtrip
set -u
BIN="$(cd "$(dirname "$0")/.." && pwd)/build"
DIR="${1:-$(cd "$(dirname "$0")/../../vgm/Spindizzy_II_(Sharp_X68000)" && pwd)}"
TMP="${TMPDIR:-/tmp}/vgmrt.$$"
mkdir -p "$TMP"

pass=0; fail=0; rt=0
for f in "$DIR"/*.vgm; do
  name="$(basename "$f" .vgm)"
  out="$TMP/$name.json"
  res=$("$BIN/vgm2json.exe" "$f" -o "$out" 2>&1)
  code=$?
  if [ $code -ne 0 ]; then
    echo "IMPORT-FAIL [$code] $name"
    echo "$res" | sed 's/^/    /'
    fail=$((fail+1))
    continue
  fi
  integ=$(echo "$res" | grep -o 'structured=[0-9]*' | cut -d= -f2)
  file=$(echo "$res" | grep -o 'file=[0-9]*' | cut -d= -f2)
  rtres=$("$BIN/vgmroundtrip.exe" "$f" 2>&1)
  rtcode=$?
  if [ $rtcode -eq 0 ]; then rt=$((rt+1)); rttxt="RT-OK"; else rttxt="RT-FAIL"; fi
  printf "%-28s writes=%-6s notes=%-5s %s\n" "$name" "$integ" \
    "$(echo "$res" | grep -o 'notes *: [0-9]*' | grep -o '[0-9]*')" "$rttxt"
  pass=$((pass+1))
done

echo "--------------------------------------------"
echo "import ok=$pass fail=$fail   roundtrip ok=$rt"
rm -rf "$TMP"
