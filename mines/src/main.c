/*
 * main.c - Win32 front end for the Minesweeper engine.
 *
 * Plain C11 against the Win32 API: one window, GDI drawing into a back
 * buffer, no resources and no runtime dependencies. All game rules live in
 * core.c and all inference in solver.c; this file only draws and dispatches.
 */
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "core.h"
#include "solver.h"

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")
#endif

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

/* ------------------------------------------------------------- palette -- */

#define C_BG       RGB(0x0d, 0x11, 0x17)
#define C_HIDDEN   RGB(0x2a, 0x31, 0x3c)
#define C_OPEN     RGB(0x16, 0x1b, 0x22)
#define C_TEXT     RGB(0xc9, 0xd1, 0xd9)
#define C_DIM      RGB(0x6e, 0x76, 0x81)
#define C_GREEN    RGB(0x3f, 0xb9, 0x50)
#define C_RED      RGB(0xf8, 0x51, 0x49)
#define C_AMBER    RGB(0xf0, 0x88, 0x3e)
#define C_BLUE     RGB(0x58, 0xa6, 0xff)
#define C_BOOM     RGB(0xda, 0x36, 0x33)

static const COLORREF NUMBER_COLOR[9] = {
    0,
    RGB(0x58, 0xa6, 0xff), RGB(0x3f, 0xb9, 0x50), RGB(0xf8, 0x51, 0x49),
    RGB(0xbc, 0x8c, 0xff), RGB(0xd2, 0x99, 0x22), RGB(0x39, 0xc5, 0xcf),
    RGB(0xc9, 0xd1, 0xd9), RGB(0x8b, 0x94, 0x9e)
};

/* --------------------------------------------------------------- state -- */

typedef struct { const wchar_t *name; int w, h, mines; } level;
static const level LEVELS[3] = {
    { L"beginner",      9,  9, 10 },
    { L"intermediate", 16, 16, 40 },
    { L"expert",       30, 16, 99 },
};

enum {
    IDM_NEW = 100, IDM_RETRY, IDM_LEVEL0, IDM_LEVEL1, IDM_LEVEL2,
    IDM_COPY_SEED, IDM_PASTE_SEED, IDM_EXIT,
    IDM_HINT, IDM_PROB, IDM_STEP, IDM_HELP
};

static struct {
    HWND     hwnd;
    HMENU    menu;
    ms_game  game;
    int      level;

    ms_analysis     an;          /* valid only when an_ok */
    int             an_ok;
    int             show_prob;
    int             hint;        /* highlighted cell or -1 */
    int             cur_x, cur_y, cur_on;   /* keyboard cursor */

    ULONGLONG t_start, t_end;
    wchar_t   msg[128];
    COLORREF  msg_color;

    /* layout, in device pixels */
    UINT  dpi;
    int   cell, margin, head_h, gutter, foot_h;
    int   board_x, board_y, client_w, client_h;
    HFONT f_cell, f_small, f_ui;
} A;

static int S(int v) { return MulDiv(v, (int)A.dpi, 96); }

static void set_msg(COLORREF color, const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(A.msg, ARRAYSIZE(A.msg) - 1, fmt, ap);
    va_end(ap);
    A.msg[ARRAYSIZE(A.msg) - 1] = 0;
    A.msg_color = color;
}

static int playing(void)
{
    return A.game.state == MS_READY || A.game.state == MS_RUNNING;
}

static uint32_t fresh_seed(void)
{
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    uint64_t z = (uint64_t)c.QuadPart ^ (GetTickCount64() << 21) ^
                 ((uint64_t)GetCurrentProcessId() * 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return (uint32_t)(z ^ (z >> 32));
}

/* -------------------------------------------------------------- layout -- */

static HFONT make_font(int px, int weight)
{
    return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       FIXED_PITCH | FF_MODERN, L"Consolas");
}

