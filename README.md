# space-wine

[![Verify Wine Patch Matrix](https://github.com/livingstaccato/space-wine/actions/workflows/prove.yml/badge.svg)](https://github.com/livingstaccato/space-wine/actions/workflows/prove.yml)

`space-wine` is an upstream-first Wine patch queue with deterministic standalone
verification. The repository is organized to make subsystem review, revision rounds,
and cross-version evidence easy for Wine maintainers to evaluate.

## Baselines and Scope

| Role | Upstream tag | Default install prefix | CI scope |
|---|---|---|---|
| Primary baseline | `wine-11.5` | `/opt/wine-11.5` | Windows ground truth, Linux patched/unpatched, macOS patched |
| Backport baseline | `wine-10.0` | `/opt/wine-10.0` | Windows ground truth, Linux patched/unpatched |

There is no earlier maintained `11.x` review target. `11.5` is the only primary line
in scope.

## Upstream Submission Surface

Primary reviewer-facing paths:

- [`patches/README.md`](/Users/tim/code/gh/livingstaccato/space-wine/.worktrees/codex/review-ready/patches/README.md): subsystem queue and status table
- [`UPSTREAMING.md`](/Users/tim/code/gh/livingstaccato/space-wine/.worktrees/codex/review-ready/UPSTREAMING.md): expected Wine review flow and submission rules
- [`BUILDING.md`](/Users/tim/code/gh/livingstaccato/space-wine/.worktrees/codex/review-ready/BUILDING.md): baseline-specific build instructions
- [`tests/`](/Users/tim/code/gh/livingstaccato/space-wine/.worktrees/codex/review-ready/tests): standalone verification tools

Secondary material remains under [`support/`](/Users/tim/code/gh/livingstaccato/space-wine/.worktrees/codex/review-ready/support), but it is not part of the primary upstream case.

## Patch Groups

### Upstream candidates

- `ntdll`: contested file-lock completion
- `kernelbase`: `UnlockFileEx` event completion
- `win32u`: OEM charset mapping
- `user32`: stale multiline edit line-list rebuild
- `comctl32_v6`: stale multiline edit line-list rebuild
- `wineserver`: released lock FD cleanup
- `kernel32/tests`: lock and unlock coverage expansion

### Local-only workarounds

- `wow64cpu`: Rosetta 2 workaround for the macOS `11.5` line only

## Review Status Model

Every queued patch is classified in [`patches/README.md`](/Users/tim/code/gh/livingstaccato/space-wine/.worktrees/codex/review-ready/patches/README.md) with:

- subsystem
- baseline applicability
- upstream status
- deterministic test coverage

Status values:

- `ready`: suitable for subsystem review as-is
- `needs split`: behavior is promising but should be divided before review
- `needs stronger evidence`: deterministic tests/native comparison should be improved
- `backport only`: carried only to keep the `10.0` line aligned
- `local-only`: not intended for upstream submission

## Verification Contract

- Windows ground truth runs the standalone tools on real Windows kernels.
- Linux CI compares patched and unpatched `10.0` and `11.5`.
- macOS CI validates the patched `11.5` line.
- Local verification writes generated output under [`build/results/`](/Users/tim/code/gh/livingstaccato/space-wine/.worktrees/codex/review-ready/build/results).

Standalone tools:

- `locktest.exe`: parameter handling, APC, IOCP, and lock semantics
- `lockstress.exe`: contested overlapped lock completion
- `fonttest.exe`: OEM charset regression coverage
- `edittest.exe`: multiline edit-control stability
- `fdleaktest.exe`: released lock FD regression coverage

## Quick Start

```bash
# Primary baseline
make prove

# Backport baseline
make prove WINE_VERSION=10.0
```

The Makefile clones the selected upstream tag, applies the appropriate patch and
workaround series, builds Wine, and runs the standalone verification suite.

## Repository Layout

```text
patches/                 upstream-candidate patch queue and series manifests
workarounds/             local-only workaround series
tests/                   deterministic standalone verification tools
tools/                   patch-series helper scripts
.github/workflows/       cross-platform CI matrix
support/                 archival, non-primary collateral
results/                 generated-output note only
```

## Notes for Reviewers

- The primary implementation line is `wine-11.5`.
- The backport line is `wine-10.0`.
- The patch queue is intentionally structured around subsystem review and revision rounds.
- Local-only workarounds are separated from upstream candidates.
- Application-specific collateral is archival and not required to evaluate the fixes.
