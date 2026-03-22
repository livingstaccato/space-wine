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
cd space-wine/wine
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
   ../configure --enable-archs=x86_64,i386 --prefix=/opt/wine'
```

**CRITICAL:** Use `--enable-archs=x86_64,i386`, NOT `--enable-win64`. The archs flag
builds both x86_64 and i386 PE DLLs from one tree. Without i386 DLLs, 32-bit apps
(TWGS is PE32) fail with "failed to load syswow64/ntdll.dll".

Verify the output includes:
- `checking for -lfreetype... libfreetype.6.dylib` (NOT "not found")
- `i386_CC = i686-w64-mingw32-gcc` in the Makefile (NOT empty)
- 50+ `i386-windows` references in the Makefile

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
- `find dlls -path '*/i386-windows/*.dll' | wc -l` — should be 600+ (NOT 0)

## Step 4: Install

```bash
WINEPREFIX=~/.wine /opt/wine/bin/wineserver -k 2>/dev/null; sleep 1
arch -x86_64 make install

# Copy freetype dylib and fix install_name to match final location
cp /tmp/freetype-x86/lib/libfreetype.6.dylib /opt/wine/lib/
install_name_tool -id /opt/wine/lib/libfreetype.6.dylib /opt/wine/lib/libfreetype.6.dylib
codesign -f -s - /opt/wine/lib/libfreetype.6.dylib
```

## Step 5: Test

```bash
WINEPREFIX=~/.wine /opt/wine/bin/wineserver -k 2>/dev/null; sleep 1
/opt/wine/bin/wine --version
# Should show: wine-11.5 or similar

DYLD_LIBRARY_PATH=/opt/wine/lib WINEPREFIX=~/.wine \
  /opt/wine/bin/wine ~/.wine/drive_c/TWGS/twgs.exe
# Ports 2002 and 2003 should open within 15 seconds
# No "cannot find FreeType" errors
# No "syswow64/ntdll.dll" errors
```

## Running Test Tools

```bash
# Compile (one-time)
x86_64-w64-mingw32-gcc -o locktest.exe tests/locktest.c -lntdll
x86_64-w64-mingw32-gcc -o lockstress.exe tests/lockstress.c
x86_64-w64-mingw32-gcc -o fonttest.exe tests/fonttest.c -lgdi32 -luser32
x86_64-w64-mingw32-gcc -o edittest.exe tests/edittest.c -lgdi32 -luser32

# Run
DYLD_LIBRARY_PATH=/opt/wine/lib wine locktest.exe -v    # all passed, 0 failed
DYLD_LIBRARY_PATH=/opt/wine/lib wine lockstress.exe     # 200/200, 0 hangs
DYLD_LIBRARY_PATH=/opt/wine/lib wine fonttest.exe       # 0 FIXMEs
DYLD_LIBRARY_PATH=/opt/wine/lib wine edittest.exe       # edit control
```

## What Each Piece Does

| Component | Why It's Needed | What Breaks Without It |
|---|---|---|
| FreeType (x86_64, from source) | Builds .fon fonts, loads TrueType at runtime | No fonts — blank/broken GUI windows |
| `--enable-archs=x86_64,i386` | Builds both 64-bit and 32-bit PE DLLs | 32-bit apps (TWGS) can't load |
| `DYLD_LIBRARY_PATH=/opt/wine/lib` | Runtime finds libfreetype.6.dylib | "cannot find FreeType" at startup |
| `arch -x86_64` | Build tools run under Rosetta | Configure/make fail on arm64 |
| bison from Homebrew | Wine's parser tools need bison >= 3.0 | Configure error |
| Rosetta patches (in source) | WoW64 compatibility on Apple Silicon | 32-bit apps crash |

## Patches in This Build

Apply from the `patches/` directory (all are `git apply` compatible):

1. `ntdll-fix-NtLockFile-FIXMEs.patch` — NtLockFile/NtUnlockFile parameter handling + IOCP completion
2. `kernelbase-fix-UnlockFileEx.patch` — UnlockFileEx overlapped I/O support
3. `win32u-fix-OEM_CHARSET.patch` — OEM_CHARSET (255) font matching
4. `user32-fix-edit-BuildLineDefs.patch` — Edit control stability under rapid updates
5. `comctl32-fix-edit-BuildLineDefs.patch` — Same edit control fix for comctl32_v6
6. `kernel32-tests-expand-lockfile.patch` — Expanded lock tests for Wine test suite
