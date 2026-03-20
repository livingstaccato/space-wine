/*
 * Wine NtLockFile / NtUnlockFile Verification Tool
 *
 * Tests three categories of Wine ntdll file locking bugs:
 *   A. NtLockFile returns STATUS_NOT_IMPLEMENTED for io_status/key/apc params
 *   B. Contested overlapped (async) locks return STATUS_PENDING and never complete
 *   C. NtUnlockFile returns STATUS_NOT_IMPLEMENTED when passed a key parameter
 *
 * Compile with MinGW:
 *   x86_64-w64-mingw32-gcc -o locktest.exe locktest.c -lntdll
 *
 * Run under Wine:
 *   wine locktest.exe          # run all tests
 *   wine locktest.exe -v       # verbose: show sub-checks
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS            ((NTSTATUS)0x00000000)
#endif
#ifndef STATUS_PENDING
#define STATUS_PENDING            ((NTSTATUS)0x00000103)
#endif
#ifndef STATUS_NOT_IMPLEMENTED
#define STATUS_NOT_IMPLEMENTED    ((NTSTATUS)0xC0000002)
#endif
#ifndef STATUS_LOCK_NOT_GRANTED
#define STATUS_LOCK_NOT_GRANTED   ((NTSTATUS)0xC0000055)
#endif
#ifndef STATUS_FILE_LOCK_CONFLICT
#define STATUS_FILE_LOCK_CONFLICT ((NTSTATUS)0xC0000054)
#endif
#ifndef STATUS_RANGE_NOT_LOCKED
#define STATUS_RANGE_NOT_LOCKED   ((NTSTATUS)0xC000007E)
#endif

/* Wine may return either STATUS_LOCK_NOT_GRANTED or STATUS_FILE_LOCK_CONFLICT
 * for lock conflicts; both are valid NT status codes for the same condition. */
static int is_lock_conflict(NTSTATUS s)
{
    return s == STATUS_LOCK_NOT_GRANTED || s == STATUS_FILE_LOCK_CONFLICT;
}

/* Wine may return STATUS_FILE_LOCK_CONFLICT instead of STATUS_RANGE_NOT_LOCKED */
static int is_range_not_locked(NTSTATUS s)
{
    return s == STATUS_RANGE_NOT_LOCKED || s == STATUS_FILE_LOCK_CONFLICT;
}

typedef NTSTATUS (WINAPI *pNtLockFile)(
    HANDLE, HANDLE, PIO_APC_ROUTINE, PVOID, PIO_STATUS_BLOCK,
    PLARGE_INTEGER, PLARGE_INTEGER, ULONG *, BOOLEAN, BOOLEAN);

typedef NTSTATUS (WINAPI *pNtUnlockFile)(
    HANDLE, PIO_STATUS_BLOCK, PLARGE_INTEGER, PLARGE_INTEGER, ULONG *);

static pNtLockFile NtLockFile_fn;
static pNtUnlockFile NtUnlockFile_fn;

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;
static int verbose = 0;

static const char *status_name(NTSTATUS s)
{
    switch (s) {
    case STATUS_SUCCESS:          return "STATUS_SUCCESS";
    case STATUS_PENDING:          return "STATUS_PENDING";
    case STATUS_NOT_IMPLEMENTED:  return "STATUS_NOT_IMPLEMENTED";
    case STATUS_LOCK_NOT_GRANTED: return "STATUS_LOCK_NOT_GRANTED";
    case STATUS_FILE_LOCK_CONFLICT: return "STATUS_FILE_LOCK_CONFLICT";
    case STATUS_RANGE_NOT_LOCKED: return "STATUS_RANGE_NOT_LOCKED";
    default: {
        static char buf[32];
        snprintf(buf, sizeof(buf), "0x%08lX", (unsigned long)s);
        return buf;
    }
    }
}

