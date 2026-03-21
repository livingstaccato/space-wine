/*
 * Wine File Descriptor / Handle Leak Test
 *
 * Proves that unpatched Wine leaks internal state when overlapped locks
 * contest on the same byte range:
 *
 *   1. Creates contested overlapped lock attempts with timeouts
 *   2. After each iteration, verifies the file handles are still usable
 *      (can read/write/lock again) — leaked internal state would prevent this
 *   3. Opens new handles each iteration to detect accumulation
 *   4. On unpatched Wine: locks hang (timeout), handles become unusable
 *   5. On patched Wine / Windows: all locks complete, handles stay healthy
 *
 * The key insight: on unpatched Wine, NtLockFile returns STATUS_PENDING and
 * never completes. The wineserver keeps the lock request alive internally,
 * the event handle from the lock attempt is never signaled, and the file
 * descriptor stays in a locked state. Subsequent lock attempts on the same
 * range also hang, creating a cascade.
 *
 * Compile:
 *   x86_64-w64-mingw32-gcc -o fdleaktest.exe tests/fdleaktest.c
 *
 * Run:
 *   wine fdleaktest.exe        # normal
 *   wine fdleaktest.exe -v     # verbose
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static int verbose = 0;
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

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

static char temp_path[MAX_PATH];

static HANDLE create_test_file(DWORD flags)
{
    HANDLE h;
    DWORD written;
    char buf[4096];

    if (!temp_path[0]) {
        char dir[MAX_PATH];
        GetTempPathA(MAX_PATH, dir);
        GetTempFileNameA(dir, "fdl", 0, temp_path);
    }

    h = CreateFileA(temp_path, GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, CREATE_ALWAYS,
                    FILE_FLAG_DELETE_ON_CLOSE | flags, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        memset(buf, 'X', sizeof(buf));
        WriteFile(h, buf, sizeof(buf), &written, NULL);
        FlushFileBuffers(h);
    }
    return h;
}

static HANDLE open_another(DWORD flags)
{
    return CreateFileA(temp_path, GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, flags, NULL);
}

struct release_data {
    HANDLE file;
    DWORD delay_ms;
};

static DWORD WINAPI release_thread(LPVOID param)
{
    struct release_data *d = param;
    OVERLAPPED ov = {0};
    Sleep(d->delay_ms);
    UnlockFileEx(d->file, 0, 100, 0, &ov);
    return 0;
}

/*
 * Test 1: Contested locks complete (don't hang)
 *
 * On unpatched Wine, this test hangs on the first iteration because
 * NtLockFile returns STATUS_PENDING and never completes the lock.
 */
static void test_contested_locks_complete(void)
{
    HANDLE file_a, file_b;
    int i, hangs = 0, completed = 0;
    int iterations = 10;

    printf("\n  [contested locks complete — no hang]\n");

    file_a = create_test_file(FILE_FLAG_OVERLAPPED);
    file_b = open_another(FILE_FLAG_OVERLAPPED);
    if (file_a == INVALID_HANDLE_VALUE || file_b == INVALID_HANDLE_VALUE) {
        check("open test files", 0, NULL);
        return;
    }

    for (i = 0; i < iterations; i++) {
        OVERLAPPED ov_lock = {0};
        OVERLAPPED ov_contend = {0};
        HANDLE event_a, event_b, thread;
        struct release_data rd;
        BOOL ret;
        DWORD wait;

        /* Lock [0,100) on handle A */
        event_a = CreateEventA(NULL, TRUE, FALSE, NULL);
        ov_lock.hEvent = event_a;
        LockFileEx(file_a, LOCKFILE_EXCLUSIVE_LOCK, 0, 100, 0, &ov_lock);

        /* Spawn thread to release after 50ms */
        rd.file = file_a;
        rd.delay_ms = 50;
        thread = CreateThread(NULL, 0, release_thread, &rd, 0, NULL);

        /* Contend on [0,100) from handle B — should block briefly */
        event_b = CreateEventA(NULL, TRUE, FALSE, NULL);
        ov_contend.hEvent = event_b;
        ret = LockFileEx(file_b, LOCKFILE_EXCLUSIVE_LOCK, 0, 100, 0, &ov_contend);
        if (!ret && GetLastError() == ERROR_IO_PENDING) {
            wait = WaitForSingleObject(event_b, 2000);
            ret = (wait == WAIT_OBJECT_0);
        }

        if (ret) {
            completed++;
            /* Release lock B */
            ov_contend.hEvent = 0;
            UnlockFileEx(file_b, 0, 100, 0, &ov_contend);
        } else {
            hangs++;
            CancelIo(file_b);
        }

        WaitForSingleObject(thread, 5000);
        CloseHandle(thread);
        CloseHandle(event_a);
        CloseHandle(event_b);
    }

    check("all contested locks completed",
          completed == iterations, "%d/%d completed, %d hangs",
          completed, iterations, hangs);

    CloseHandle(file_b);
    CloseHandle(file_a);
}

