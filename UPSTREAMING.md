# Upstreaming Notes

This repository is intended to stage Wine patches in a shape that matches the way
Wine reviewers typically accept code.

## Submission Rules

- Submit one logical change per patch.
- Use subsystem-first patch titles such as `ntdll: ...`, `server: ...`, or `win32u: ...`.
- Keep local-only workarounds out of the upstream queue.
- Treat deterministic tests as first-class review artifacts, not optional support files.

## Expected Reviewer Evidence

- Add or expand Wine tests when behavior can be expressed in the Wine test suite.
- When a standalone tool is the only practical reproducer, cite its deterministic result explicitly.
- If native behavior is not obvious, document the Windows-observed result before claiming Wine should match it.
- Expect review rounds and resubmissions. The normal path is revised patches, not one-shot acceptance.

## Queue Expectations

- `ready`: already scoped for subsystem review
- `needs split`: separate behavior changes from test additions or unrelated cleanup
- `needs stronger evidence`: tighten deterministic coverage or add native-behavior notes first
- `backport only`: maintain only to keep the `wine-10.0` line aligned
- `local-only`: do not submit upstream

## Current Repo Conventions

- `wine-11.5` is the source-of-truth implementation line.
- `wine-10.0` is maintained through backports or explicit exclusions.
- [`patches/README.md`](/Users/tim/code/gh/livingstaccato/space-wine/.worktrees/codex/review-ready/patches/README.md) is the canonical queue/status index.
- [`BUILDING.md`](/Users/tim/code/gh/livingstaccato/space-wine/.worktrees/codex/review-ready/BUILDING.md) is for build instructions only.
- [`support/`](/Users/tim/code/gh/livingstaccato/space-wine/.worktrees/codex/review-ready/support/) is archival and not part of the primary upstream case.
