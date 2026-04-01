# CLAUDE.md — space-wine

## What This Repository Is

Upstream-first Wine patch tracks with deterministic standalone verification.

Supported baselines:

- `wine-11.5` — primary baseline
- `wine-10.0` — backport baseline

This repository is organized around subsystem review, deterministic tests, a local
macOS workaround, and cross-platform CI evidence.

## Critical Rules

- Never modify system Wine installations.
- Prefer `/opt/wine-11.5` and `/opt/wine-10.0` over a shared `/opt/wine`.
- Keep `11.5` as the source-of-truth line unless the user explicitly asks otherwise.
- Keep `10.0` support versioned through backports or explicit exclusions.
- Do not present application-specific collateral as the primary review evidence.
- Prefer subsystem-and-behavior patch titles over local symptom labels.
- Treat `UPSTREAMING.md` and `patches/README.md` as the primary reviewer docs.

## Patch Tracks

Upstream candidates:

- [`patches/series-11.5.txt`](/Users/tim/code/gh/livingstaccato/space-wine/.worktrees/codex/review-ready/patches/series-11.5.txt)
- [`patches/series-10.0.txt`](/Users/tim/code/gh/livingstaccato/space-wine/.worktrees/codex/review-ready/patches/series-10.0.txt)

Local-only workarounds:

- [`workarounds/series-11.5.txt`](/Users/tim/code/gh/livingstaccato/space-wine/.worktrees/codex/review-ready/workarounds/series-11.5.txt)
- [`workarounds/series-10.0.txt`](/Users/tim/code/gh/livingstaccato/space-wine/.worktrees/codex/review-ready/workarounds/series-10.0.txt)

## Build Shortcuts

```bash
# Primary baseline
make prove

# Backport baseline
make prove WINE_VERSION=10.0
```

## CI Contract

- Windows: standalone tests on real Windows
- Linux: patched and unpatched `10.0` and `11.5`
- macOS: patched `11.5`

## Secondary Collateral

Application-specific notes and artifacts live under [`support/twgs/`](/Users/tim/code/gh/livingstaccato/space-wine/.worktrees/codex/review-ready/support/twgs/). They are not part of the primary SME review path.
