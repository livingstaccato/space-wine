#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 2 ]; then
  echo "usage: $0 <wine-source-dir> <series-file> [<series-file> ...]" >&2
  exit 1
fi

wine_src="$1"
shift

repo_root="$(cd "$(dirname "$0")/.." && pwd)"

check_series() {
  local series_file="$1"
  [ -f "$series_file" ] || return 0

  while IFS= read -r relpath || [ -n "$relpath" ]; do
    case "$relpath" in
      ""|\#*) continue ;;
    esac
    git -C "$wine_src" apply --check "$repo_root/$relpath"
  done < "$series_file"
}

for series in "$@"; do
  check_series "$series"
done