static void layout(void)
{
    if (A.f_cell)  DeleteObject(A.f_cell);
    if (A.f_small) DeleteObject(A.f_small);
    if (A.f_ui)    DeleteObject(A.f_ui);

    A.cell   = S(30);
    A.margin = S(14);
    A.head_h = S(28);
    A.gutter = S(22);
    A.foot_h = S(26);
    A.f_cell  = make_font(A.cell * 6 / 10, FW_BOLD);
    A.f_small = make_font(A.cell * 4 / 10, FW_NORMAL);
    A.f_ui    = make_font(S(14), FW_NORMAL);

    /* The status lines need room for about 62 monospace characters. */
    HDC dc = GetDC(A.hwnd);
    HFONT old = SelectObject(dc, A.f_ui);
    SIZE sz;
    GetTextExtentPoint32W(dc, L"0", 1, &sz);
    SelectObject(dc, old);
    ReleaseDC(A.hwnd, dc);

    int board_w = A.game.w * A.cell, board_h = A.game.h * A.cell;
    int inner = A.gutter + board_w;
    if (inner < sz.cx * 62) inner = sz.cx * 62;

    A.client_w = inner + 2 * A.margin;
    A.board_x  = A.margin + (inner - A.gutter - board_w) / 2 + A.gutter;
    A.board_y  = A.margin + A.head_h + A.gutter;
    A.client_h = A.board_y + board_h + S(10) + A.foot_h + A.margin;

    RECT r = { 0, 0, A.client_w, A.client_h };
    AdjustWindowRectExForDpi(&r, (DWORD)GetWindowLongPtrW(A.hwnd, GWL_STYLE), TRUE,
                             (DWORD)GetWindowLongPtrW(A.hwnd, GWL_EXSTYLE), A.dpi);
    SetWindowPos(A.hwnd, NULL, 0, 0, r.right - r.left, r.bottom - r.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(A.hwnd, NULL, FALSE);
}

static int cell_at(int px, int py, int *x, int *y)
{
    if (px < A.board_x || py < A.board_y) return 0;
    *x = (px - A.board_x) / A.cell;
    *y = (py - A.board_y) / A.cell;
    return *x < A.game.w && *y < A.game.h;
}

/* ---------------------------------------------------------------- game -- */

static void refresh_analysis(void)
{
    A.an_ok = 0;
    if (!playing()) return;
    ms_solve_status st = ms_analyze(&A.game, &A.an);
    if (st == MS_SOLVE_OK) A.an_ok = 1;
    else if (st == MS_SOLVE_BUDGET)
        set_msg(C_AMBER, L"solver: search budget exceeded on this position.");
    else
        set_msg(C_AMBER, L"solver: unavailable (status %d).", (int)st);
}

static void update_menu(void)
{
    CheckMenuRadioItem(A.menu, IDM_LEVEL0, IDM_LEVEL2, (UINT)(IDM_LEVEL0 + A.level), MF_BYCOMMAND);
    CheckMenuItem(A.menu, IDM_PROB, MF_BYCOMMAND | (A.show_prob ? MF_CHECKED : MF_UNCHECKED));
}

static void new_game(int lv, uint32_t seed)
{
    int resized = lv != A.level || A.game.w == 0;
    A.level = lv;
    ms_init(&A.game, LEVELS[lv].w, LEVELS[lv].h, LEVELS[lv].mines, seed);
    A.t_start = A.t_end = 0;
    A.hint = -1;
    A.an_ok = 0;
    if (A.cur_x >= A.game.w) A.cur_x = A.game.w - 1;
    if (A.cur_y >= A.game.h) A.cur_y = A.game.h - 1;
    set_msg(C_DIM, L"ready. the first probe is always safe.");
    if (A.show_prob) refresh_analysis();
    update_menu();
    if (resized) layout();
    InvalidateRect(A.hwnd, NULL, FALSE);
}

/* Bookkeeping after any action that may have changed the board. */
static void after_move(void)
{
    ms_game *g = &A.game;
    A.hint = -1;
    A.an_ok = 0;
    if (g->state == MS_RUNNING && !A.t_start) A.t_start = GetTickCount64();
    if (!playing() && !A.t_end) {
        A.t_end = GetTickCount64();
        if (!A.t_start) A.t_start = A.t_end;
        double secs = (double)(A.t_end - A.t_start) / 1000.0;
        if (g->state == MS_WON)
            set_msg(C_GREEN, L"exit 0: cleared in %.1f s, %d clicks (3BV %d).",
                    secs, g->clicks, g->bbbv);
        else
            set_msg(C_RED, L"segfault: mine at (0x%X, 0x%X). [N]ew  [R]etry",
                    g->boom % g->w, g->boom / g->w);
    } else if (g->state == MS_RUNNING) {
        set_msg(C_DIM, L"%d of %d safe cells open.", g->opened, g->w * g->h - g->mines);
    }
    if (A.show_prob) refresh_analysis();
    InvalidateRect(A.hwnd, NULL, FALSE);
}

static void act_open(int x, int y)
{
    if (!playing()) return;
    if (A.game.cell[y * A.game.w + x] & MS_OPEN) ms_chord(&A.game, x, y);
    else ms_reveal(&A.game, x, y);
    after_move();
}

static void act_flag(int x, int y)
{
    if (ms_toggle_flag(&A.game, x, y)) InvalidateRect(A.hwnd, NULL, FALSE);
}

static void act_hint(void)
{
    if (!playing()) return;
    if (A.game.state == MS_READY) {
        set_msg(C_GREEN, L"hint: the first probe is always safe. open anywhere.");
        InvalidateRect(A.hwnd, NULL, FALSE);
        return;
    }
    refresh_analysis();
    if (A.an_ok && A.an.best >= 0) {
        int b = A.an.best, x = b % A.game.w, y = b / A.game.w;
        A.hint = b;
        if (A.an.verdict[b] == MS_SAFE)
            set_msg(C_GREEN, L"hint: (0x%X, 0x%X) is provably safe.", x, y);
        else
            set_msg(C_AMBER, L"hint: no safe cell. best guess (0x%X, 0x%X), p=%.3f",
                    x, y, A.an.p[b]);
    }
    InvalidateRect(A.hwnd, NULL, FALSE);
}

/* Apply every deduction that is certain: open safe cells, flag sure mines. */
static void act_step(void)
{
    ms_game *g = &A.game;
    if (!playing()) return;
    refresh_analysis();
    if (!A.an_ok) { InvalidateRect(A.hwnd, NULL, FALSE); return; }

    ms_analysis an = A.an;   /* the board changes under us while we apply it */
    int opened = 0, flagged = 0, total = g->w * g->h;
    for (int i = 0; i < total && playing(); i++) {
        int x = i % g->w, y = i / g->w;
        if (g->cell[i] & MS_OPEN) continue;
        if (an.verdict[i] == MS_SAFE) {
            if (g->cell[i] & MS_FLAG) ms_toggle_flag(g, x, y);
            if (ms_reveal(g, x, y) > 0) opened++;
        } else if (an.verdict[i] == MS_SURE_MINE && !(g->cell[i] & MS_FLAG)) {
            ms_toggle_flag(g, x, y);
            flagged++;
        }
    }
    after_move();
    if (!playing()) return;
    if (opened || flagged) {
        set_msg(C_GREEN, L"step: opened %d, flagged %d.", opened, flagged);
    } else if (an.best >= 0) {
        A.hint = an.best;
        set_msg(C_AMBER, L"step: nothing forced. best guess (0x%X, 0x%X), p=%.3f",
                an.best % g->w, an.best / g->w, an.p[an.best]);
    }
    InvalidateRect(A.hwnd, NULL, FALSE);
}

static void act_toggle_prob(void)
{
    A.show_prob = !A.show_prob;
    if (A.show_prob) refresh_analysis();
    update_menu();
    InvalidateRect(A.hwnd, NULL, FALSE);
}

static void act_copy_seed(void)
{
    wchar_t text[16];
    _snwprintf(text, ARRAYSIZE(text), L"0x%08X", A.game.seed);
    text[ARRAYSIZE(text) - 1] = 0;
    size_t bytes = (wcslen(text) + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) return;
    memcpy(GlobalLock(mem), text, bytes);
    GlobalUnlock(mem);
    if (OpenClipboard(A.hwnd)) {
        EmptyClipboard();
        if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
        CloseClipboard();
        set_msg(C_DIM, L"seed %s copied to the clipboard.", text);
    } else {
        GlobalFree(mem);
    }
    InvalidateRect(A.hwnd, NULL, FALSE);
}

static void act_paste_seed(void)
{
    int ok = 0;
    uint32_t seed = 0;
    if (OpenClipboard(A.hwnd)) {
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        const wchar_t *text = h ? GlobalLock(h) : NULL;
        if (text) {
            wchar_t *end;
            unsigned long v = wcstoul(text, &end, 0);
            if (end != text) { seed = (uint32_t)v; ok = 1; }
            GlobalUnlock(h);
        }
        CloseClipboard();
    }
    if (ok) {
        new_game(A.level, seed);
        set_msg(C_DIM, L"seed 0x%08X loaded from the clipboard.", seed);
    } else {
        set_msg(C_AMBER, L"the clipboard does not hold a number.");
    }
    InvalidateRect(A.hwnd, NULL, FALSE);
}

static void act_help(void)
{
    MessageBoxW(A.hwnd,
        L"Mouse\n"
        L"  left click\t\topen a cell (on a number: chord)\n"
        L"  right click\t\tflag\n"
        L"  middle click\t\tchord\n\n"
        L"Keyboard\n"
        L"  arrows or h j k l\tmove the cursor\n"
        L"  space, enter\t\topen / chord\n"
        L"  f\t\t\tflag\n"
        L"  n, F2\t\t\tnew game\n"
        L"  r\t\t\tretry the same seed\n"
        L"  1 2 3\t\t\tbeginner, intermediate, expert\n"
        L"  ?\t\t\thint\n"
        L"  p\t\t\tshow mine probabilities\n"
        L"  s\t\t\tsolver step: apply every certain deduction\n"
        L"  Ctrl+C, Ctrl+V\t\tcopy / paste the seed\n"
        L"  q, Esc\t\t\tquit\n\n"
        L"A board is fully determined by its size, its seed and the first\n"
        L"cell you open. The solver sees only what you see; its numbers are\n"
        L"the exact chance, in percent, that a cell hides a mine.",
        L"mines - help", MB_OK | MB_ICONINFORMATION);
}

static void move_cursor(int dx, int dy)
{
    if (!A.cur_on) { A.cur_on = 1; }
    else {
        A.cur_x = (A.cur_x + dx + A.game.w) % A.game.w;
        A.cur_y = (A.cur_y + dy + A.game.h) % A.game.h;
    }
    InvalidateRect(A.hwnd, NULL, FALSE);
}

/* ------------------------------------------------------------- drawing -- */

static void fill(HDC dc, int x, int y, int w, int h, COLORREF c)
{
    RECT r = { x, y, x + w, y + h };
    SetBkColor(dc, c);
    ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &r, NULL, 0, NULL);
}

