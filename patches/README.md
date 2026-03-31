# Patch Catalog

This repository maintains two versioned patch tracks:

- `wine-11.5`: primary baseline
- `wine-10.0`: backport baseline

Patch categories:

- **Upstream candidates** live under `patches/`
- **Local-only workarounds** live under `workarounds/`

Series manifests:

- [`patches/series-11.5.txt`](/Users/tim/code/gh/livingstaccato/space-wine/.worktrees/codex/review-ready/patches/series-11.5.txt)
- [`patches/series-10.0.txt`](/Users/tim/code/gh/livingstaccato/space-wine/.worktrees/codex/review-ready/patches/series-10.0.txt)
- [`workarounds/series-11.5.txt`](/Users/tim/code/gh/livingstaccato/space-wine/.worktrees/codex/review-ready/workarounds/series-11.5.txt)
- [`workarounds/series-10.0.txt`](/Users/tim/code/gh/livingstaccato/space-wine/.worktrees/codex/review-ready/workarounds/series-10.0.txt)

## Applicability Matrix

| Patch | 11.5 | 10.0 | Notes |
|---|---|---|---|
| `ntdll-fix-NtLockFile-FIXMEs.patch` | Native patch | Backport variant | `10.0` uses an older `NtUnlockFile` shape, so it has a dedicated backport patch. |
| `kernelbase-fix-UnlockFileEx.patch` | Native patch | Same patch | Applies unchanged. |
| `win32u-fix-OEM_CHARSET.patch` | Native patch | Same patch | Applies unchanged. |
| `user32-fix-edit-BuildLineDefs.patch` | Native patch | Same patch | Applies unchanged. |
| `comctl32-fix-edit-BuildLineDefs.patch` | Native patch | Not applicable | `dlls/comctl32_v6/edit.c` is not present in `wine-10.0`. |
| `wineserver-fix-lock-fd-leak.patch` | Native patch | Same patch | Applies unchanged. |
| `kernel32-tests-expand-lockfile.patch` | Native patch | Same patch | Applies unchanged. |
| `wow64cpu-rosetta2-workaround.patch` | Local-only workaround | Not maintained | The Rosetta workaround is kept only on the `11.5` primary macOS line. |
