# Design Tradeoffs

This file records implementation tradeoffs that may matter during upstream review.
It is not part of the primary repo introduction; use
[`README.md`](/Users/tim/code/gh/livingstaccato/space-wine/.worktrees/codex/review-ready/README.md)
and [`UPSTREAMING.md`](/Users/tim/code/gh/livingstaccato/space-wine/.worktrees/codex/review-ready/UPSTREAMING.md)
for the main review path.

## Contested Overlapped Locks: Blocking vs True Async

### Windows behavior

When `NtLockFile` is issued on an overlapped handle and the target byte range is
already locked, Windows returns `STATUS_PENDING` and later completes the operation
asynchronously. Completion is communicated through the event, IOCP, and/or APC path.

### Unpatched Wine behavior

On the affected baselines, the overlapped lock path returns `STATUS_PENDING` and never
completes. The practical failure mode is an indefinite wait:

- the event is never signaled
- IOCP does not receive completion
- APC is not queued
- follow-on lock contention can cascade into leaked state

### Patched behavior

The patch removes the non-completing early return and reuses the existing wait/retry
path. This is not a true async implementation; it is a correctness-oriented backstop
that converts an infinite hang into bounded waiting followed by normal completion.

### Tradeoff

| Scenario | Windows | Unpatched Wine | Patched Wine |
|---|---|---|---|
| Uncontested lock | Immediate success | Immediate success | Immediate success |
| Contested lock | Async completion | Never completes | Blocks, then completes |
| Event completion | Yes | No | Yes |
| IOCP completion | Yes | No | Yes |
| APC completion | Yes | No | Yes |

### Why not implement true async?

True async lock completion would require wineserver protocol and async-infrastructure
changes across `server/` and `dlls/ntdll/unix/`. That is the correct long-term design,
but it is materially larger than the patchset maintained here.

The maintained tradeoff is therefore:

- preserve normal immediate-success behavior
- restore completion semantics for contested overlapped locks
- avoid a broader wineserver protocol redesign in this patch series

## APC on Immediate Success

Windows does not queue an APC for immediate lock success. The maintained patch path
does queue one. This is a documented behavioral deviation chosen to preserve the
existing completion plumbing in a small patch.

The deviation is accepted here because:

- it is deterministic
- standalone tests document it
- no supported scenario is known to depend on APC suppression for immediate success

## `key` Parameter Handling

The `key` parameter is ignored with a warning trace rather than implemented fully.

Reasoning:

- supported Wine userspace paths do not rely on keyed lock ownership
- wineserver does not track lock keys alongside byte ranges
- full key support would require protocol and server-state changes beyond the scope of this patchset

## Version Scope

- `wine-11.5` is the primary implementation line
- `wine-10.0` is a backport line with explicit applicability differences
- local macOS workaround coverage is maintained only on `wine-11.5`