static void frame(HDC dc, int x, int y, int w, int h, int t, COLORREF c)
{
    fill(dc, x, y, w, t, c);
    fill(dc, x, y + h - t, w, t, c);
    fill(dc, x, y, t, h, c);
    fill(dc, x + w - t, y, t, h, c);
}

static void label(HDC dc, int x, int y, int w, int h, const wchar_t *s, COLORREF c, UINT fmt)
{
    RECT r = { x, y, x + w, y + h };
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, fmt | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
}

/* Draw `s` at *x and advance *x past it. */
static void put(HDC dc, int *x, int y, int h, const wchar_t *s, COLORREF c)
{
    SIZE sz;
    GetTextExtentPoint32W(dc, s, (int)wcslen(s), &sz);
    label(dc, *x, y, sz.cx, h, s, c, DT_LEFT);
    *x += sz.cx;
}

static COLORREF mix(COLORREF a, COLORREF b, double t)
{
    return RGB((int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t),
               (int)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t),
               (int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t));
}

static void draw_cell(HDC dc, int i)
{
    const ms_game *g = &A.game;
    int cx = i % g->w, cy = i / g->w;
    int x = A.board_x + cx * A.cell, y = A.board_y + cy * A.cell;
    int inner = A.cell - 1;                      /* 1 px of background = grid line */
    uint8_t c = g->cell[i];
    int over = !playing();
    wchar_t text[4] = L"";
    COLORREF bg = C_HIDDEN, fg = C_TEXT;
    HFONT font = A.f_cell;

    if (c & MS_OPEN) {
        bg = C_OPEN;
        if (c & MS_MINE) { bg = C_BOOM; fg = C_BG; wcscpy(text, L"*"); }
        else if (c & MS_ADJ) { text[0] = (wchar_t)(L'0' + (c & MS_ADJ)); fg = NUMBER_COLOR[c & MS_ADJ]; }
    } else if (over) {
        if (c & MS_FLAG) {
            int right = (c & MS_MINE) != 0;
            wcscpy(text, right ? L"F" : L"x");
            fg = right ? (g->state == MS_WON ? C_GREEN : C_AMBER) : C_RED;
        } else if (c & MS_MINE) {
            wcscpy(text, L"*");
            fg = C_RED;
        }
    } else {
        int prob = A.show_prob && A.an_ok;
        if (prob) {
            double p = A.an.p[i];
            bg = mix(RGB(0x16, 0x45, 0x2a), RGB(0x6b, 0x1f, 0x1f), p);
            if (!(c & MS_FLAG)) {
                font = A.f_small;
                if (A.an.verdict[i] == MS_SAFE) { wcscpy(text, L"00"); fg = C_GREEN; }
                else if (A.an.verdict[i] == MS_SURE_MINE) { wcscpy(text, L"!!"); fg = C_RED; }
                else {
                    int pct = (int)(p * 100.0 + 0.5);
                    if (pct < 1) wcscpy(text, L"<1");
                    else _snwprintf(text, 3, L"%02d", pct > 99 ? 99 : pct);
                    fg = C_TEXT;
                }
            }
        }
        if (c & MS_FLAG) { wcscpy(text, L"F"); fg = C_AMBER; }
    }

    fill(dc, x, y, inner, inner, bg);
    if (text[0]) {
        SelectObject(dc, font);
        label(dc, x, y, inner, inner, text, fg, DT_CENTER);
    }
    if (i == A.hint) frame(dc, x, y, inner, inner, S(2), C_GREEN);
    if (A.cur_on && cx == A.cur_x && cy == A.cur_y) frame(dc, x, y, inner, inner, S(2), C_BLUE);
}

