# Building Reviewer Baselines

This repository exposes two reviewer-visible Wine baselines:

- `wine-11.5` → `/opt/wine-11.5`
- `wine-10.0` → `/opt/wine-10.0`

Use this file only for baseline build instructions. Submission guidance lives in
[`UPSTREAMING.md`](/Users/tim/code/gh/livingstaccato/space-wine/.worktrees/codex/review-ready/UPSTREAMING.md).

## Common Commands

```bash
# Primary baseline
make build test
make install

# Backport baseline
make build test WINE_VERSION=10.0
make install WINE_VERSION=10.0
```

The Makefile derives these values automatically:

- `WINE_TAG=wine-$(WINE_VERSION)`
- `WINE_SRC=wine-src-$(WINE_VERSION)`
- `PREFIX=/opt/wine-$(WINE_VERSION)`

## macOS Apple Silicon

The macOS path is verified in GitHub Actions for `wine-11.5`.

### Prerequisites

```bash
brew install bison mingw-w64 lld llvm
```

### FreeType

Build x86_64 FreeType from source into the same versioned `/opt/wine-*` prefix used by Wine.
On macOS, Wine build tools load `libfreetype.6.dylib` via its install name, so staging it only
under `/tmp` is not sufficient for a clean build.

```bash
cd /tmp
curl -L -o freetype-2.13.3.tar.gz \
  https://download.savannah.gnu.org/releases/freetype/freetype-2.13.3.tar.gz
tar xf freetype-2.13.3.tar.gz
cd freetype-2.13.3
sudo mkdir -p /opt/wine-11.5
sudo chown -R "$USER" /opt/wine-11.5
arch -x86_64 ./configure --prefix=/opt/wine-11.5 \
  CC="clang -arch x86_64" --without-harfbuzz --without-png --without-bzip2
arch -x86_64 make -j$(sysctl -n hw.ncpu)
arch -x86_64 make install
```

If you want to run the `10.0` line locally on macOS, install the same dependency into
`/opt/wine-10.0` before invoking `make build WINE_VERSION=10.0`.

### Build

```bash
make build WINE_VERSION=11.5
make install WINE_VERSION=11.5
```

This installs into `/opt/wine-11.5` by default.

The repository keeps the Rosetta workaround only on the `11.5` line. The `10.0`
backport line is not part of the macOS CI guarantee.

## Linux

Linux CI verifies both baselines from source:

- unpatched `wine-10.0`
- patched `wine-10.0`
- unpatched `wine-11.5`
- patched `wine-11.5`

Typical local flow:

```bash
sudo apt-get install -y mingw-w64 gcc g++ make bison flex \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
  libxfixes-dev libxrender-dev libxcomposite-dev libxinerama-dev \
  libfreetype-dev gcc-multilib

make prove
make prove WINE_VERSION=10.0
```

## Verification Outputs

Per-version standalone results are written under:

- `build/results/11.5/`
- `build/results/10.0/`

The verification suite checks:

- lock parameter handling
- contested overlapped lock completion
- OEM charset mapping
- edit-control stability
- contested lock leak behavior
