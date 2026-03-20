# Building Wine for /opt/wine (macOS Apple Silicon)

## CRITICAL: Do Not Skip Steps

This builds a fully working Wine from source at `/opt/wine/`. Every step matters.
Skipping freetype = no fonts. Skipping i386 = no 32-bit apps (TWGS is 32-bit).

## Prerequisites

```bash
brew install bison mingw-w64 lld llvm
```

## Step 1: Build x86_64 FreeType from Source

Wine needs FreeType to build bitmap font files (.fon) and to load TrueType fonts
at runtime. There is no x86_64 FreeType in Homebrew on Apple Silicon — build it:

```bash
cd /tmp
curl -L -o freetype-2.13.3.tar.gz \
  https://download.savannah.gnu.org/releases/freetype/freetype-2.13.3.tar.gz
tar xf freetype-2.13.3.tar.gz
cd freetype-2.13.3
arch -x86_64 ./configure --prefix=/tmp/freetype-x86 \
  CC="clang -arch x86_64" --without-harfbuzz --without-png --without-bzip2
arch -x86_64 make -j$(sysctl -n hw.ncpu)
arch -x86_64 make install
```

Then build the dynamic library (Wine dlopen's it at runtime):

```bash
cd /tmp/freetype-2.13.3
arch -x86_64 /bin/bash -c '
CC="clang -arch x86_64"
CFLAGS="-I/tmp/freetype-2.13.3/include -DFT2_BUILD_LIBRARY"
for f in src/base/ftsystem.c src/base/ftinit.c src/base/ftdebug.c src/base/ftbase.c \
  src/base/ftbbox.c src/base/ftbdf.c src/base/ftbitmap.c src/base/ftcid.c \
  src/base/ftfstype.c src/base/ftgasp.c src/base/ftglyph.c src/base/ftgxval.c \
  src/base/ftmm.c src/base/ftotval.c src/base/ftpatent.c src/base/ftpfr.c \
  src/base/ftstroke.c src/base/ftsynth.c src/base/fttype1.c src/base/ftwinfnt.c \
  src/base/ftmac.c src/truetype/truetype.c src/type1/type1.c src/cff/cff.c \
  src/cid/type1cid.c src/pfr/pfr.c src/type42/type42.c src/winfonts/winfnt.c \
  src/pcf/pcf.c src/bdf/bdf.c src/sfnt/sfnt.c src/autofit/autofit.c \
  src/pshinter/pshinter.c src/raster/raster.c src/smooth/smooth.c \
  src/cache/ftcache.c src/gzip/ftgzip.c src/lzw/ftlzw.c src/psaux/psaux.c \
  src/psnames/psnames.c src/sdf/sdf.c src/svg/svg.c; do
  $CC $CFLAGS -c "$f" -o "$(basename ${f%.c}.o)"
done
$CC -arch x86_64 -dynamiclib -o /tmp/freetype-x86/lib/libfreetype.6.dylib \
  -install_name /opt/wine/lib/libfreetype.6.dylib *.o \
  -framework CoreFoundation -lz
'
```

## Step 2: Configure Wine

```bash
cd uwarp-space/wine
mkdir build64-x86 && cd build64-x86

# Symlink i686 mingw tools (arm64 binaries, work under Rosetta)
mkdir -p /tmp/mingw-bin
for tool in gcc g++ dlltool ar ranlib windres; do
  ln -sf /opt/homebrew/bin/i686-w64-mingw32-$tool /tmp/mingw-bin/
done

PATH="/opt/homebrew/opt/bison/bin:/tmp/mingw-bin:/opt/homebrew/bin:$PATH" \
arch -x86_64 /bin/bash -c \
  'export PATH="/opt/homebrew/opt/bison/bin:/tmp/mingw-bin:/opt/homebrew/bin:$PATH"
   CC="clang -arch x86_64" CXX="clang++ -arch x86_64" \
   FREETYPE_CFLAGS="-I/tmp/freetype-x86/include/freetype2" \
   FREETYPE_LIBS="-L/tmp/freetype-x86/lib -lfreetype" \
   i386_CC="i686-w64-mingw32-gcc" \
   ../configure --enable-win64 --prefix=/opt/wine'
```

Verify the output includes:
- `checking for -lfreetype... libfreetype.6.dylib` (NOT "not found")
- `i386_CC = i686-w64-mingw32-gcc` in the Makefile (NOT empty)

## Step 3: Build

```bash
export PATH="/opt/homebrew/opt/bison/bin:/tmp/mingw-bin:/opt/homebrew/bin:$PATH"
arch -x86_64 make -j$(sysctl -n hw.ncpu)
```

Verify after build:
- `fonts/*.fon` — should be 50 files (NOT 0)
- `dlls/ntdll/ntdll.so` — exists
- `dlls/win32u/win32u.so` — exists
- `server/wineserver` — exists

## Step 4: Install

```bash
pkill -f wineserver; sleep 1
arch -x86_64 make install

# Copy freetype dylib (Wine dlopen's it at runtime)
cp /tmp/freetype-x86/lib/libfreetype.6.dylib /opt/wine/lib/

# Copy i386 PE DLLs from Wine Stable.app (Wine 11.0 configure doesn't
# generate i386 targets on macOS — these provide WoW64 32-bit support)
cp -R "/Applications/Wine Stable.app/Contents/Resources/wine/lib/wine/i386-windows" \
  /opt/wine/lib/wine/
```

## Step 5: Test

```bash
pkill -f wineserver; sleep 1
/opt/wine/bin/wine --version
# Should show: wine-11.0-XX-gXXXXXXX

DYLD_LIBRARY_PATH=/opt/wine/lib WINEPREFIX=~/.wine \
  /opt/wine/bin/wine ~/.wine/drive_c/TWGS/twgs.exe
# Ports 2002 and 2003 should open within 15 seconds
# No "cannot find FreeType" errors
# No "syswow64/ntdll.dll" errors
```

## Running Test Tools

```bash
# Compile (one-time)
x86_64-w64-mingw32-gcc -o locktest.exe wine/tools/locktest.c -lntdll
x86_64-w64-mingw32-gcc -o lockstress.exe wine/tools/lockstress.c
x86_64-w64-mingw32-gcc -o fonttest.exe wine/tools/fonttest.c -lgdi32 -luser32

# Run
DYLD_LIBRARY_PATH=/opt/wine/lib wine locktest.exe -v    # 51 passed, 0 failed
DYLD_LIBRARY_PATH=/opt/wine/lib wine lockstress.exe     # 200/200, 0 hangs
DYLD_LIBRARY_PATH=/opt/wine/lib wine fonttest.exe       # 11 passed, 0 failed
```

## What Each Piece Does

| Component | Why It's Needed | What Breaks Without It |
|---|---|---|
| FreeType (x86_64, from source) | Builds .fon fonts, loads TrueType at runtime | No fonts — blank/broken GUI windows |
| `--enable-win64` | Builds 64-bit Wine host | Nothing works |
| `i386_CC=i686-w64-mingw32-gcc` | Tells configure about 32-bit cross-compiler | No WoW64 in Makefile |
| i386 DLLs from Wine Stable.app | 32-bit PE DLLs for WoW64 | 32-bit apps (TWGS) can't load |
| `DYLD_LIBRARY_PATH=/opt/wine/lib` | Runtime finds libfreetype.6.dylib | "cannot find FreeType" at startup |
| `arch -x86_64` | Build tools run under Rosetta | Configure/make fail on arm64 |
| bison from Homebrew | Wine's parser tools need bison >= 3.0 | Configure error |
| Rosetta patches (in source) | WoW64 compatibility on Apple Silicon | 32-bit apps crash |

## Patches in This Build

All patches are in the wine submodule source tree (`wine-11.0-patched` branch):

1. **kernelbase: UnlockFileEx overlapped I/O** (9aecf2de29d)
2. **ntdll: Rosetta 2 WoW64 compatibility** (b4d89e3d324)
3. **ntdll: IOCP completion on lock success** (a5bb5729af4)
4. **ntdll: Fix NtLockFile/NtUnlockFile FIXMEs** (8211cf4312f)
5. **win32u: OEM_CHARSET font matching** (1bd6ed35acc)