static void paint(HDC dc)
{
    const ms_game *g = &A.game;
    wchar_t buf[64];

    fill(dc, 0, 0, A.client_w, A.client_h, C_BG);
    SetBkMode(dc, TRANSPARENT);

    /* Header: mines left, clock, seed, 3BV, level. */
    SelectObject(dc, A.f_ui);
    int x = A.margin, y = A.margin, h = A.head_h;
    ULONGLONG now = A.t_end ? A.t_end : GetTickCount64();
    double secs = A.t_start ? (double)(now - A.t_start) / 1000.0 : 0.0;

    put(dc, &x, y, h, L"mines ", C_DIM);
    _snwprintf(buf, 63, L"%03d", g->mines - g->flags);
    put(dc, &x, y, h, buf, C_TEXT);
    put(dc, &x, y, h, L"   time ", C_DIM);
    _snwprintf(buf, 63, L"%05.1f", secs);
    put(dc, &x, y, h, buf, C_TEXT);
    put(dc, &x, y, h, L"   seed ", C_DIM);
    _snwprintf(buf, 63, L"0x%08X", g->seed);
    put(dc, &x, y, h, buf, C_BLUE);
    put(dc, &x, y, h, L"   3BV ", C_DIM);
    if (g->bbbv) _snwprintf(buf, 63, L"%d", g->bbbv); else wcscpy(buf, L"--");
    put(dc, &x, y, h, buf, C_TEXT);
    label(dc, A.margin, y, A.client_w - 2 * A.margin, h, LEVELS[A.level].name, C_DIM, DT_RIGHT);

    /* Hexadecimal axes. */
    SelectObject(dc, A.f_small);
    for (int cx = 0; cx < g->w; cx++) {
        _snwprintf(buf, 63, L"%X", cx);
        label(dc, A.board_x + cx * A.cell, A.board_y - A.gutter, A.cell, A.gutter, buf, C_DIM, DT_CENTER);
    }
    for (int cy = 0; cy < g->h; cy++) {
        _snwprintf(buf, 63, L"%X", cy);
        label(dc, A.board_x - A.gutter, A.board_y + cy * A.cell, A.gutter - S(6), A.cell, buf, C_DIM, DT_RIGHT);
    }

    for (int i = 0; i < g->w * g->h; i++) draw_cell(dc, i);

    /* Footer: a one-line log. */
    SelectObject(dc, A.f_ui);
    x = A.margin;
    y = A.client_h - A.margin - A.foot_h;
    put(dc, &x, y, A.foot_h, L"> ", C_GREEN);
    label(dc, x, y, A.client_w - A.margin - x, A.foot_h, A.msg, A.msg_color, DT_LEFT | DT_END_ELLIPSIS);
}

