#!/usr/bin/env bash
#
# wine-monitor.sh — Sample TWGS/Wine process metrics to CSV
#
# Usage:
#   ./wine-monitor.sh --pid <PID>                    # monitor existing process
#   ./wine-monitor.sh --pid <PID> --interval 5       # every 5 seconds
#   ./wine-monitor.sh --pid <PID> --duration 30m     # run for 30 minutes
#   ./wine-monitor.sh --pid <PID> -o metrics.csv     # output to file
#
set -euo pipefail

PID=""
INTERVAL=5
DURATION=1800  # 30 minutes default
OUTPUT=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --pid) PID="$2"; shift 2 ;;
        --interval) INTERVAL="$2"; shift 2 ;;
        --duration)
            val="$2"
            case "$val" in
                *m) DURATION=$(( ${val%m} * 60 )) ;;
                *h) DURATION=$(( ${val%h} * 3600 )) ;;
                *s) DURATION="${val%s}" ;;
                *)  DURATION="$val" ;;
            esac
            shift 2 ;;
        -o|--output) OUTPUT="$2"; shift 2 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

if [ -z "$PID" ]; then
    PID=$(pgrep -f twgs.exe 2>/dev/null | head -1)
    if [ -z "$PID" ]; then
        echo "ERROR: No TWGS process found. Use --pid <PID>" >&2
        exit 1
    fi
    echo "Auto-detected TWGS PID: $PID" >&2
fi

if ! kill -0 "$PID" 2>/dev/null; then
    echo "ERROR: PID $PID not running" >&2
    exit 1
fi

# Find wineserver PID
WINESERVER_PID=$(pgrep -f wineserver 2>/dev/null | head -1 || echo "")

HEADER="timestamp,elapsed_s,rss_kb,fds,cpu_pct,threads,tw3_count,fixme_count,wineserver_rss_kb"

if [ -n "$OUTPUT" ]; then
    echo "$HEADER" > "$OUTPUT"
    exec >> "$OUTPUT"
else
    echo "$HEADER"
fi

START=$(date +%s)
SAMPLES=0

while true; do
    NOW=$(date +%s)
    ELAPSED=$(( NOW - START ))

    if [ "$ELAPSED" -ge "$DURATION" ]; then
        break
    fi

    if ! kill -0 "$PID" 2>/dev/null; then
        echo "# Process $PID exited after ${ELAPSED}s" >&2
        break
    fi

    # Collect metrics
    RSS=$(ps -o rss= -p "$PID" 2>/dev/null | tr -d ' ' || echo 0)
    FDS=$(lsof -p "$PID" 2>/dev/null | wc -l | tr -d ' ')
    CPU=$(ps -o %cpu= -p "$PID" 2>/dev/null | tr -d ' ' || echo 0)
    THREADS=$(ps -M -p "$PID" 2>/dev/null | tail -n +2 | wc -l | tr -d ' ')
    TW3=$(ps aux 2>/dev/null | grep -c "TW3 " || echo 0)

    # Wineserver metrics
    if [ -n "$WINESERVER_PID" ] && kill -0 "$WINESERVER_PID" 2>/dev/null; then
        WS_RSS=$(ps -o rss= -p "$WINESERVER_PID" 2>/dev/null | tr -d ' ' || echo 0)
    else
        WS_RSS=0
    fi

    TS=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

    echo "$TS,$ELAPSED,$RSS,$FDS,$CPU,$THREADS,$TW3,0,$WS_RSS"

    SAMPLES=$(( SAMPLES + 1 ))
    sleep "$INTERVAL"
done

echo "# Collected $SAMPLES samples over ${ELAPSED}s" >&2
