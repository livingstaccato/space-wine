# Patch Queue

This directory is the primary reviewer-facing queue for upstream-candidate Wine work.

Tracked baselines:

- `wine-11.5`: primary implementation line
- `wine-10.0`: backport line

Local-only workarounds are kept separately under
[`workarounds/`](/Users/tim/code/gh/livingstaccato/space-wine/.worktrees/codex/review-ready/workarounds).

Series manifests:

- [`patches/series-11.5.txt`](/Users/tim/code/gh/livingstaccato/space-wine/.worktrees/codex/review-ready/patches/series-11.5.txt)
- [`patches/series-10.0.txt`](/Users/tim/code/gh/livingstaccato/space-wine/.worktrees/codex/review-ready/patches/series-10.0.txt)
- [`workarounds/series-11.5.txt`](/Users/tim/code/gh/livingstaccato/space-wine/.worktrees/codex/review-ready/workarounds/series-11.5.txt)
- [`workarounds/series-10.0.txt`](/Users/tim/code/gh/livingstaccato/space-wine/.worktrees/codex/review-ready/workarounds/series-10.0.txt)

## Patch Status

| Patch | Subsystem | 11.5 | 10.0 | Upstream status | Deterministic coverage |
|---|---|---|---|---|---|
| `ntdll-complete-contested-lock-requests.patch` | `ntdll` | native patch | dedicated backport | needs stronger evidence | `locktest`, `lockstress` |
| `kernelbase-signal-unlockfileex-events.patch` | `kernelbase` | native patch | same patch | ready | `kernel32/tests`, `locktest` |
| `win32u-map-oem-charset.patch` | `win32u` | native patch | same patch | ready | `fonttest` |
| `user32-rebuild-stale-multiline-edit-lines.patch` | `user32` | native patch | same patch | needs stronger evidence | `edittest` |
| `comctl32-rebuild-stale-multiline-edit-lines.patch` | `comctl32_v6` | native patch | not applicable | needs split | `edittest` |
| `wineserver-close-released-lock-fds.patch` | `server` | native patch | same patch | ready | `fdleaktest` |
| `kernel32-tests-expand-locking-coverage.patch` | `kernel32/tests` | native patch | same patch | ready | Wine test suite |

Status meanings:

- `ready`: already shaped like a Wine subsystem patch
- `needs split`: should be divided before upstream submission
- `needs stronger evidence`: deterministic coverage exists, but native-behavior proof or narrower tests should be improved

## Applicability Notes

- `ntdll` uses a dedicated `10.0` backport because the older tree has a different `NtUnlockFile` shape.
- `comctl32_v6` is `11.5`-only because `dlls/comctl32_v6/edit.c` is absent in `wine-10.0`.
- The Rosetta workaround is intentionally excluded from this queue and remains local-only.