static void on_paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ old_bmp = SelectObject(mem, bmp);
    HGDIOBJ old_font = SelectObject(mem, A.f_ui);

    paint(mem);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);

    SelectObject(mem, old_font);
    SelectObject(mem, old_bmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

/* -------------------------------------------------------------- window -- */

static HMENU build_menu(void)
{
    HMENU bar = CreateMenu(), game = CreatePopupMenu(), solver = CreatePopupMenu();

    AppendMenuW(game, MF_STRING, IDM_NEW,        L"&New\tN");
    AppendMenuW(game, MF_STRING, IDM_RETRY,      L"&Retry same seed\tR");
    AppendMenuW(game, MF_SEPARATOR, 0, NULL);
    AppendMenuW(game, MF_STRING, IDM_LEVEL0,     L"&Beginner  9x9, 10 mines\t1");
    AppendMenuW(game, MF_STRING, IDM_LEVEL1,     L"&Intermediate  16x16, 40 mines\t2");
    AppendMenuW(game, MF_STRING, IDM_LEVEL2,     L"&Expert  30x16, 99 mines\t3");
    AppendMenuW(game, MF_SEPARATOR, 0, NULL);
    AppendMenuW(game, MF_STRING, IDM_COPY_SEED,  L"&Copy seed\tCtrl+C");
    AppendMenuW(game, MF_STRING, IDM_PASTE_SEED, L"&Paste seed\tCtrl+V");
    AppendMenuW(game, MF_SEPARATOR, 0, NULL);
    AppendMenuW(game, MF_STRING, IDM_EXIT,       L"&Quit\tQ");

    AppendMenuW(solver, MF_STRING, IDM_HINT,     L"&Hint\t?");
    AppendMenuW(solver, MF_STRING, IDM_PROB,     L"Show &probabilities\tP");
    AppendMenuW(solver, MF_STRING, IDM_STEP,     L"&Step: apply certain deductions\tS");

    AppendMenuW(bar, MF_POPUP, (UINT_PTR)game,   L"&Game");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)solver, L"&Solver");
    AppendMenuW(bar, MF_STRING, IDM_HELP,        L"&Help");
    return bar;
}

