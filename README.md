# space-wine

[![Verify Wine Patch Matrix](https://github.com/livingstaccato/space-wine/actions/workflows/prove.yml/badge.svg)](https://github.com/livingstaccato/space-wine/actions/workflows/prove.yml)

`space-wine` is a versioned Wine patchset with deterministic standalone verification.
It is organized for code review, upstream discussion, and reproducible CI rather than
for any single application.

## Supported Baselines

| Role | Upstream tag | Default install prefix | CI scope |
|---|---|---|---|
| Primary baseline | `wine-11.5` | `/opt/wine-11.5` | Windows ground truth, Linux patched/unpatched, macOS patched |
| Backport baseline | `wine-10.0` | `/opt/wine-10.0` | Windows ground truth, Linux patched/unpatched |

The earlier `11.x` baseline is no longer a supported review target.

## Patch Categories

### upstream candidates

These patches are organized under [`patches/README.md`](/Users/tim/code/gh/livingstaccato/space-wine/.worktrees/codex/review-ready/patches/README.md) and applied by versioned series manifests:

- `ntdll-fix-NtLockFile-FIXMEs.patch`
- `kernelbase-fix-UnlockFileEx.patch`
- `win32u-fix-OEM_CHARSET.patch`
- `user32-fix-edit-BuildLineDefs.patch`
- `comctl32-fix-edit-BuildLineDefs.patch`
- `wineserver-fix-lock-fd-leak.patch`
- `kernel32-tests-expand-lockfile.patch`

### local-only workaround

- `wow64cpu-rosetta2-workaround.patch`

This workaround is intentionally maintained only on the `wine-11.5` macOS line.

## Version Applicability

| Patch area | 11.5 | 10.0 |
|---|---|---|
| `ntdll` file locking fixes | native patch | dedicated backport patch |
| `kernelbase` unlock fix | native patch | same patch |
| `win32u` OEM charset fix | native patch | same patch |
| `user32` edit fix | native patch | same patch |
| `comctl32_v6` edit fix | native patch | not applicable |
| `wineserver` FD leak fix | native patch | same patch |
| `kernel32` lock tests | native patch | same patch |
| `wow64cpu` Rosetta workaround | local-only | not maintained |

## Verification Model

The repository is intentionally built around repeatable evidence:

- **Windows ground truth**: standalone tools run on real Windows kernels
- **Linux patched/unpatched comparison**: explicit `wine-10.0` and `wine-11.5` matrix
- **macOS patched verification**: primary `wine-11.5` deployment path

Standalone verification tools:

- `locktest.exe`: parameter handling, APC, IOCP, and lock semantics
- `lockstress.exe`: contested overlapped lock stress
- `fonttest.exe`: OEM charset regression check
- `edittest.exe`: edit-control stability check
- `fdleaktest.exe`: contested lock leak regression check

The main review path does not depend on application-specific evidence.

## Quick Start

```bash
# Primary baseline
make prove

# Backport baseline
make prove WINE_VERSION=10.0
```

The Makefile clones the selected upstream tag, applies the appropriate patch and
workaround series, builds Wine, and runs the standalone verification suite.

## Build and Install

```bash
# Build and install the primary baseline
make build install

# Build and install the backport baseline
make build install WINE_VERSION=10.0
```

Default install prefixes:

- `wine-11.5` → `/opt/wine-11.5`
- `wine-10.0` → `/opt/wine-10.0`

See [`BUILDING.md`](/Users/tim/code/gh/livingstaccato/space-wine/.worktrees/codex/review-ready/BUILDING.md) for the macOS build notes and version-specific prefix guidance.

## Repository Layout

```text
patches/                 upstream-candidate patch tracks and series manifests
workarounds/             local-only workaround series
tests/                   standalone verification tools
results/                 captured generic before/after test output
support/twgs/            optional application-specific collateral
tools/                   patch-series helper scripts
.github/workflows/       cross-platform verification matrix
```

## Notes for Reviewers

- The primary implementation line is `wine-11.5`.
- The backport line is `wine-10.0`.
- Earlier `11.x` references were removed from the maintained review surface.
- `support/twgs/` remains available as secondary collateral, but it is not part of the main technical case for these patches.