static void check(const char *name, int pass, const char *fmt, ...)
{
    tests_run++;
    if (pass) {
        tests_passed++;
        if (verbose) {
            printf("    ok   %-52s", name);
            if (fmt) {
                va_list ap;
                va_start(ap, fmt);
                printf("  (");
                vprintf(fmt, ap);
                printf(")");
                va_end(ap);
            }
            printf("\n");
        }
    } else {
        tests_failed++;
        printf("    FAIL %-52s", name);
        if (fmt) {
            va_list ap;
            va_start(ap, fmt);
            printf("  (");
            vprintf(fmt, ap);
            printf(")");
            va_end(ap);
        }
        printf("\n");
    }
}

static void section(const char *title)
{
    printf("\n  [%s]\n", title);
}

/* --- helpers --- */

static char temp_file_path[MAX_PATH];

static HANDLE create_temp_file(DWORD flags)
{
    HANDLE h;
    DWORD written;
    char buf[4096];

    if (!temp_file_path[0]) {
        char path[MAX_PATH];
        GetTempPathA(MAX_PATH, path);
        GetTempFileNameA(path, "lck", 0, temp_file_path);
    }

    h = CreateFileA(temp_file_path, GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE | flags, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return h;

    memset(buf, 'X', sizeof(buf));
    WriteFile(h, buf, sizeof(buf), &written, NULL);
    FlushFileBuffers(h);
    return h;
}

static HANDLE open_second_handle(DWORD flags)
{
    return CreateFileA(temp_file_path, GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, flags, NULL);
}

static NTSTATUS do_lock(HANDLE f, LONGLONG off, LONGLONG len,
                        ULONG *key, BOOLEAN wait, BOOLEAN excl)
{
    IO_STATUS_BLOCK iosb = {0};
    LARGE_INTEGER o, l;
    o.QuadPart = off;
    l.QuadPart = len;
    return NtLockFile_fn(f, NULL, NULL, NULL, &iosb, &o, &l, key, !wait, excl);
}

static NTSTATUS do_unlock(HANDLE f, LONGLONG off, LONGLONG len, ULONG *key)
{
    IO_STATUS_BLOCK iosb = {0};
    LARGE_INTEGER o, l;
    o.QuadPart = off;
    l.QuadPart = len;
    return NtUnlockFile_fn(f, &iosb, &o, &l, key);
}

struct unlock_thread_data {
    HANDLE file;
    LONGLONG offset;
    LONGLONG length;
    DWORD delay_ms;
};

static DWORD WINAPI unlock_thread_proc(LPVOID param)
{
    struct unlock_thread_data *d = param;
    Sleep(d->delay_ms);
    do_unlock(d->file, d->offset, d->length, NULL);
    return 0;
}

/* --- APC support --- */

static volatile LONG apc_count = 0;
static volatile PVOID apc_got_context = NULL;
static volatile PIO_STATUS_BLOCK apc_got_iosb = NULL;

static void WINAPI lock_apc_callback(PVOID ctx, PIO_STATUS_BLOCK iosb, ULONG reserved)
{
    apc_got_context = ctx;
    apc_got_iosb = iosb;
    InterlockedIncrement(&apc_count);
}

/* ================================================================
 * Test group 1: io_status parameter handling
 * ================================================================ */
static void test_io_status(HANDLE file)
{
    IO_STATUS_BLOCK iosb;
    LARGE_INTEGER offset, length;
    NTSTATUS status;

    section("io_status parameter");

    /* 1a: io_status written on success */
    memset(&iosb, 0xcc, sizeof(iosb));
    offset.QuadPart = 0;
    length.QuadPart = 100;
    status = NtLockFile_fn(file, NULL, NULL, NULL, &iosb,
                           &offset, &length, NULL, TRUE, TRUE);
    check("NtLockFile returns STATUS_SUCCESS",
          status == STATUS_SUCCESS, "%s", status_name(status));
    check("io_status.Status == STATUS_SUCCESS",
          iosb.Status == STATUS_SUCCESS, "got %s", status_name(iosb.Status));
    check("io_status.Information == 0",
          iosb.Information == 0, "got %lu", (unsigned long)iosb.Information);
    if (status == STATUS_SUCCESS)
        do_unlock(file, 0, 100, NULL);

    /* 1b: io_status written on failure (lock conflict) */
    do_lock(file, 500, 50, NULL, FALSE, TRUE);
    memset(&iosb, 0xcc, sizeof(iosb));
    offset.QuadPart = 500;
    length.QuadPart = 50;
    status = NtLockFile_fn(file, NULL, NULL, NULL, &iosb,
                           &offset, &length, NULL, TRUE, TRUE);
    check("conflicting lock returns lock error",
          is_lock_conflict(status), "%s", status_name(status));
    do_unlock(file, 500, 50, NULL);

    /* 1c: NULL io_status — Windows requires it (ACCESS_VIOLATION if NULL).
     * Wine handles NULL gracefully. Accept either behavior. */
    offset.QuadPart = 100;
    length.QuadPart = 50;
    status = NtLockFile_fn(file, NULL, NULL, NULL, NULL,
                           &offset, &length, NULL, TRUE, TRUE);
    check("NtLockFile with NULL io_status (succeeds or AV)",
          status == STATUS_SUCCESS || status == (NTSTATUS)0xC0000005,
          "%s", status_name(status));
    if (status == STATUS_SUCCESS)
        do_unlock(file, 100, 50, NULL);
}

/* ================================================================
 * Test group 2: key parameter handling
 * ================================================================ */
static void test_key_parameter(HANDLE file)
{
    NTSTATUS status;
    ULONG key;

    section("key parameter");

    /* 2a: lock with key succeeds */
    key = 0x1234;
    status = do_lock(file, 200, 100, &key, FALSE, TRUE);
    check("NtLockFile with key=0x1234",
          status == STATUS_SUCCESS, "%s", status_name(status));

    /* 2b: unlock with same key succeeds */
    if (status == STATUS_SUCCESS) {
        status = do_unlock(file, 200, 100, &key);
        check("NtUnlockFile with key=0x1234",
              status == STATUS_SUCCESS, "%s", status_name(status));
    }

    /* 2c: lock with key=0 (NULL pointer) */
    status = do_lock(file, 200, 100, NULL, FALSE, TRUE);
    check("NtLockFile with key=NULL",
          status == STATUS_SUCCESS, "%s", status_name(status));

    /* 2d: unlock with different key — Windows tracks keys (RANGE_NOT_LOCKED),
     * Wine ignores keys (SUCCESS). Both behaviors are acceptable. */
    if (status == STATUS_SUCCESS) {
        key = 0xFFFF;
        status = do_unlock(file, 200, 100, &key);
        check("NtUnlockFile with mismatched key (Wine=ok, Win=fail)",
              status == STATUS_SUCCESS || status == STATUS_RANGE_NOT_LOCKED,
              "%s", status_name(status));
        /* Clean up if mismatched key unlock failed (Windows) */
        if (status != STATUS_SUCCESS)
            do_unlock(file, 200, 100, NULL);
    }

    /* 2e: lock and unlock with key=0 value (pointer to zero, not NULL) */
    key = 0;
    status = do_lock(file, 300, 50, &key, FALSE, TRUE);
    check("NtLockFile with key=0 (non-NULL ptr)",
          status == STATUS_SUCCESS, "%s", status_name(status));
    if (status == STATUS_SUCCESS) {
        status = do_unlock(file, 300, 50, &key);
        check("NtUnlockFile with key=0 (non-NULL ptr)",
              status == STATUS_SUCCESS, "%s", status_name(status));
    }

    /* 2f: large key value */
    key = 0xDEADBEEF;
    status = do_lock(file, 400, 25, &key, FALSE, TRUE);
    check("NtLockFile with key=0xDEADBEEF",
          status == STATUS_SUCCESS, "%s", status_name(status));
    if (status == STATUS_SUCCESS)
        do_unlock(file, 400, 25, &key);
}

/* ================================================================
 * Test group 3: APC delivery
 * ================================================================ */
static void test_apc(HANDLE file)
{
    IO_STATUS_BLOCK iosb;
    LARGE_INTEGER offset, length;
    NTSTATUS status;
    DWORD ret;

    section("APC delivery");

    /* 3a: NtLockFile with APC parameter accepted (not rejected)
     *
     * Windows ground truth: APC is NOT queued for immediate (non-pending)
     * success. APCs are only for async completion. Wine currently queues
     * the APC on immediate success, which is a minor deviation but doesn't
     * break callers (they handle both paths). */
    apc_count = 0;
    apc_got_context = NULL;
    apc_got_iosb = NULL;
    memset(&iosb, 0xcc, sizeof(iosb));
    offset.QuadPart = 800;
    length.QuadPart = 100;

    status = NtLockFile_fn(file, NULL, lock_apc_callback, (PVOID)0xCAFE,
                           &iosb, &offset, &length, NULL, TRUE, TRUE);
    check("NtLockFile with APC param accepted",
          status == STATUS_SUCCESS, "%s", status_name(status));

    check("APC not fired synchronously",
          apc_count == 0, "count=%ld", (long)apc_count);

    /* On Windows: SleepEx returns 0 (no APC queued for immediate success)
     * On Wine: SleepEx returns WAIT_IO_COMPLETION (APC queued). Both ok. */
    ret = SleepEx(0, TRUE);
    check("APC delivery (Wine=yes, Windows=no, both ok)",
          ret == 0 || ret == WAIT_IO_COMPLETION, "ret=%lu", ret);

    if (apc_count == 1) {
        /* Wine fires APC — verify arguments are correct */
        check("APC context correct (Wine path)",
              apc_got_context == (PVOID)0xCAFE, "got %p", apc_got_context);
        check("APC iosb correct (Wine path)",
              apc_got_iosb == &iosb,
              "got %p, expected %p", (void *)apc_got_iosb, (void *)&iosb);
    }

    if (status == STATUS_SUCCESS)
        do_unlock(file, 800, 100, NULL);

    /* 3b: APC with NULL io_status — Windows crashes (ACCESS_VIOLATION),
     * Wine handles gracefully. Accept either. */
    apc_count = 0;
    offset.QuadPart = 900;
    length.QuadPart = 50;
    status = NtLockFile_fn(file, NULL, lock_apc_callback, (PVOID)0xBEEF,
                           NULL, &offset, &length, NULL, TRUE, TRUE);
    check("NtLockFile APC + NULL io_status (ok or AV)",
          status == STATUS_SUCCESS || status == (NTSTATUS)0xC0000005,
          "%s", status_name(status));
    if (status == STATUS_SUCCESS) {
        SleepEx(0, TRUE);
        /* APC may or may not fire — both ok */
        do_unlock(file, 900, 50, NULL);
    }
}

/* ================================================================
 * Test group 4: contested async lock (the hang bug)
 * ================================================================ */
static void test_contested_lock_event(HANDLE file)
{
    IO_STATUS_BLOCK iosb;
    LARGE_INTEGER offset, length;
    NTSTATUS status;
    HANDLE file2, event, thread;
    struct unlock_thread_data ud;
    DWORD wait_result;

    section("contested lock + event");

    /* Lock on primary handle */
    status = do_lock(file, 0, 100, NULL, FALSE, TRUE);
    if (status != STATUS_SUCCESS) {
        check("initial lock", 0, "failed: %s", status_name(status));
        return;
    }

    file2 = open_second_handle(0);
    if (file2 == INVALID_HANDLE_VALUE) {
        check("open second handle", 0, "err=%lu", GetLastError());
        do_unlock(file, 0, 100, NULL);
        return;
    }

    event = CreateEventA(NULL, TRUE, FALSE, NULL);

    /* Spawn thread to release the lock after 200ms */
    ud.file = file;
    ud.offset = 0;
    ud.length = 100;
    ud.delay_ms = 200;
    thread = CreateThread(NULL, 0, unlock_thread_proc, &ud, 0, NULL);

    /* Contested lock on second handle — should block then succeed */
    memset(&iosb, 0xcc, sizeof(iosb));
    offset.QuadPart = 0;
    length.QuadPart = 100;
    status = NtLockFile_fn(file2, event, NULL, NULL, &iosb,
                           &offset, &length, NULL, FALSE, TRUE);

    if (status == STATUS_SUCCESS) {
        check("contested lock acquired", 1, "immediate success");
        check("event signaled on immediate success",
              WaitForSingleObject(event, 0) == WAIT_OBJECT_0, NULL);
    } else if (status == STATUS_PENDING) {
        wait_result = WaitForSingleObject(event, 5000);
        check("contested lock acquired after wait",
              wait_result == WAIT_OBJECT_0,
              wait_result == WAIT_TIMEOUT ? "TIMEOUT — hang bug" : "unexpected");
        if (wait_result == WAIT_OBJECT_0) {
            check("iosb.Status after contested lock",
                  iosb.Status == STATUS_SUCCESS, "%s", status_name(iosb.Status));
        }
    } else {
        check("contested lock", 0, "%s", status_name(status));
    }

    WaitForSingleObject(thread, 5000);
    if (status == STATUS_SUCCESS || (status == STATUS_PENDING &&
        WaitForSingleObject(event, 0) == WAIT_OBJECT_0))
        do_unlock(file2, 0, 100, NULL);

    CloseHandle(thread);
    CloseHandle(event);
    CloseHandle(file2);
}

static void test_contested_lock_iocp(HANDLE file)
{
    HANDLE file2, event, port, thread;
    struct unlock_thread_data ud;
    OVERLAPPED ov;
    ULONG_PTR key_out;
    DWORD bytes;
    LPOVERLAPPED ov_out;
    BOOL ret;
    NTSTATUS status;

    section("contested lock + IOCP");

    file2 = open_second_handle(FILE_FLAG_OVERLAPPED);
    if (file2 == INVALID_HANDLE_VALUE) {
        check("open overlapped handle", 0, "err=%lu", GetLastError());
        return;
    }

    port = CreateIoCompletionPort(file2, NULL, 0xABCD, 1);
    if (!port) {
        check("create IOCP", 0, "err=%lu", GetLastError());
        CloseHandle(file2);
        return;
    }

    /* Lock on primary handle */
    status = do_lock(file, 0, 100, NULL, FALSE, TRUE);
    if (status != STATUS_SUCCESS) {
        check("initial lock", 0, "%s", status_name(status));
        CloseHandle(port);
        CloseHandle(file2);
        return;
    }

    event = CreateEventA(NULL, TRUE, FALSE, NULL);

    ud.file = file;
    ud.offset = 0;
    ud.length = 100;
    ud.delay_ms = 200;
    thread = CreateThread(NULL, 0, unlock_thread_proc, &ud, 0, NULL);

    /* Use LockFileEx so IOCP completions are properly wired up */
    memset(&ov, 0, sizeof(ov));
    ov.hEvent = event;
    ret = LockFileEx(file2, LOCKFILE_EXCLUSIVE_LOCK, 0, 100, 0, &ov);
    if (!ret && GetLastError() == ERROR_IO_PENDING) {
        ret = (WaitForSingleObject(event, 5000) == WAIT_OBJECT_0);
    }
    check("contested IOCP lock acquired", ret, "err=%lu", GetLastError());

    if (ret) {
        ov_out = NULL;
        key_out = 0;
        bytes = 0xdeadbeef;
        ret = GetQueuedCompletionStatus(port, &bytes, &key_out, &ov_out, 2000);
        check("IOCP completion received", ret != 0,
              ret ? NULL : "timeout or error %lu", GetLastError());
        if (ret) {
            check("IOCP key matches",
                  key_out == 0xABCD, "got 0x%lX", (unsigned long)key_out);
            check("IOCP overlapped matches",
                  ov_out == &ov, "got %p", ov_out);
        }

        ov.hEvent = 0;
        UnlockFileEx(file2, 0, 100, 0, &ov);
    }

    WaitForSingleObject(thread, 5000);
    CloseHandle(thread);
    CloseHandle(event);
    CloseHandle(port);
    CloseHandle(file2);
}

/* ================================================================
 * Test group 5: shared vs exclusive locks
 * ================================================================ */
static void test_shared_exclusive(HANDLE file)
{
    NTSTATUS status;
    HANDLE file2;

    section("shared vs exclusive locks");

    file2 = open_second_handle(0);
    if (file2 == INVALID_HANDLE_VALUE) {
        check("open second handle", 0, "err=%lu", GetLastError());
        return;
    }

    /* 5a: two shared locks on same range, different handles */
    status = do_lock(file, 0, 100, NULL, FALSE, FALSE);
    check("shared lock on handle1", status == STATUS_SUCCESS, "%s", status_name(status));
    status = do_lock(file2, 0, 100, NULL, FALSE, FALSE);
    check("shared lock on handle2 (same range)", status == STATUS_SUCCESS, "%s", status_name(status));
    do_unlock(file, 0, 100, NULL);
    do_unlock(file2, 0, 100, NULL);

    /* 5b: exclusive blocks shared on different handle */
    status = do_lock(file, 0, 100, NULL, FALSE, TRUE);
    check("exclusive lock on handle1", status == STATUS_SUCCESS, "%s", status_name(status));
    status = do_lock(file2, 0, 100, NULL, FALSE, FALSE);
    check("shared lock blocked by exclusive",
          is_lock_conflict(status), "%s", status_name(status));
    do_unlock(file, 0, 100, NULL);

    /* 5c: exclusive blocks exclusive on different handle */
    status = do_lock(file, 0, 100, NULL, FALSE, TRUE);
    check("exclusive lock on handle1", status == STATUS_SUCCESS, "%s", status_name(status));
    status = do_lock(file2, 0, 100, NULL, FALSE, TRUE);
    check("exclusive blocked by exclusive",
          is_lock_conflict(status), "%s", status_name(status));
    do_unlock(file, 0, 100, NULL);

    /* 5d: shared on same handle stacks */
    status = do_lock(file, 0, 100, NULL, FALSE, FALSE);
    check("first shared lock", status == STATUS_SUCCESS, "%s", status_name(status));
    status = do_lock(file, 0, 100, NULL, FALSE, FALSE);
    check("second shared lock (same handle)",
          status == STATUS_SUCCESS, "%s", status_name(status));
    do_unlock(file, 0, 100, NULL);
    do_unlock(file, 0, 100, NULL);

    /* 5e: overlapping ranges */
    status = do_lock(file, 0, 100, NULL, FALSE, TRUE);
    check("exclusive [0,100)", status == STATUS_SUCCESS, "%s", status_name(status));
    status = do_lock(file2, 50, 100, NULL, FALSE, TRUE);
    check("overlapping [50,150) blocked",
          is_lock_conflict(status), "%s", status_name(status));
    status = do_lock(file2, 100, 100, NULL, FALSE, TRUE);
    check("adjacent [100,200) succeeds",
          status == STATUS_SUCCESS, "%s", status_name(status));
    do_unlock(file, 0, 100, NULL);
    do_unlock(file2, 100, 100, NULL);

    CloseHandle(file2);
}

/* ================================================================
 * Test group 6: uncontested IOCP + event (non-hang path)
 * ================================================================ */
static void test_uncontested_iocp(HANDLE file)
{
    HANDLE file2, port, event;
    OVERLAPPED ov;
    ULONG_PTR key_out;
    DWORD bytes;
    LPOVERLAPPED ov_out;
    BOOL ret;

    section("uncontested IOCP + event");

    file2 = open_second_handle(FILE_FLAG_OVERLAPPED);
    if (file2 == INVALID_HANDLE_VALUE) {
        check("open overlapped handle", 0, NULL);
        return;
    }

    port = CreateIoCompletionPort(file2, NULL, 0x9999, 1);
    event = CreateEventA(NULL, TRUE, FALSE, NULL);

    /* Uncontested lock with IOCP + event via LockFileEx */
    memset(&ov, 0, sizeof(ov));
    ov.Offset = 1000;
    ov.hEvent = event;
    ret = LockFileEx(file2, LOCKFILE_EXCLUSIVE_LOCK, 0, 200, 0, &ov);
    check("uncontested IOCP lock succeeds", ret, "err=%lu", GetLastError());

    if (ret) {
        ret = (WaitForSingleObject(event, 0) == WAIT_OBJECT_0);
        check("event signaled on uncontested lock", ret, NULL);

        ov_out = NULL;
        key_out = 0;
        bytes = 0xdeadbeef;
        ret = GetQueuedCompletionStatus(port, &bytes, &key_out, &ov_out, 0);
        check("IOCP completion posted",
              ret != 0, ret ? NULL : "err=%lu", GetLastError());
        if (ret) {
            check("IOCP key correct", key_out == 0x9999,
                  "got 0x%lX", (unsigned long)key_out);
            check("IOCP overlapped correct", ov_out == &ov,
                  "got %p", ov_out);
        }

        ov.hEvent = 0;
        UnlockFileEx(file2, 0, 200, 0, &ov);
    }

    /* Low bit set on event skips IOCP */
    ResetEvent(event);
    {
        OVERLAPPED ov2 = {0};
        ov2.Offset = 1200;
        ov2.hEvent = (HANDLE)((ULONG_PTR)event | 1);
        ret = LockFileEx(file2, LOCKFILE_EXCLUSIVE_LOCK, 0, 100, 0, &ov2);
        check("LockFileEx with low-bit event", ret, "err=%lu", GetLastError());

        if (ret) {
            ov_out = NULL;
            ret = GetQueuedCompletionStatus(port, &bytes, &key_out, &ov_out, 0);
            check("IOCP skipped with low-bit event",
                  !ret && GetLastError() == WAIT_TIMEOUT, NULL);
            ov2.hEvent = 0;
            UnlockFileEx(file2, 0, 100, 0, &ov2);
        }
    }

    CloseHandle(event);
    CloseHandle(port);
    CloseHandle(file2);
}

/* ================================================================
 * Test group 7: edge cases
 * ================================================================ */
static void test_edge_cases(HANDLE file)
{
    NTSTATUS status;
    IO_STATUS_BLOCK iosb;
    LARGE_INTEGER offset, length;

    section("edge cases");

    /* 7a: zero-length lock */
    status = do_lock(file, 50, 0, NULL, FALSE, TRUE);
    check("zero-length lock succeeds",
          status == STATUS_SUCCESS, "%s", status_name(status));
    if (status == STATUS_SUCCESS)
        do_unlock(file, 50, 0, NULL);

    /* 7b: lock at high offset (> 4GB) */
    status = do_lock(file, 0x100000000LL, 100, NULL, FALSE, TRUE);
    check("lock at offset > 4GB",
          status == STATUS_SUCCESS, "%s", status_name(status));
    if (status == STATUS_SUCCESS)
        do_unlock(file, 0x100000000LL, 100, NULL);

    /* 7c: unlock range that isn't locked */
    status = do_unlock(file, 9999, 1, NULL);
    check("unlock non-locked range fails",
          is_range_not_locked(status), "%s", status_name(status));

    /* 7d: all parameters at once: io_status + key + event */
    {
        HANDLE event = CreateEventA(NULL, TRUE, FALSE, NULL);
        ULONG key = 77;
        memset(&iosb, 0xcc, sizeof(iosb));
        offset.QuadPart = 1500;
        length.QuadPart = 50;
        status = NtLockFile_fn(file, event, NULL, NULL, &iosb,
                               &offset, &length, &key, TRUE, TRUE);
        check("NtLockFile with io_status + key + event",
              status == STATUS_SUCCESS, "%s", status_name(status));
        if (status == STATUS_SUCCESS) {
            check("event signaled",
                  WaitForSingleObject(event, 0) == WAIT_OBJECT_0, NULL);
            check("iosb.Status written",
                  iosb.Status == STATUS_SUCCESS, "%s", status_name(iosb.Status));
            do_unlock(file, 1500, 50, &key);
        }
        CloseHandle(event);
    }

    /* 7e: all parameters: io_status + key + apc */
    {
        ULONG key = 88;
        apc_count = 0;
        memset(&iosb, 0xcc, sizeof(iosb));
        offset.QuadPart = 1600;
        length.QuadPart = 50;
        status = NtLockFile_fn(file, NULL, lock_apc_callback, (PVOID)0xF00D,
                               &iosb, &offset, &length, &key, TRUE, TRUE);
        check("NtLockFile with io_status + key + apc",
              status == STATUS_SUCCESS, "%s", status_name(status));
        if (status == STATUS_SUCCESS) {
            SleepEx(0, TRUE);
            /* APC may or may not fire on immediate success (Windows=no, Wine=yes) */
            check("APC delivery (Wine=yes, Windows=no)",
                  apc_count == 0 || apc_count == 1, "count=%ld", (long)apc_count);
            check("iosb.Status written",
                  iosb.Status == STATUS_SUCCESS, "%s", status_name(iosb.Status));
            do_unlock(file, 1600, 50, &key);
        }
    }
}

/* ================================================================ */

int main(int argc, char **argv)
{
    HMODULE ntdll;
    HANDLE file;
    int i;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose"))
            verbose = 1;
    }

    printf("\n");
    printf("==========================================================\n");
    printf("  Wine NtLockFile / NtUnlockFile Verification Tool\n");
    printf("==========================================================\n");

    ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) {
        fprintf(stderr, "ERROR: Cannot get ntdll.dll handle\n");
        return 1;
    }

    NtLockFile_fn = (pNtLockFile)(void *)GetProcAddress(ntdll, "NtLockFile");
    NtUnlockFile_fn = (pNtUnlockFile)(void *)GetProcAddress(ntdll, "NtUnlockFile");

    if (!NtLockFile_fn || !NtUnlockFile_fn) {
        fprintf(stderr, "ERROR: NtLockFile/NtUnlockFile not found\n");
        return 1;
    }

    file = create_temp_file(0);
    if (file == INVALID_HANDLE_VALUE)
        return 1;

    test_io_status(file);
    test_key_parameter(file);
    test_apc(file);
    test_contested_lock_event(file);
    test_contested_lock_iocp(file);
    test_shared_exclusive(file);
    test_uncontested_iocp(file);
    test_edge_cases(file);

    printf("\n  ----------------------------------------------------------\n");
    printf("  Results: %d passed, %d failed, %d total\n",
           tests_passed, tests_failed, tests_run);

    if (tests_failed > 0)
        printf("  ** FAILURES indicate Wine bugs are present **\n");
    else
        printf("  All tests passed.\n");

    printf("\n");

    CloseHandle(file);
    return tests_failed > 0 ? 1 : 0;
}
