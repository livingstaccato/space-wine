# Design Tradeoffs

## Contested Lock: Blocking vs True Async

### What Windows does

When `NtLockFile` is called on an overlapped file handle and the requested byte range
is already locked by another handle, Windows returns `STATUS_PENDING` and completes
the lock asynchronously — the kernel maintains an internal queue of pending lock
requests and grants them in order as locks are released. The calling thread remains
free to do other work. When the lock is eventually granted, the kernel signals the
event, posts to the I/O completion port, and/or queues the APC.

### What Wine 11.0 does (unpatched)

Returns `STATUS_PENDING` and **never completes the lock**. The event is never
signaled, the IOCP never receives a completion, and the APC is never queued. The
calling thread hangs forever if it waits on any of these. Because the lock is never
granted or released, the underlying file descriptor remains held in a locked state —
other threads contending on the same range also hang, creating a cascade of leaked
file descriptors and frozen threads.

```c
if (async)
{
    FIXME( "Async I/O lock wait not implemented, might deadlock\n" );
    if (handle) NtClose( handle );
    return STATUS_PENDING;  // ← never completed
}
```

### What our patch does

Removes the early return. Overlapped handles fall through to the same blocking
wait-and-retry loop that synchronous handles use:

```c
if (handle)
{
    NtWaitForSingleObject( handle, FALSE, NULL );  // blocks until lock available
    NtClose( handle );
}
else
{
    LARGE_INTEGER time;
    time.QuadPart = -100 * (ULONGLONG)10000;  // 100ms
    NtDelayExecution( FALSE, &time );          // sleep and retry
}
```

When the lock is acquired, the success path signals the event, posts IOCP, and
queues APC — matching Windows behavior for the completion notification.

### What this means

| Scenario | Windows | Wine (unpatched) | Wine (patched) |
|---|---|---|---|
| Uncontested lock | Immediate success | Immediate success | Immediate success |
| Contested, short wait (<1s) | Async, thread free | **Infinite hang** | Blocks briefly, then succeeds |
| Contested, long wait (>1s) | Async, thread free | **Infinite hang** | **Blocks thread** (not ideal) |
| Event signaled on completion | Yes | Never | Yes |
| IOCP posted on completion | Yes | Never | Yes |
| APC queued on completion | Yes | Never | Yes |

### Who is affected by the blocking behavior?

Applications that:
1. Lock files from a UI thread AND
2. Expect the thread to remain responsive during contention AND
3. Hold locks for more than a few hundred milliseconds

In practice, the affected software we've identified (TWGS, VMware vSphere installer,
Newsbin, winetricks/dotnet installers) all do file locking on I/O or worker threads
where brief blocking is acceptable. Lock contention windows in these applications are
typically milliseconds.

### Why not implement true async?

True async lock completion in Wine would require:

1. **Server-side async queue** in `server/fd.c` — maintain a list of pending lock
   requests per file descriptor
2. **Protocol extension** in `server/protocol.def` — new request/reply for async
   lock completion notifications
3. **Async infrastructure** in `server/async.c` — tie lock completion into Wine's
   existing async I/O framework (used by read/write/ioctl)
4. **Client-side async handling** in `dlls/ntdll/unix/file.c` — register the pending
   lock and handle completion callbacks

This is a significant architectural change touching the wineserver protocol, the
server's file descriptor management, and the async I/O subsystem. It's the right
long-term fix but is out of scope for a targeted patch that makes broken software
work.

### Why blocking is strictly better than the status quo

- **Before:** Infinite hang (STATUS_PENDING, never completes)
- **After:** Brief block, then correct completion with all notifications

Any application that hung before now works. No application that worked before is
broken, because the only changed behavior is for contested overlapped locks — which
previously always hung. There is no regression path.

## APC on Immediate Success (Wine Deviation)

**Windows behavior (verified via GitHub Actions CI on windows-latest):**
When NtLockFile succeeds immediately (no contention), Windows does NOT queue the APC.
APCs are only delivered for async completions — when `STATUS_PENDING` is returned and
the lock is later granted. `SleepEx(0, TRUE)` returns 0, not `WAIT_IO_COMPLETION`.

**Wine behavior (our patch):**
We queue the APC on immediate success via `NtQueueApcThread`. This is a minor
deviation from Windows but is harmless:
- Callers that check for APC delivery handle both paths
- No known software depends on APC *not* firing for immediate lock success
- The alternative (not queuing) would be more correct but is a behavior change
  we can't verify doesn't break something

**NULL io_status:**
Windows requires a non-NULL `io_status` parameter — passing NULL causes
`STATUS_ACCESS_VIOLATION` (0xC0000005). Wine handles NULL gracefully. This is
a defensive difference that doesn't affect real callers (all Win32 API paths
provide io_status).

## Key Parameter: `key` (Ignored)

Windows uses the `key` parameter in NtLockFile/NtUnlockFile to associate lock ranges
with specific callers — primarily used by kernel-mode filter drivers. Wine's
wineserver does not track lock keys; locks are identified by file handle + byte range.

Our patch ignores the key with a `WARN` trace. This is correct for all known Wine
use cases — no Wine-supported driver uses lock keys. If a future driver needs key
support, it would require wineserver changes to track keys alongside lock ranges.

## PE-Side Verification

`NtLockFile` is declared as `-syscall` in `dlls/ntdll/ntdll.spec`, meaning the PE
(Windows) side is a thin syscall stub that immediately transitions to the Unix side
(`dlls/ntdll/unix/file.c`). There is no PE-side logic to patch. Verified by
examining:

- `ntdll.spec` line 260: `@ stdcall -syscall NtLockFile(...)`
- `unix/syscall.c`: WRAP_FUNC thunk for argument marshaling only
- `signal_arm64ec.c`: DEFINE_SYSCALL declaration only
- `ntsyscalls.h`: syscall table entry only
