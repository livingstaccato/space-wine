#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

primary_docs=(
  "$ROOT/README.md"
  "$ROOT/BUILDING.md"
  "$ROOT/UPSTREAMING.md"
  "$ROOT/CLAUDE.md"
  "$ROOT/TRADEOFFS.md"
)

for file in "${primary_docs[@]}"; do
  if rg -n "Wine 11\\.0|wine-11\\.0|latest development|TWGS|Trade Wars|Game Server" "$file" >/dev/null; then
    echo "stale primary-doc wording in $file"
    exit 1
  fi
done

rg -n "wine-11\\.5" "$ROOT/README.md" >/dev/null
rg -n "wine-10\\.0|Wine 10\\.0" "$ROOT/README.md" >/dev/null
rg -n "Upstream candidates|Local-only workarounds|Review Status Model|UPSTREAMING\\.md" "$ROOT/README.md" >/dev/null

rg -n "10\\.0|11\\.5" "$ROOT/.github/workflows/prove.yml" >/dev/null
rg -n "linux-unpatched-10\\.0|linux-patched-10\\.0|linux-unpatched-11\\.5|linux-patched-11\\.5" \
  "$ROOT/.github/workflows/prove.yml" >/dev/null
rg -n "actions/checkout@v6|actions/upload-artifact@v6" "$ROOT/.github/workflows/prove.yml" >/dev/null

test -f "$ROOT/patches/series-11.5.txt"
test -f "$ROOT/patches/series-10.0.txt"
test -f "$ROOT/patches/README.md"
test -f "$ROOT/UPSTREAMING.md"

rg -n "wine-10\\.0" "$ROOT/Makefile" >/dev/null
rg -n "series-11\\.5|series-10\\.0" "$ROOT/Makefile" >/dev/null
rg -n "upstream-first|ready|needs split|needs stronger evidence" "$ROOT/patches/README.md" >/dev/null
