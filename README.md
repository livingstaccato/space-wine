# space-wine: NtLockFile / NtUnlockFile FIXME Patches for Wine

[![Prove NtLockFile Fix](https://github.com/livingstaccato/space-wine/actions/workflows/prove.yml/badge.svg)](https://github.com/livingstaccato/space-wine/actions/workflows/prove.yml)

Fixes three longstanding FIXMEs in Wine's `dlls/ntdll/unix/file.c` that cause file
locking to reject valid parameters or hang forever on contested overlapped locks.

**Base:** Wine 11.0 (`db11d0fe6a1`)

**CI verifies:** Tests run on real Windows (ground truth) and Linux Wine (unpatched vs patched).
See [TRADEOFFS.md](TRADEOFFS.md) for design decisions and the blocking-vs-async tradeoff.

## The Bugs

### FIXME #1: `if (apc || io_status || key)` → STATUS_NOT_IMPLEMENTED

```c
// Wine 11.0 NtLockFile (line 6913)
if (apc || io_status || key)
{
    FIXME("Unimplemented yet parameter\n");
    return STATUS_NOT_IMPLEMENTED;
}
```

**Impact:** Any caller passing `io_status` (which is *every* normal caller), a lock
`key`, or an APC routine gets `STATUS_NOT_IMPLEMENTED`. This breaks file locking for
any application that uses the standard Windows API calling convention.

**Fix:** Remove the guard. Handle each parameter:
- `key` → ignore with `WARN` (kernel filter driver feature, no Wine driver needs it)
- `io_status` → write `STATUS_SUCCESS` + `Information = 0` on success
- `apc` → queue via `NtQueueApcThread` on success (matches existing Wine pattern)

### FIXME #2: Contested async lock → hangs forever

```c
// Wine 11.0 NtLockFile (line 6939)
if (async)
{
    FIXME( "Async I/O lock wait not implemented, might deadlock\n" );
    if (handle) NtClose( handle );
    return STATUS_PENDING;
}
```

**Impact:** When an overlapped file handle contends on a lock held by another handle,
Wine returns `STATUS_PENDING` and **never completes the lock**. The calling thread
hangs forever. This is the bug that causes multi-threaded game servers (TWGS),
VMware vSphere client installers, and Newsbin to freeze.

**Fix:** Remove the early return. Let overlapped handles fall through to the same
blocking wait-and-retry loop that synchronous handles use. The existing success path
already handles event signaling and IOCP posting.

### FIXME #3: NtUnlockFile `if (key)` → STATUS_NOT_IMPLEMENTED

```c
// Wine 11.0 NtUnlockFile (line 6972)
if (key)
{
    FIXME("Unimplemented yet parameter\n");
    return (io_status->Status = STATUS_NOT_IMPLEMENTED);
}
```

**Impact:** Any unlock call with a key parameter fails.

**Fix:** Replace with `WARN` and proceed normally (same as FIXME #1 key handling).

### FIXME #4: OEM_CHARSET (255) → "Untranslated charset" in font matching

```c
// Wine 11.0 dlls/win32u/font.c find_matching_face (line 2273)
if (!translate_charset_info( ... ))
{
    if (lf->lfCharSet != DEFAULT_CHARSET) FIXME( "Untranslated charset %d\n", lf->lfCharSet );
    csi->fs.fsCsb[0] = 0;  // falls back to generic matching
}
```

**Impact:** Any app requesting fonts with OEM_CHARSET (DOS terminal fonts) gets a
FIXME and falls back to generic font matching instead of charset-specific matching.
Affects TWGS and any DOS-era BBS/terminal application running under Wine.

**Fix:** Map OEM_CHARSET to the system OEM codepage (`oem_cp.CodePage`) via
`translate_charset_info(TCI_SRCCODEPAGE)` before the FIXME fallback.

## Affected Software

| Software | Bug(s) Hit | Symptom | Wine Bug |
|---|---|---|---|
| **TWGS (Trade Wars 2002 Gold)** | #1, #2 | Server hangs on startup, ports never open | — |
| **VMware vSphere Client 4-6** | #1, #2 | Installer hangs | [Bug 40827](https://bugs.winehq.org/show_bug.cgi?id=40827) |
| **Newsbin (Usenet client)** | #1, #2 | Page faults, critical section timeouts | [Bug 21112](https://bugs.winehq.org/show_bug.cgi?id=21112) |
| **winetricks vcrun2005/2008** | #1 | NtLockFile FIXME in logs | — |
| **winetricks dotnet20** | #1 | NtLockFile FIXME in logs | [Issue #589](https://github.com/Winetricks/winetricks/issues/589) |
| **wine-mono installer** | #1 | System slowdown during install | — |
| **Any multi-threaded app with overlapped file locks** | #2 | Hang on lock contention | — |

## Repository Structure

```
patches/                               # git apply compatible patch files
  ntdll-fix-NtLockFile-FIXMEs.patch    # NtLockFile/NtUnlockFile + IOCP completion
  kernelbase-fix-UnlockFileEx.patch    # UnlockFileEx overlapped I/O
  win32u-fix-OEM_CHARSET.patch         # OEM_CHARSET font matching
  user32-fix-edit-BuildLineDefs.patch   # Edit control stability (user32)
  comctl32-fix-edit-BuildLineDefs.patch # Edit control stability (comctl32_v6)
  kernel32-tests-expand-lockfile.patch  # Expanded lock tests for Wine test suite

tests/
  locktest.c           # NtLockFile/NtUnlockFile verification (unit tests)
  lockstress.c         # Multi-threaded contention stress test (hang reproducer)
  fonttest.c           # OEM_CHARSET font matching test
  edittest.c           # Edit control stability test
  kernel32_file_test.c # Full Wine kernel32 test file with expanded lock tests

results/               # Captured before/after test output
wine/                  # Wine source (submodule, branch wine-11.0-clean)
BUILDING.md            # How to build and test on macOS Apple Silicon
```

## Quick Test

```bash
# Compile test tools (MinGW)
x86_64-w64-mingw32-gcc -o locktest.exe tests/locktest.c -lntdll
x86_64-w64-mingw32-gcc -o lockstress.exe tests/lockstress.c

# Run under Wine
wine locktest.exe -v     # 53 checks, all should pass on patched Wine
wine lockstress.exe      # 200 lock ops, should complete in ~12ms on patched Wine
```

## Test Results Summary

### locktest.exe (53 unit checks)

| | Unpatched Wine 11.0 | Patched |
|---|---|---|
| Passed | 7 | **53** |
| Failed | 33 | **0** |
| Skipped | 13 (cascading) | 0 |

### lockstress.exe (multi-threaded contention)

| | Unpatched Wine 11.0 | Patched |
|---|---|---|
| Completed | 168 / 200 | **200 / 200** |
| Hangs | **1** | 0 |
| Time | 5005 ms (timeout) | **12 ms** |

### TWGS Game Server

| | Unpatched Wine 11.0 | Patched |
|---|---|---|
| Port 2002 (telnet) | CLOSED | **OPEN** |
| Port 2003 (admin) | CLOSED | **OPEN** |
| Status | Hung during init | **Fully functional** |
| NtLockFile FIXMEs | N/A (never got that far) | **0** |

### winetricks Installers

| Package | Unpatched FIXMEs | Patched FIXMEs |
|---|---|---|
| vcrun2005 | 2 | **0** |
| vcrun2008 | 2 | **0** |
| dotnet20 | 2 | **0** |

## Applying the Patch

```bash
# From a Wine 11.0 source tree
cd wine
git apply /path/to/patches/ntdll-fix-NtLockFile-FIXMEs.patch
git apply /path/to/patches/kernel32-tests-expand-lockfile.patch
```

See [BUILDING.md](BUILDING.md) for macOS Apple Silicon cross-compilation instructions.