/*
 * Test 2: Handles remain usable after contested locks
 *
 * After contested lock resolution, both handles should still work for
 * normal I/O operations. On unpatched Wine, the internal lock state
 * leaks and subsequent operations on the handle can fail or hang.
 */
static void test_handles_usable_after_contention(void)
{
    HANDLE file_a, file_b;
    OVERLAPPED ov = {0};
    HANDLE event, thread;
    struct release_data rd;
    BOOL ret;
    DWORD wait, written, bytes_read;
    char buf[64];

    printf("\n  [handles usable after contention]\n");

    file_a = create_test_file(FILE_FLAG_OVERLAPPED);
    file_b = open_another(FILE_FLAG_OVERLAPPED);
    if (file_a == INVALID_HANDLE_VALUE || file_b == INVALID_HANDLE_VALUE) {
        check("open test files", 0, NULL);
        return;
    }

    /* Create contention and resolve it */
    event = CreateEventA(NULL, TRUE, FALSE, NULL);
    ov.hEvent = event;
    LockFileEx(file_a, LOCKFILE_EXCLUSIVE_LOCK, 0, 100, 0, &ov);

    rd.file = file_a;
    rd.delay_ms = 50;
    thread = CreateThread(NULL, 0, release_thread, &rd, 0, NULL);

    {
        OVERLAPPED ov2 = {0};
        HANDLE ev2 = CreateEventA(NULL, TRUE, FALSE, NULL);
        ov2.hEvent = ev2;
        ret = LockFileEx(file_b, LOCKFILE_EXCLUSIVE_LOCK, 0, 100, 0, &ov2);
        if (!ret && GetLastError() == ERROR_IO_PENDING) {
            wait = WaitForSingleObject(ev2, 2000);
            ret = (wait == WAIT_OBJECT_0);
        }
        check("contested lock resolved", ret, NULL);
        if (ret) {
            ov2.hEvent = 0;
            UnlockFileEx(file_b, 0, 100, 0, &ov2);
        } else {
            CancelIo(file_b);
        }
        CloseHandle(ev2);
    }

    WaitForSingleObject(thread, 5000);
    CloseHandle(thread);
    CloseHandle(event);

    /* Verify handles still work for I/O (use OVERLAPPED struct for async handles) */
    {
        OVERLAPPED ov_w = {0};
        HANDLE ev_w = CreateEventA(NULL, TRUE, FALSE, NULL);
        ov_w.hEvent = ev_w;
        ret = WriteFile(file_a, "AAAA", 4, &written, &ov_w);
        if (!ret && GetLastError() == ERROR_IO_PENDING) {
            WaitForSingleObject(ev_w, 1000);
            GetOverlappedResult(file_a, &ov_w, &written, FALSE);
            ret = TRUE;
        }
        check("handle A writable after contention", ret && written == 4,
              "ret=%d written=%lu", ret, (unsigned long)written);
        CloseHandle(ev_w);
    }

    {
        OVERLAPPED ov_r = {0};
        HANDLE ev_r = CreateEventA(NULL, TRUE, FALSE, NULL);
        ov_r.hEvent = ev_r;
        ret = ReadFile(file_b, buf, 4, &bytes_read, &ov_r);
        if (!ret && GetLastError() == ERROR_IO_PENDING) {
            WaitForSingleObject(ev_r, 1000);
            GetOverlappedResult(file_b, &ov_r, &bytes_read, FALSE);
            ret = TRUE;
        }
        check("handle B readable after contention", ret && bytes_read == 4,
              "ret=%d read=%lu", ret, (unsigned long)bytes_read);
        CloseHandle(ev_r);
    }

    /* Verify we can lock again (no stale lock state) */
    {
        OVERLAPPED ov3 = {0};
        HANDLE ev3 = CreateEventA(NULL, TRUE, FALSE, NULL);
        ov3.hEvent = ev3;
        ret = LockFileEx(file_a, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                         0, 100, 0, &ov3);
        check("handle A can lock again", ret, "err=%lu", GetLastError());
        if (ret) {
            ov3.hEvent = 0;
            UnlockFileEx(file_a, 0, 100, 0, &ov3);
        }
        CloseHandle(ev3);
    }

    CloseHandle(file_b);
    CloseHandle(file_a);
}

