/*
 * lockstress.exe — Multi-threaded file lock stress test
 *
 * Simulates a game server (like TWGS) doing concurrent overlapped file I/O
 * with byte-range locks. Multiple threads contend on the same file regions.
 *
 * On unpatched Wine: hangs within seconds (contested overlapped locks never complete)
 * On patched Wine: completes all iterations cleanly
 *
 * Compile: x86_64-w64-mingw32-gcc -o lockstress.exe lockstress.c
 */

#include <stdio.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define NUM_THREADS    4
#define NUM_RECORDS    20
#define RECORD_SIZE    128
#define ITERATIONS     50
#define LOCK_TIMEOUT   5000  /* ms per lock attempt before declaring hang */

static char file_path[MAX_PATH];
static volatile LONG completed = 0;
static volatile LONG hangs = 0;
static volatile LONG errors = 0;

static DWORD WINAPI worker_thread( LPVOID param )
{
    int id = (int)(ULONG_PTR)param;
    HANDLE file;
    int i;

    file = CreateFileA( file_path, GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL );
    if (file == INVALID_HANDLE_VALUE)
    {
        printf( "  thread %d: CreateFile failed err=%lu\n", id, GetLastError() );
        InterlockedIncrement( &errors );
        return 1;
    }

    for (i = 0; i < ITERATIONS; i++)
    {
        /* Each thread locks a "record" — threads overlap on the same records
         * to create contention, just like a real game server */
        int record = (id + i) % NUM_RECORDS;
        DWORD offset = record * RECORD_SIZE;
        OVERLAPPED ov = {0};
        HANDLE event;
        DWORD wait_result;
        BOOL ret;
        char buf[RECORD_SIZE];
        DWORD bytes;

        event = CreateEventA( NULL, TRUE, FALSE, NULL );
        ov.Offset = offset;
        ov.hEvent = event;

        /* Acquire exclusive lock on this record — may contend with other threads */
        ret = LockFileEx( file, LOCKFILE_EXCLUSIVE_LOCK, 0, RECORD_SIZE, 0, &ov );
        if (!ret)
        {
            if (GetLastError() == ERROR_IO_PENDING)
            {
                /* Contested — wait for lock with timeout */
                wait_result = WaitForSingleObject( event, LOCK_TIMEOUT );
                if (wait_result == WAIT_TIMEOUT)
                {
                    /* HANG: This is the Wine bug — overlapped lock never completes */
                    InterlockedIncrement( &hangs );
                    CloseHandle( event );
                    break;
                }
                else if (wait_result != WAIT_OBJECT_0)
                {
                    InterlockedIncrement( &errors );
                    CloseHandle( event );
                    break;
                }
                /* Lock acquired after contention */
            }
            else
            {
                InterlockedIncrement( &errors );
                CloseHandle( event );
                break;
            }
        }

        /* Simulate read-modify-write while holding the lock */
        SetFilePointer( file, offset, NULL, FILE_BEGIN );
        ReadFile( file, buf, RECORD_SIZE, &bytes, NULL );
        buf[0]++;  /* modify */
        SetFilePointer( file, offset, NULL, FILE_BEGIN );
        WriteFile( file, buf, RECORD_SIZE, &bytes, NULL );

        /* Release lock */
        ov.hEvent = 0;
        UnlockFileEx( file, 0, RECORD_SIZE, 0, &ov );
        CloseHandle( event );

        InterlockedIncrement( &completed );
    }

    CloseHandle( file );
    return 0;
}

int main( void )
{
    HANDLE file, threads[NUM_THREADS];
    DWORD written, i;
    char buf[RECORD_SIZE * NUM_RECORDS];
    DWORD start, elapsed;

    printf( "\n" );
    printf( "============================================================\n" );
    printf( "  lockstress — Multi-threaded Overlapped File Lock Stress\n" );
    printf( "============================================================\n" );
    printf( "\n" );
    printf( "  %d threads, %d records, %d iterations each\n", NUM_THREADS, NUM_RECORDS, ITERATIONS );
    printf( "  Total lock operations: %d\n", NUM_THREADS * ITERATIONS );
    printf( "  Timeout per lock: %d ms\n\n", LOCK_TIMEOUT );

    /* Create temp file with initial data */
    {
        char path[MAX_PATH];
        GetTempPathA( MAX_PATH, path );
        GetTempFileNameA( path, "lks", 0, file_path );
    }

    file = CreateFileA( file_path, GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL, CREATE_ALWAYS, 0, NULL );
    if (file == INVALID_HANDLE_VALUE)
    {
        printf( "  ERROR: cannot create temp file\n" );
        return 1;
    }

    memset( buf, 0, sizeof(buf) );
    WriteFile( file, buf, sizeof(buf), &written, NULL );
    FlushFileBuffers( file );
    CloseHandle( file );  /* threads open their own handles */

    /* Launch worker threads */
    start = GetTickCount();
    printf( "  Starting %d threads...\n", NUM_THREADS );

    for (i = 0; i < NUM_THREADS; i++)
        threads[i] = CreateThread( NULL, 0, worker_thread, (LPVOID)(ULONG_PTR)i, 0, NULL );

    /* Wait for all threads (with overall timeout) */
    WaitForMultipleObjects( NUM_THREADS, threads, TRUE, NUM_THREADS * ITERATIONS * LOCK_TIMEOUT );
    elapsed = GetTickCount() - start;

    for (i = 0; i < NUM_THREADS; i++)
        CloseHandle( threads[i] );

    DeleteFileA( file_path );

    /* Results */
    printf( "\n  ----------------------------------------------------------\n" );
    printf( "  Completed: %ld / %d lock operations\n", completed, NUM_THREADS * ITERATIONS );
    printf( "  Hangs:     %ld (contested lock never completed)\n", hangs );
    printf( "  Errors:    %ld\n", errors );
    printf( "  Time:      %lu ms\n", elapsed );
    printf( "\n" );

    if (hangs > 0)
    {
        printf( "  ** HANG DETECTED — Wine overlapped lock bug is present **\n" );
        printf( "  Contested LockFileEx returned ERROR_IO_PENDING but the\n" );
        printf( "  lock was never granted. This causes game servers and other\n" );
        printf( "  multi-threaded applications to freeze.\n" );
    }
    else if (errors > 0)
    {
        printf( "  ** ERRORS — unexpected failures during lock operations **\n" );
    }
    else
    {
        printf( "  All lock operations completed successfully.\n" );
    }

    printf( "\n" );
    return (hangs > 0 || errors > 0) ? 1 : 0;
}
