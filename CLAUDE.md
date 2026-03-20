# CLAUDE.md — space-wine

## What This Is

Wine patches and build infrastructure for running TWGS (Trade Wars 2002 Game Server)
and other Windows applications under Wine on macOS Apple Silicon and Linux. Fork of
Wine 11.0 with fixes for file locking, font matching, and edit control stability.

## Critical Rules

- **NEVER modify system Wine** (Wine Stable.app, /opt/homebrew/bin/wine, or any
  system-installed Wine). All changes go through `/opt/wine/` built from source.
- **NEVER use `--without-freetype`** — produces a Wine that cannot render fonts.
- **NEVER use `--enable-win64` alone** — use `--enable-archs=x86_64,i386` to build
  both 64-bit and 32-bit PE DLLs. TWGS is a 32-bit app.
- **FreeType must be built from source** for x86_64 on Apple Silicon (no Homebrew
  x86_64 package exists). See BUILDING.md Step 1.
- **Always `codesign -f -s -`** after replacing any .so file in a Wine install on
  macOS (Rosetta AOT cache invalidation).
- **Always `pkill -f wineserver`** after replacing Wine binaries — running wineserver
  has the old code loaded.

## Building Wine

Full instructions: [BUILDING.md](BUILDING.md)

Summary:
```bash
# 1. Build x86_64 FreeType from source → /tmp/freetype-x86/
# 2. Configure:
#    --enable-archs=x86_64,i386 --prefix=/opt/wine
#    FREETYPE_CFLAGS/FREETYPE_LIBS pointing to /tmp/freetype-x86
# 3. arch -x86_64 make -j$(sysctl -n hw.ncpu)
# 4. arch -x86_64 make install
# 5. cp /tmp/freetype-x86/lib/libfreetype.6.dylib /opt/wine/lib/
```

## Running TWGS

```bash
DYLD_LIBRARY_PATH=/opt/wine/lib WINEPREFIX=~/.wine \
  /opt/wine/bin/wine ~/.wine/drive_c/TWGS/twgs.exe
```

Suppress noisy debug channels:
```bash
WINEDEBUG=-fixme+edit,-fixme+font ...
```

Ports: 2002 (telnet), 2003 (admin). Headless: see [docs/twgs-headless.md](docs/twgs-headless.md).

## Patches (branch: wine-11.0-patched)

| Patch file | File(s) modified | What it fixes |
|---|---|---|
| `ntdll-fix-NtLockFile-FIXMEs.patch` | `dlls/ntdll/unix/file.c` | io_status, key, APC params rejected; contested async locks hang forever; IOCP completion posting on lock success |
| `kernelbase-fix-UnlockFileEx.patch` | `dlls/kernelbase/file.c` | Overlapped I/O support for UnlockFileEx |
| `win32u-fix-OEM_CHARSET.patch` | `dlls/win32u/font.c` | "Untranslated charset 255" FIXME for DOS terminal fonts |
| `user32-fix-edit-BuildLineDefs.patch` | `dlls/user32/edit.c` | "modification occurred outside buffer" FIXME on rapid text updates |
| `comctl32-fix-edit-BuildLineDefs.patch` | `dlls/comctl32_v6/edit.c` | Same edit control fix for comctl32_v6 |
| `kernel32-tests-expand-lockfile.patch` | `dlls/kernel32/tests/file.c` | Expanded lock test coverage for Wine test suite (51 checks) |

## Test Tools

```bash
# Compile
x86_64-w64-mingw32-gcc -o locktest.exe tests/locktest.c -lntdll
x86_64-w64-mingw32-gcc -o lockstress.exe tests/lockstress.c
x86_64-w64-mingw32-gcc -o fonttest.exe tests/fonttest.c -lgdi32 -luser32
x86_64-w64-mingw32-gcc -o edittest.exe tests/edittest.c -lgdi32 -luser32

# Run
DYLD_LIBRARY_PATH=/opt/wine/lib wine locktest.exe -v   # 51 checks
DYLD_LIBRARY_PATH=/opt/wine/lib wine lockstress.exe     # 200 lock ops
DYLD_LIBRARY_PATH=/opt/wine/lib wine fonttest.exe       # OEM_CHARSET
DYLD_LIBRARY_PATH=/opt/wine/lib wine edittest.exe       # edit control
```

## CI

GitHub Actions workflow `prove.yml` runs on every push:
- **Windows** (2019, 2022, 2025): Ground truth — tests on real Windows NT kernel
- **Linux unpatched** (ubuntu-22.04, ubuntu-24.04): Shows failures on stock Wine
- **Linux patched** (ubuntu-22.04, ubuntu-24.04): Builds Wine from source, verifies all pass
- **macOS patched** (13 Intel, 14/15 ARM64): Builds Wine with FreeType, validates primary target

## Repository Structure

```
wine/                  # Wine source (submodule, branch wine-11.0-clean)
patches/               # git apply compatible patch files
tests/                 # locktest.c, lockstress.c, fonttest.c, edittest.c
results/               # Captured before/after test output
tools/                 # wine-monitor.sh
docs/                  # twgs-headless.md, twgs-profiling.md
.github/workflows/     # CI pipeline (matrix: Windows, Linux, macOS)
Makefile               # Build and test pipeline (make prove)
```

## Install Target

`/opt/wine/` — self-contained Wine install. Never modify system Wine installations.
`DYLD_LIBRARY_PATH=/opt/wine/lib` required at runtime for FreeType.
