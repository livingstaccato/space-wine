#!/usr/bin/env bash
#
# prove.sh — Clone Wine 11.0, build unpatched + patched ntdll, run proof tests
#
# Usage:
#   ./prove.sh              # full run: clone, build, test both
#   ./prove.sh --skip-clone # reuse existing wine-src/
#   ./prove.sh --patch-only # build + install patched ntdll, skip comparison
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WINE_TAG="wine-11.0"
WINE_DIR="$SCRIPT_DIR/wine-src"
BUILD_DIR="$WINE_DIR/build64-x86"
MINGW_GCC="$(command -v x86_64-w64-mingw32-gcc 2>/dev/null || echo "")"
WINE_BIN="$(command -v wine 2>/dev/null || echo "")"

# System Wine ntdll location (macOS Wine Stable.app)
WINE_NTDLL=""
for candidate in \
    "/Applications/Wine Stable.app/Contents/Resources/wine/lib/wine/x86_64-unix/ntdll.so" \
    "/Applications/Wine.app/Contents/Resources/wine/lib/wine/x86_64-unix/ntdll.so" \
    "/opt/homebrew/lib/wine/x86_64-unix/ntdll.so" \
    "/usr/lib/wine/x86_64-unix/ntdll.so" \
    "/usr/lib64/wine/x86_64-unix/ntdll.so"; do
    if [ -f "$candidate" ]; then
        WINE_NTDLL="$candidate"
        break
    fi
done

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BOLD='\033[1m'
NC='\033[0m'

SKIP_CLONE=0
PATCH_ONLY=0
for arg in "$@"; do
    case "$arg" in
        --skip-clone) SKIP_CLONE=1 ;;
        --patch-only) PATCH_ONLY=1 ;;
    esac
done

die() { echo -e "${RED}ERROR: $*${NC}" >&2; exit 1; }
info() { echo -e "${BOLD}==> $*${NC}"; }
ok() { echo -e "  ${GREEN}OK${NC} $*"; }
fail() { echo -e "  ${RED}FAIL${NC} $*"; }

# ── Preflight ──────────────────────────────────────────────────────

info "Preflight checks"

[ -n "$WINE_BIN" ] || die "wine not found in PATH. Install Wine Stable: https://wiki.winehq.org/macOS"
ok "wine: $WINE_BIN ($(wine --version 2>/dev/null || echo unknown))"

[ -n "$MINGW_GCC" ] || die "x86_64-w64-mingw32-gcc not found. Run: brew install mingw-w64"
ok "mingw: $MINGW_GCC"

[ -n "$WINE_NTDLL" ] || die "Cannot find system Wine ntdll.so. Searched standard locations."
ok "ntdll: $WINE_NTDLL"

if [ "$(uname -m)" = "arm64" ]; then
    arch -x86_64 /bin/bash -c "echo ok" >/dev/null 2>&1 || die "Rosetta 2 not available. Run: softwareupdate --install-rosetta"
    ok "rosetta: available"
fi

command -v bison >/dev/null 2>&1 || die "bison not found. Run: brew install bison"
BISON_PATH=""
for p in /opt/homebrew/opt/bison/bin /usr/local/opt/bison/bin; do
    [ -x "$p/bison" ] && BISON_PATH="$p" && break
done
# Fall back to system bison
[ -z "$BISON_PATH" ] && BISON_PATH="$(dirname "$(command -v bison)")"
ok "bison: $BISON_PATH/bison"

echo ""

# ── Clone Wine ─────────────────────────────────────────────────────

if [ $SKIP_CLONE -eq 0 ]; then
    if [ -d "$WINE_DIR" ]; then
        info "Removing existing wine-src/"
        rm -rf "$WINE_DIR"
    fi
    info "Cloning Wine $WINE_TAG (shallow)"
    git clone --depth 1 --branch "$WINE_TAG" https://gitlab.winehq.org/wine/wine.git "$WINE_DIR"
    echo ""