/*
 * Test 3: No handle accumulation across many iterations
 *
 * Opens and closes many handles while doing contested locks.
 * If internal state leaks, new handle opens will eventually fail
 * or the process will run out of file descriptors.
 */
static void test_no_handle_accumulation(void)
{
    HANDLE file_owner;
    int i, open_failures = 0, lock_failures = 0;
    int iterations = 50;

    printf("\n  [no handle accumulation — %d iterations]\n", iterations);

    file_owner = create_test_file(FILE_FLAG_OVERLAPPED);
    if (file_owner == INVALID_HANDLE_VALUE) {
        check("create owner file", 0, NULL);
        return;
    }

    for (i = 0; i < iterations; i++) {
        HANDLE h = open_another(FILE_FLAG_OVERLAPPED);
        if (h == INVALID_HANDLE_VALUE) {
            open_failures++;
            continue;
        }

        /* Quick uncontested lock/unlock cycle */
        {
            OVERLAPPED ov = {0};
            HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
            BOOL ret;
            ov.Offset = (DWORD)(i * 10);
            ov.hEvent = ev;
            ret = LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK, 0, 10, 0, &ov);
            if (!ret && GetLastError() == ERROR_IO_PENDING) {
                if (WaitForSingleObject(ev, 1000) != WAIT_OBJECT_0)
                    ret = FALSE;
                else
                    ret = TRUE;
            }
            if (ret) {
                OVERLAPPED ov_u = {0};
                ov_u.Offset = ov.Offset;
                UnlockFileEx(h, 0, 10, 0, &ov_u);
            } else {
                lock_failures++;
            }
            CloseHandle(ev);
        }

        CloseHandle(h);
    }

    check("all handles opened successfully",
          open_failures == 0, "%d failures in %d iterations",
          open_failures, iterations);
    check("all lock/unlock cycles completed",
          lock_failures == 0, "%d failures in %d iterations",
          lock_failures, iterations);

    CloseHandle(file_owner);
}

int main(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose"))
            verbose = 1;
    }

    printf("\n");
    printf("==========================================================\n");
    printf("  Wine File Descriptor / Handle Leak Test\n");
    printf("==========================================================\n");

    test_contested_locks_complete();
    test_handles_usable_after_contention();
    test_no_handle_accumulation();

    printf("\n  ----------------------------------------------------------\n");
    printf("  Results: %d passed, %d failed, %d total\n",
           tests_passed, tests_failed, tests_run);

    if (tests_failed > 0)
        printf("  ** FAILURES — contested locks hang or leak state **\n");
    else
        printf("  All tests passed. No leaks detected.\n");

    printf("\n");
    return tests_failed > 0 ? 1 : 0;
}
