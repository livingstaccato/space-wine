#!/bin/bash
set -e

echo ""
echo "=========================================="
echo "  TWGS Headless Test (Linux + Xvfb)"
echo "=========================================="
echo ""

# Start virtual framebuffer
echo "[1/5] Starting Xvfb..."
Xvfb :99 -screen 0 1024x768x24 &
XVFB_PID=$!
sleep 1
export DISPLAY=:99
echo "  Xvfb started (PID $XVFB_PID)"

# Set up Wine prefix
export WINEPREFIX=${WINEPREFIX:-/wineprefix}
echo "[2/5] Initializing Wine prefix at $WINEPREFIX..."
WINEDEBUG=-all wineboot --init 2>/dev/null || true
sleep 3
echo "  Wine prefix ready"

# Install TWGS into the prefix (mount /twgs or use existing drive_c)
echo "[3/5] Setting up TWGS..."
if [ -d /twgs ] && [ -f /twgs/twgs.exe ]; then
    mkdir -p "$WINEPREFIX/drive_c/TWGS"
    cp -r /twgs/* "$WINEPREFIX/drive_c/TWGS/"
    echo "  Copied from /twgs mount"
elif [ -f "$WINEPREFIX/drive_c/TWGS/twgs.exe" ]; then
    echo "  Using existing drive_c/TWGS"
else
    echo "  ERROR: No TWGS binary found. Mount with -v /path/to/TWGS:/twgs"
    exit 1
fi
ls "$WINEPREFIX/drive_c/TWGS/twgs.exe" >/dev/null
echo "  TWGS ready"

# Launch TWGS headless
echo "[4/5] Launching TWGS..."
WINEDEBUG=-fixme+edit,-fixme+font wine "$WINEPREFIX/drive_c/TWGS/twgs.exe" &
TWGS_PID=$!
echo "  TWGS launched (PID $TWGS_PID)"

# Wait for ports to open
echo "[5/5] Waiting for ports (max 30s)..."
PORT_2002=false
PORT_2003=false

for i in $(seq 1 30); do
    if nc -z localhost 2002 2>/dev/null; then
        PORT_2002=true
    fi
    if nc -z localhost 2003 2>/dev/null; then
        PORT_2003=true
    fi
    if $PORT_2002 && $PORT_2003; then
        break
    fi
    sleep 1
    printf "  %ds...\r" "$i"
done
echo ""

echo ""
echo "=========================================="
echo "  Results"
echo "=========================================="
echo ""

PASS=true

if $PORT_2002; then
    echo "  PASS: Port 2002 (telnet) OPEN"
else
    echo "  FAIL: Port 2002 (telnet) CLOSED"
    PASS=false
fi

if $PORT_2003; then
    echo "  PASS: Port 2003 (admin)  OPEN"
else
    echo "  FAIL: Port 2003 (admin)  CLOSED"
    PASS=false
fi

# Try connecting to telnet port
if $PORT_2002; then
    echo ""
    echo "  Telnet probe:"
    BANNER=$(echo "" | nc -w 2 localhost 2002 2>/dev/null | head -3) || true
    if [ -n "$BANNER" ]; then
        echo "$BANNER" | sed 's/^/    /'
        echo "  PASS: Telnet responds"
    else
        echo "    (connected, no banner within 2s — normal for TWGS)"
        echo "  PASS: Port accepts connections"
    fi
fi

# Check TWGS process is still alive
if kill -0 $TWGS_PID 2>/dev/null; then
    echo "  PASS: TWGS process alive"
else
    echo "  FAIL: TWGS process died"
    PASS=false
fi

echo ""
if $PASS; then
    echo "  ** HEADLESS TWGS VERIFIED **"
    echo ""
    echo "  Wine: $(wine --version 2>/dev/null)"
    echo "  Platform: $(uname -m) Linux (headless, Xvfb)"
else
    echo "  ** HEADLESS TWGS FAILED **"
fi
echo ""

# Cleanup
kill $TWGS_PID 2>/dev/null || true
wineserver -k 2>/dev/null || true
kill $XVFB_PID 2>/dev/null || true

$PASS