else
    [ -d "$WINE_DIR" ] || die "wine-src/ not found. Run without --skip-clone first."
    info "Reusing existing wine-src/"
    echo ""
fi

# ── Compile test tools ─────────────────────────────────────────────

info "Compiling test tools"
mkdir -p "$SCRIPT_DIR/build"
"$MINGW_GCC" -o "$SCRIPT_DIR/build/locktest.exe" "$SCRIPT_DIR/tests/locktest.c" -lntdll
ok "locktest.exe"
"$MINGW_GCC" -o "$SCRIPT_DIR/build/lockstress.exe" "$SCRIPT_DIR/tests/lockstress.c"
ok "lockstress.exe"
echo ""

# ── Helper: swap ntdll ─────────────────────────────────────────────

swap_ntdll() {
    local src="$1"
    local label="$2"
    cp "$src" "$WINE_NTDLL"
    codesign -f -s - "$WINE_NTDLL" 2>/dev/null || true
    pkill -f wineserver 2>/dev/null || true
    sleep 2
    info "Installed $label ntdll.so"
}

backup_ntdll() {
    if [ ! -f "$WINE_NTDLL.space-wine-backup" ]; then
        cp "$WINE_NTDLL" "$WINE_NTDLL.space-wine-backup"
        ok "Backed up original ntdll.so"
    fi
}

restore_ntdll() {
    if [ -f "$WINE_NTDLL.space-wine-backup" ]; then
        cp "$WINE_NTDLL.space-wine-backup" "$WINE_NTDLL"
        codesign -f -s - "$WINE_NTDLL" 2>/dev/null || true
        pkill -f wineserver 2>/dev/null || true
        sleep 1
    fi
}

# ── Helper: build ntdll.so from a source tree ─────────────────────

build_ntdll() {
    local label="$1"
    info "Building $label ntdll.so"

    if [ ! -f "$BUILD_DIR/Makefile" ]; then
        mkdir -p "$BUILD_DIR"
        (
            cd "$BUILD_DIR"
            PATH="$BISON_PATH:$PATH" \
            arch -x86_64 /bin/bash -c \
                'CC="clang -arch x86_64" CXX="clang++ -arch x86_64" \
                 ../configure --enable-win64 --without-freetype --with-mingw=x86_64-w64-mingw32' \
                > "$SCRIPT_DIR/build/configure.log" 2>&1
        )
        if [ ! -f "$BUILD_DIR/Makefile" ]; then
            die "configure failed — see build/configure.log"
        fi
        ok "configure (log: build/configure.log)"
    fi

    local ncpu
    ncpu="$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
    (
        cd "$BUILD_DIR"
        export PATH="$BISON_PATH:$PATH"
        # Build tools first (makedep, widl etc.), then ntdll and its dependencies
        arch -x86_64 make -j"$ncpu" tools 2>&1 | tail -1
        arch -x86_64 make -j"$ncpu" dlls/ntdll/ntdll.so 2>&1 | tail -1
    )

    if [ ! -f "$BUILD_DIR/dlls/ntdll/ntdll.so" ]; then
        die "ntdll.so build failed — check build output above"
    fi
    ok "built: $BUILD_DIR/dlls/ntdll/ntdll.so"
    echo ""
}

# ── Helper: run tests ──────────────────────────────────────────────