static void on_command(int id)
{
    switch (id) {
    case IDM_NEW:        new_game(A.level, fresh_seed()); break;
    case IDM_RETRY:      new_game(A.level, A.game.seed); break;
    case IDM_LEVEL0:
    case IDM_LEVEL1:
    case IDM_LEVEL2:     new_game(id - IDM_LEVEL0, fresh_seed()); break;
    case IDM_COPY_SEED:  act_copy_seed(); break;
    case IDM_PASTE_SEED: act_paste_seed(); break;
    case IDM_EXIT:       DestroyWindow(A.hwnd); break;
    case IDM_HINT:       act_hint(); break;
    case IDM_PROB:       act_toggle_prob(); break;
    case IDM_STEP:       act_step(); break;
    case IDM_HELP:       act_help(); break;
    }
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    int x, y;

    switch (msg) {
    case WM_CREATE:
        SetTimer(hwnd, 1, 100, NULL);
        return 0;

    case WM_TIMER:
        if (A.game.state == MS_RUNNING) {
            RECT r = { 0, 0, A.client_w, A.margin + A.head_h };
            InvalidateRect(hwnd, &r, FALSE);
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        on_paint(hwnd);
        return 0;

    case WM_LBUTTONDOWN:
        if (cell_at(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &x, &y)) {
            A.cur_on = 0;
            act_open(x, y);
        }
        return 0;

    case WM_RBUTTONDOWN:
        if (cell_at(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &x, &y)) {
            A.cur_on = 0;
            act_flag(x, y);
        }
        return 0;

    case WM_MBUTTONDOWN:
        if (cell_at(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &x, &y) && playing()) {
            ms_chord(&A.game, x, y);
            after_move();
        }
        return 0;

    case WM_KEYDOWN:
        switch (wp) {
        case VK_LEFT:   move_cursor(-1, 0); break;
        case VK_RIGHT:  move_cursor(1, 0);  break;
        case VK_UP:     move_cursor(0, -1); break;
        case VK_DOWN:   move_cursor(0, 1);  break;
        case VK_F1:     act_help(); break;
        case VK_F2:     on_command(IDM_NEW); break;
        case VK_ESCAPE: DestroyWindow(hwnd); break;
        }
        return 0;

    case WM_CHAR:
        switch (towlower((wint_t)wp)) {
        case L'h': move_cursor(-1, 0); break;
        case L'l': move_cursor(1, 0);  break;
        case L'k': move_cursor(0, -1); break;
        case L'j': move_cursor(0, 1);  break;
        case L' ':
        case L'\r':
            if (!A.cur_on) move_cursor(0, 0);
            else act_open(A.cur_x, A.cur_y);
            break;
        case L'f':
            if (!A.cur_on) move_cursor(0, 0);
            else act_flag(A.cur_x, A.cur_y);
            break;
        case L'n': on_command(IDM_NEW); break;
        case L'r': on_command(IDM_RETRY); break;
        case L'1': on_command(IDM_LEVEL0); break;
        case L'2': on_command(IDM_LEVEL1); break;
        case L'3': on_command(IDM_LEVEL2); break;
        case L'?': act_hint(); break;
        case L'p': act_toggle_prob(); break;
        case L's': act_step(); break;
        case L'q': DestroyWindow(hwnd); break;
        case 0x03: act_copy_seed(); break;    /* Ctrl+C */
        case 0x16: act_paste_seed(); break;   /* Ctrl+V */
        }
        return 0;

    case WM_COMMAND:
        on_command(LOWORD(wp));
        return 0;

    case WM_DPICHANGED: {
        const RECT *r = (const RECT *)lp;
        A.dpi = HIWORD(wp);
        SetWindowPos(hwnd, NULL, r->left, r->top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        layout();
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)prev; (void)cmd;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = L"MinesWindow";
    if (!RegisterClassExW(&wc)) return 1;

    A.menu = build_menu();
    A.hint = -1;
    A.level = -1;
    A.hwnd = CreateWindowExW(0, wc.lpszClassName, L"mines",
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                             CW_USEDEFAULT, CW_USEDEFAULT, 640, 480,
                             NULL, A.menu, inst, NULL);
    if (!A.hwnd) return 1;

    BOOL dark = TRUE;
    DwmSetWindowAttribute(A.hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);

    A.dpi = GetDpiForWindow(A.hwnd);
    new_game(0, fresh_seed());

    ShowWindow(A.hwnd, show);
    UpdateWindow(A.hwnd);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return (int)m.wParam;
}
