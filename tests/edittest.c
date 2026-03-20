#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define WM_APPEND (WM_USER + 1)

static HWND g_edit;
static int g_counter = 0;

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_APPEND) {
        char buf[200];
        int len = GetWindowTextLengthA(g_edit);
        SendMessageA(g_edit, EM_SETSEL, len, len);
        sprintf(buf, "Append #%d at pos %d\r\n", g_counter++, len);
        SendMessageA(g_edit, EM_REPLACESEL, FALSE, (LPARAM)buf);
        /* Post another append immediately — queue fills up */
        PostMessageA(hwnd, WM_APPEND, 0, 0);
        return 0;
    }
    if (msg == WM_TIMER) {
        /* Meanwhile, timer replaces ALL text with something shorter */
        char buf[100];
        sprintf(buf, "=== Timer reset %d ===\r\n", g_counter);
        SetWindowTextA(g_edit, buf);
        /* And post more appends */
        PostMessageA(hwnd, WM_APPEND, 0, 0);
        PostMessageA(hwnd, WM_APPEND, 0, 0);
        PostMessageA(hwnd, WM_APPEND, 0, 0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int main(void)
{
    HWND hwnd;
    MSG msg;
    WNDCLASSA wc = {0};
    DWORD start;

    printf("\n  Edit Control PostMessage Interleaving Test\n\n");

    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "EditTest";
    RegisterClassA(&wc);

    hwnd = CreateWindowExA(0, "EditTest", "test", WS_OVERLAPPEDWINDOW,
                           0, 0, 100, 50, NULL, NULL, GetModuleHandleA(NULL), NULL);
    ShowWindow(hwnd, SW_SHOW);

    g_edit = CreateWindowExA(0, "EDIT", "",
                             WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL,
                             0, 0, 80, 30, hwnd, (HMENU)1,
                             GetModuleHandleA(NULL), NULL);

    /* Start timer (fires every 10ms) and initial appends */
    SetTimer(hwnd, 1, 10, NULL);
    PostMessageA(hwnd, WM_APPEND, 0, 0);

    start = GetTickCount();
    while (GetTickCount() - start < 3000) {
        if (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

    KillTimer(hwnd, 1);
    printf("  Done (%d operations). Check stderr.\n\n", g_counter);
    DestroyWindow(hwnd);
    return 0;
}
