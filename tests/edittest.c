/*
 * edittest.exe — Multi-line edit control stress test
 *
 * Rapidly appends text to a multi-line edit control to reproduce the
 * "modification occurred outside buffer" FIXME in Wine's EDIT_BuildLineDefs_ML.
 *
 * On unpatched Wine: floods stderr with FIXME messages
 * On patched Wine: clean (WARN only with +edit trace enabled)
 *
 * Compile: x86_64-w64-mingw32-gcc -o edittest.exe edittest.c -lgdi32 -luser32
 */

#include <stdio.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define NUM_APPENDS  500
#define LINE_TEXT    "Log entry: TWGS game server status update - sector scan complete\r\n"

int main(void)
{
    HWND hwnd, edit;
    MSG msg;
    DWORD start, elapsed;
    int i;

    printf("\n");
    printf("============================================================\n");
    printf("  Multi-line Edit Control Stress Test\n");
    printf("============================================================\n\n");
    printf("  Appending %d lines to a multi-line edit control.\n", NUM_APPENDS);
    printf("  On unpatched Wine, this produces FIXME spam on stderr.\n");
    printf("  On patched Wine, no FIXMEs.\n\n");

    /* Create a hidden window with a multi-line edit control */
    hwnd = CreateWindowExA(0, "STATIC", "edittest",
                           WS_OVERLAPPEDWINDOW,
                           0, 0, 400, 300,
                           NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!hwnd) {
        printf("  ERROR: CreateWindow failed (%lu)\n", GetLastError());
        return 1;
    }

    edit = CreateWindowExA(0, "EDIT", "",
                           WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL |
                           ES_READONLY | WS_VSCROLL,
                           0, 0, 380, 280,
                           hwnd, (HMENU)1, GetModuleHandleA(NULL), NULL);
    if (!edit) {
        printf("  ERROR: CreateWindow EDIT failed (%lu)\n", GetLastError());
        DestroyWindow(hwnd);
        return 1;
    }

    /* Rapidly append text — this is what TWGS does to its log window */
    start = GetTickCount();

    for (i = 0; i < NUM_APPENDS; i++) {
        int len = GetWindowTextLengthA(edit);
        SendMessageA(edit, EM_SETSEL, len, len);
        SendMessageA(edit, EM_REPLACESEL, FALSE, (LPARAM)LINE_TEXT);

        /* Process messages periodically to let the control update */
        if (i % 50 == 0) {
            while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
                DispatchMessageA(&msg);
        }
    }

    elapsed = GetTickCount() - start;

    printf("  Completed: %d appends in %lu ms\n", NUM_APPENDS, elapsed);
    printf("  Final text length: %d chars\n", GetWindowTextLengthA(edit));
    printf("\n  Check stderr for FIXME messages.\n");
    printf("  'modification occurred outside buffer' = BUG (unpatched)\n");
    printf("  Clean stderr = PASS (patched)\n\n");

    DestroyWindow(hwnd);
    return 0;
}
