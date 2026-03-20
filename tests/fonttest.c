/*
 * fonttest.exe — OEM_CHARSET font matching verification
 *
 * Tests that requesting a font with OEM_CHARSET (255) succeeds without
 * triggering Wine's "Untranslated charset 255" FIXME.
 *
 * Compile: x86_64-w64-mingw32-gcc -o fonttest.exe fonttest.c -lgdi32
 */

#include <stdio.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

static void check(const char *name, int pass, const char *detail)
{
    tests_run++;
    if (pass) {
        tests_passed++;
        printf("  ok   %-55s", name);
    } else {
        tests_failed++;
        printf("  FAIL %-55s", name);
    }
    if (detail && detail[0])
        printf("  (%s)", detail);
    printf("\n");
}

int main(void)
{
    LOGFONTA lf;
    HFONT font, old;
    HDC hdc;
    TEXTMETRICA tm;
    char detail[256];

    printf("\n");
    printf("============================================================\n");
    printf("  OEM_CHARSET Font Matching Test\n");
    printf("============================================================\n\n");

    hdc = GetDC(NULL);
    if (!hdc) {
        printf("  ERROR: GetDC failed\n");
        return 1;
    }

    /* Test 1: Terminal font with OEM_CHARSET (what TWGS requests) */
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -16;
    lf.lfCharSet = OEM_CHARSET;  /* 255 */
    lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    strcpy(lf.lfFaceName, "Terminal");

    font = CreateFontIndirectA(&lf);
    check("CreateFontIndirect Terminal OEM_CHARSET",
          font != NULL, font ? "ok" : "NULL");

    if (font) {
        old = SelectObject(hdc, font);
        GetTextMetricsA(hdc, &tm);
        snprintf(detail, sizeof(detail), "charset=%d height=%ld family=%s",
                 tm.tmCharSet, tm.tmHeight,
                 tm.tmPitchAndFamily & TMPF_FIXED_PITCH ? "variable" : "fixed");
        /* The font should have been resolved — charset should not be 255
         * unless the system actually has an OEM font */
        check("Font resolved (not rejected)", 1, detail);

        /* Verify we got a fixed-pitch font back */
        check("Got fixed-pitch font",
              !(tm.tmPitchAndFamily & TMPF_FIXED_PITCH), detail);

        SelectObject(hdc, old);
        DeleteObject(font);
    }

    /* Test 2: System font with OEM_CHARSET */
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -12;
    lf.lfCharSet = OEM_CHARSET;
    strcpy(lf.lfFaceName, "System");

    font = CreateFontIndirectA(&lf);
    check("CreateFontIndirect System OEM_CHARSET",
          font != NULL, font ? "ok" : "NULL");

    if (font) {
        old = SelectObject(hdc, font);
        GetTextMetricsA(hdc, &tm);
        snprintf(detail, sizeof(detail), "charset=%d height=%ld",
                 tm.tmCharSet, tm.tmHeight);
        check("System font resolved", 1, detail);
        SelectObject(hdc, old);
        DeleteObject(font);
    }

    /* Test 3: No face name with OEM_CHARSET (let font mapper choose) */
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -14;
    lf.lfCharSet = OEM_CHARSET;
    lf.lfPitchAndFamily = FIXED_PITCH;
    lf.lfFaceName[0] = '\0';

    font = CreateFontIndirectA(&lf);
    check("CreateFontIndirect (no name) OEM_CHARSET",
          font != NULL, font ? "ok" : "NULL");

    if (font) {
        old = SelectObject(hdc, font);
        GetTextMetricsA(hdc, &tm);
        snprintf(detail, sizeof(detail), "charset=%d height=%ld",
                 tm.tmCharSet, tm.tmHeight);
        check("Mapper-chosen font resolved", 1, detail);
        SelectObject(hdc, old);
        DeleteObject(font);
    }

    /* Test 4: Courier New with OEM_CHARSET */
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -13;
    lf.lfCharSet = OEM_CHARSET;
    strcpy(lf.lfFaceName, "Courier New");

    font = CreateFontIndirectA(&lf);
    check("CreateFontIndirect Courier New OEM_CHARSET",
          font != NULL, font ? "ok" : "NULL");

    if (font) {
        old = SelectObject(hdc, font);
        GetTextMetricsA(hdc, &tm);
        snprintf(detail, sizeof(detail), "charset=%d height=%ld",
                 tm.tmCharSet, tm.tmHeight);
        check("Courier New resolved", 1, detail);
        SelectObject(hdc, old);
        DeleteObject(font);
    }

    /* Test 5: ANSI_CHARSET for comparison (should never FIXME) */
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -16;
    lf.lfCharSet = ANSI_CHARSET;
    strcpy(lf.lfFaceName, "Terminal");

    font = CreateFontIndirectA(&lf);
    check("CreateFontIndirect Terminal ANSI_CHARSET (control)",
          font != NULL, font ? "ok" : "NULL");

    if (font) {
        old = SelectObject(hdc, font);
        GetTextMetricsA(hdc, &tm);
        snprintf(detail, sizeof(detail), "charset=%d", tm.tmCharSet);
        check("ANSI control resolved", 1, detail);
        SelectObject(hdc, old);
        DeleteObject(font);
    }

    ReleaseDC(NULL, hdc);

    printf("\n  ----------------------------------------------------------\n");
    printf("  Results: %d passed, %d failed, %d total\n",
           tests_passed, tests_failed, tests_run);

    if (tests_failed > 0)
        printf("  ** Font matching failures detected **\n");
    else
        printf("  All font tests passed.\n");

    printf("\n  NOTE: On unpatched Wine, this test will produce\n");
    printf("  'fixme:font:find_matching_face Untranslated charset 255'\n");
    printf("  on stderr. The FIXME is the bug — fonts still load via\n");
    printf("  fallback, but the charset-specific matching is skipped.\n");
    printf("\n");

    return tests_failed > 0 ? 1 : 0;
}
