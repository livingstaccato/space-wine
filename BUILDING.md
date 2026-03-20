# Building and Testing Wine Patches (macOS Apple Silicon)

## Architecture

The system Wine (`/opt/homebrew/bin/wine`) is **x86_64** running under Rosetta 2.
It lives at `/Applications/Wine Stable.app/Contents/Resources/wine/`.

To test ntdll patches, we cross-compile an x86_64 `ntdll.so` and swap it into the
system Wine's library directory.

## Prerequisites

```bash
brew install bison mingw-w64 lld llvm
```

## Building the Patched ntdll.so

```bash
# Configure (one-time, from wine/build64-x86/)
mkdir -p wine/build64-x86 && cd wine/build64-x86
PATH="/opt/homebrew/opt/bison/bin:$PATH" \
  arch -x86_64 /bin/bash -c \
  'CC="clang -arch x86_64" CXX="clang++ -arch x86_64" \
   ../configure --enable-win64 --without-freetype --with-mingw=x86_64-w64-mingw32'

# Build ntdll.so only (incremental, ~10 seconds)
cd wine/build64-x86
arch -x86_64 make -j$(sysctl -n hw.ncpu) dlls/ntdll/ntdll.so
```

Output: `wine/build64-x86/dlls/ntdll/ntdll.so` (Mach-O x86_64 dylib)

## Swapping into System Wine

```bash
WINE_NTDLL="/Applications/Wine Stable.app/Contents/Resources/wine/lib/wine/x86_64-unix/ntdll.so"

# Backup original (first time only)
cp "$WINE_NTDLL" "$WINE_NTDLL.bak"

# Install patched version
cp wine/build64-x86/dlls/ntdll/ntdll.so "$WINE_NTDLL"

# CRITICAL: Re-sign for Rosetta (without this, Wine crashes with "rosetta error")
codesign -f -s - "$WINE_NTDLL"

# Kill any running wineserver to pick up the new ntdll
pkill -f wineserver; sleep 1
```

## Restoring Original

```bash
WINE_NTDLL="/Applications/Wine Stable.app/Contents/Resources/wine/lib/wine/x86_64-unix/ntdll.so"
cp "$WINE_NTDLL.bak" "$WINE_NTDLL"
codesign -f -s - "$WINE_NTDLL"
pkill -f wineserver; sleep 1
```

## Running the Verification Tool

```bash
# Compile locktest.exe (x86_64 PE, one-time)
/opt/homebrew/bin/x86_64-w64-mingw32-gcc -o wine/tools/locktest.exe wine/tools/locktest.c -lntdll

# Run (system wine, uses whatever ntdll.so is currently installed)
wine wine/tools/locktest.exe
```

Expected output with patched ntdll: 53 passed, 0 failed.
Expected output with original ntdll: 7 passed, 33 failed (40 total — 13 skipped due to cascading failures).

## Key Gotchas

1. **Always `codesign -f -s -` after swapping ntdll.so** — Rosetta caches the AOT
   translation and the code signature must match. Without this you get:
   `rosetta error: Attachment of code signature supplement failed: 1`

2. **Always `pkill -f wineserver` after swapping** — the running wineserver has the
   old ntdll loaded. New wine processes connect to the existing server.

3. **locktest.exe must be x86_64 PE** — the system Wine is x86_64. aarch64 PE
   binaries won't load (`ShellExecuteEx failed: File not found`).

4. **`arch -x86_64` is required for the build** — even though clang cross-compiles,
   the configure scripts and build tools must run under Rosetta.