run_tests() {
    local label="$1"
    local results_dir="$SCRIPT_DIR/build/results-$label"
    mkdir -p "$results_dir"

    info "Running tests ($label)"

    # locktest
    wine "$SCRIPT_DIR/build/locktest.exe" -v 2>/dev/null \
        | grep -v '^\[mvk-\|^\t\|^$' > "$results_dir/locktest.txt" 2>&1 || true
    local locktest_pass=$(grep "passed" "$results_dir/locktest.txt" | grep -o '[0-9]* passed' | grep -o '[0-9]*')
    local locktest_fail=$(grep "failed" "$results_dir/locktest.txt" | grep -o '[0-9]* failed' | grep -o '[0-9]*')
    echo -e "  locktest:   ${locktest_pass:-?} passed, ${locktest_fail:-?} failed"

    # lockstress
    wine "$SCRIPT_DIR/build/lockstress.exe" 2>/dev/null \
        | grep -v '^\[mvk-\|^\t\|^$' > "$results_dir/lockstress.txt" 2>&1 || true
    local stress_hangs=$(grep "Hangs:" "$results_dir/lockstress.txt" | grep -o '[0-9]*' | head -1)
    local stress_done=$(grep "Completed:" "$results_dir/lockstress.txt" | grep -o '[0-9]* /' | grep -o '[0-9]*')
    echo -e "  lockstress: ${stress_done:-?}/200 completed, ${stress_hangs:-?} hangs"

    pkill -f wineserver 2>/dev/null || true
    sleep 1
    echo ""
}

# ── Main: Build unpatched, test, patch, test ───────────────────────

backup_ntdll

if [ $PATCH_ONLY -eq 0 ]; then
    # Build unpatched Wine 11.0
    build_ntdll "UNPATCHED (Wine 11.0)"
    swap_ntdll "$BUILD_DIR/dlls/ntdll/ntdll.so" "UNPATCHED"
    run_tests "unpatched"

    # Apply patch
    info "Applying NtLockFile patch"
    cp "$SCRIPT_DIR/patches/file.c" "$WINE_DIR/dlls/ntdll/unix/file.c"
    ok "patched dlls/ntdll/unix/file.c"

    # Rebuild
    rm -f "$BUILD_DIR/dlls/ntdll/unix/file.o"  # force recompile
    build_ntdll "PATCHED"
    swap_ntdll "$BUILD_DIR/dlls/ntdll/ntdll.so" "PATCHED"
    run_tests "patched"
else
    # Patch-only mode: just apply, build, install
    info "Applying NtLockFile patch"
    cp "$SCRIPT_DIR/patches/file.c" "$WINE_DIR/dlls/ntdll/unix/file.c"
    ok "patched dlls/ntdll/unix/file.c"
    rm -f "$BUILD_DIR/dlls/ntdll/unix/file.o"
    build_ntdll "PATCHED"
    swap_ntdll "$BUILD_DIR/dlls/ntdll/ntdll.so" "PATCHED"
    run_tests "patched"
fi

# ── Summary ────────────────────────────────────────────────────────

echo ""
echo -e "${BOLD}============================================================${NC}"
echo -e "${BOLD}  Results Summary${NC}"
echo -e "${BOLD}============================================================${NC}"
echo ""

if [ $PATCH_ONLY -eq 0 ]; then
    echo -e "  ${YELLOW}UNPATCHED Wine 11.0:${NC}"
    grep -E "passed|Hangs:|Completed:" "$SCRIPT_DIR/build/results-unpatched/locktest.txt" 2>/dev/null | sed 's/^/    /'
    grep -E "Completed:|Hangs:|Time:" "$SCRIPT_DIR/build/results-unpatched/lockstress.txt" 2>/dev/null | sed 's/^/    /'
    echo ""
fi

echo -e "  ${GREEN}PATCHED:${NC}"
grep -E "passed|Hangs:|Completed:" "$SCRIPT_DIR/build/results-patched/locktest.txt" 2>/dev/null | sed 's/^/    /'
grep -E "Completed:|Hangs:|Time:" "$SCRIPT_DIR/build/results-patched/lockstress.txt" 2>/dev/null | sed 's/^/    /'
echo ""

echo -e "  Full results in: ${BOLD}build/results-*/${NC}"
echo -e "  Patched ntdll.so is now installed in system Wine."
echo -e "  To restore original: ${BOLD}cp \"$WINE_NTDLL.space-wine-backup\" \"$WINE_NTDLL\" && codesign -f -s - \"$WINE_NTDLL\"${NC}"
echo ""
