/*
 * main.c - Windows shell for Tank Defender.
 *
 * The game itself (game.c, render.c) knows nothing about Windows. This file
 * opens a window, turns key presses into a tk_input, steps the game 60 times
 * a second, stretches its 512x448 picture onto the window, and plays short
 * synthesised sounds. One executable, no data files, no dependencies.
 */
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <mmsystem.h>
#include <stdint.h>
#include <string.h>

#include "game.h"

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winmm.lib")
#endif

static tk_game  G;
static uint32_t FB[TK_FB_W * TK_FB_H];

/* ---------------------------------------------------------------- input -- */

static int held[4];          /* per direction: 0, or the order it was pressed in */
static int press_order;
static int fire_held, start_pressed, pause_pressed;

static int key_dir(WPARAM vk)
{
    switch (vk) {
    case VK_UP:    case 'W': return TK_UP;
    case VK_RIGHT: case 'D': return TK_RIGHT;
    case VK_DOWN:  case 'S': return TK_DOWN;
    case VK_LEFT:  case 'A': return TK_LEFT;
    }
    return -1;
}

/* The most recently pressed direction that is still held wins. */
static tk_input read_input(void)
{
    tk_input in = { -1, fire_held, start_pressed, pause_pressed };
    int best = 0;
    for (int d = 0; d < 4; d++)
        if (held[d] > best) { best = held[d]; in.dir = d; }
    start_pressed = pause_pressed = 0;
    return in;
}

static void release_all(void)
{
    memset(held, 0, sizeof held);
    fire_held = 0;
}

/* ---------------------------------------------------------------- sound -- */

#define RATE 22050
enum { S_BRICK, S_STEEL, S_SHOT, S_HIT, S_EXPLODE, S_PICKUP, S_START, S_CLEAR, S_DIE, S_GAMEOVER, S_COUNT };

static uint8_t  wav[S_COUNT][44 + RATE];   /* each sound is at most one second */
static int      wav_ms[S_COUNT];
static int      muted;
static int      playing = -1;              /* sound in progress and when it ends */
static ULONGLONG playing_until;

static uint32_t noise_state = 0x1234567;
static int noise(void)
{
    noise_state ^= noise_state << 13; noise_state ^= noise_state >> 17; noise_state ^= noise_state << 5;
    return (int)(noise_state & 0xFF) - 128;
}

/*
 * Build one sound as an 8-bit mono WAV image in memory.
 * notes: pairs of (frequency in Hz, duration in ms), ending with 0. A
 * frequency of 1 means noise. Each note fades out; `slide` bends the pitch
 * by that many Hz over the note.
 */
static void make_sound(int id, const int *notes, int slide, int volume)
{
    uint8_t *data = wav[id] + 44;
    int n = 0;
    for (; notes[0]; notes += 2) {
        int count = notes[1] * RATE / 1000;
        double phase = 0.0;
        for (int i = 0; i < count && n < RATE; i++, n++) {
            int level = volume * (count - i) / count;
            int sample;
            if (notes[0] == 1) {
                sample = noise() * level / 128;
            } else {
                double f = notes[0] + (double)slide * i / count;
                phase += f / RATE;
                phase -= (int)phase;
                sample = phase < 0.5 ? level : -level;
            }
            data[n] = (uint8_t)(128 + sample);
        }
    }
    uint8_t *h = wav[id];
    uint32_t size = (uint32_t)n, rate = RATE;
    memcpy(h, "RIFF", 4);  { uint32_t v = 36 + size; memcpy(h + 4, &v, 4); }
    memcpy(h + 8, "WAVEfmt ", 8);
    { uint32_t v = 16; memcpy(h + 16, &v, 4); }
    { uint16_t v = 1; memcpy(h + 20, &v, 2); memcpy(h + 22, &v, 2); }    /* PCM, mono */
    memcpy(h + 24, &rate, 4);
    memcpy(h + 28, &rate, 4);                                            /* bytes per second */
    { uint16_t v = 1; memcpy(h + 32, &v, 2); v = 8; memcpy(h + 34, &v, 2); }
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &size, 4);
    wav_ms[id] = n * 1000 / RATE;
}

static void make_sounds(void)
{
    static const int brick[]    = { 1, 45, 0 };
    static const int steel[]    = { 1700, 40, 0 };
    static const int shot[]     = { 900, 90, 0 };
    static const int hit[]      = { 520, 80, 0 };
    static const int explode[]  = { 1, 380, 0 };
    static const int pickup[]   = { 660, 70, 880, 70, 1320, 140, 0 };
    static const int start[]    = { 392, 110, 523, 110, 659, 110, 784, 260, 0 };
    static const int clear[]    = { 523, 100, 659, 100, 784, 100, 1047, 300, 0 };
    static const int die[]      = { 1, 250, 220, 400, 0 };
    static const int gameover[] = { 392, 220, 330, 220, 262, 220, 196, 320, 0 };

    make_sound(S_BRICK, brick, 0, 40);
    make_sound(S_STEEL, steel, 0, 30);
    make_sound(S_SHOT, shot, -600, 45);
    make_sound(S_HIT, hit, -200, 50);
    make_sound(S_EXPLODE, explode, 0, 90);
    make_sound(S_PICKUP, pickup, 0, 50);
    make_sound(S_START, start, 0, 50);
    make_sound(S_CLEAR, clear, 0, 50);
    make_sound(S_DIE, die, -120, 90);
    make_sound(S_GAMEOVER, gameover, 0, 55);
}

/* One sound at a time: a new one cuts in only if it matters at least as much. */
static void play_events(uint32_t ev)
{
    static const struct { uint32_t event; int sound; } MAP[] = {
        { TK_EV_GAMEOVER, S_GAMEOVER }, { TK_EV_DIE, S_DIE }, { TK_EV_CLEAR, S_CLEAR },
        { TK_EV_START, S_START }, { TK_EV_PICKUP, S_PICKUP }, { TK_EV_EXPLODE, S_EXPLODE },
        { TK_EV_HIT, S_HIT }, { TK_EV_SHOT, S_SHOT }, { TK_EV_STEEL, S_STEEL }, { TK_EV_BRICK, S_BRICK },
    };
    if (!ev || muted) return;
    for (size_t i = 0; i < sizeof MAP / sizeof MAP[0]; i++) {
        if (!(ev & MAP[i].event)) continue;
        int s = MAP[i].sound;
        ULONGLONG now = GetTickCount64();
        if (playing >= 0 && now < playing_until && s < playing) return;
        PlaySoundA((LPCSTR)wav[s], NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
        playing = s;
        playing_until = now + (ULONGLONG)wav_ms[s];
        return;
    }
}

/* -------------------------------------------------------------- display -- */

/* Stretch the frame onto the window: whole-number scale when it fits, centred. */
static void present(HWND hwnd, HDC dc)
{
    static const BITMAPINFO bmi = {
        { sizeof(BITMAPINFOHEADER), TK_FB_W, -TK_FB_H, 1, 32, BI_RGB, 0, 0, 0, 0, 0 }, { { 0, 0, 0, 0 } }
    };
    RECT rc;
    GetClientRect(hwnd, &rc);
    int cw = rc.right, ch = rc.bottom;
    if (cw <= 0 || ch <= 0) return;

    int w, h;
    int scale = cw / TK_FB_W < ch / TK_FB_H ? cw / TK_FB_W : ch / TK_FB_H;
    if (scale >= 1) { w = TK_FB_W * scale; h = TK_FB_H * scale; }
    else if (cw * TK_FB_H < ch * TK_FB_W) { w = cw; h = cw * TK_FB_H / TK_FB_W; }
    else { h = ch; w = ch * TK_FB_W / TK_FB_H; }
    int x = (cw - w) / 2, y = (ch - h) / 2;

    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, x, y, w, h, 0, 0, TK_FB_W, TK_FB_H, FB, &bmi, DIB_RGB_COLORS, SRCCOPY);

    HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RECT bars[4] = { { 0, 0, cw, y }, { 0, y + h, cw, ch }, { 0, y, x, y + h }, { x + w, y, cw, y + h } };
    for (int i = 0; i < 4; i++)
        if (bars[i].right > bars[i].left && bars[i].bottom > bars[i].top) FillRect(dc, &bars[i], black);
}

/* --------------------------------------------------------------- window -- */

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_KEYDOWN: {
        int repeat = (lp & (1 << 30)) != 0;
        int d = key_dir(wp);
        if (d >= 0) { if (!held[d]) held[d] = ++press_order; }
        else if (wp == VK_SPACE) fire_held = 1;
        else if (repeat) break;
        else if (wp == VK_RETURN) start_pressed = 1;
        else if (wp == 'P') pause_pressed = 1;
        else if (wp == 'M') { muted = !muted; if (muted) PlaySoundA(NULL, NULL, 0); }
        else if (wp == VK_ESCAPE) DestroyWindow(hwnd);
        return 0;
    }
    case WM_KEYUP: {
        int d = key_dir(wp);
        if (d >= 0) held[d] = 0;
        else if (wp == VK_SPACE) fire_held = 0;
        return 0;
    }
    case WM_KILLFOCUS:
        release_all();
        if (G.state == TK_PLAY) pause_pressed = 1;      /* pause when the window loses focus */
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        present(hwnd, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMinTrackSize.x = TK_FB_W / 2 + 40;
        mm->ptMinTrackSize.y = TK_FB_H / 2 + 60;
        return 0;
    }
    case WM_DESTROY:
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
    wc.lpszClassName = L"TankDefenderWindow";
    if (!RegisterClassExW(&wc)) return 1;

    /* Open at the largest whole-number scale that fits the desktop. */
    const DWORD style = WS_OVERLAPPEDWINDOW;
    RECT work = { 0, 0, 1280, 720 };
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int scale = 4;
    RECT r;
    for (;;) {
        r = (RECT){ 0, 0, TK_FB_W * scale, TK_FB_H * scale };
        AdjustWindowRectEx(&r, style, FALSE, 0);
        if (scale == 1 || (r.right - r.left <= work.right - work.left && r.bottom - r.top <= work.bottom - work.top)) break;
        scale--;
    }
    int ww = r.right - r.left, wh = r.bottom - r.top;
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Tank Defender", style,
                                work.left + (work.right - work.left - ww) / 2,
                                work.top + (work.bottom - work.top - wh) / 2,
                                ww, wh, NULL, NULL, inst, NULL);
    if (!hwnd) return 1;

    LARGE_INTEGER freq, last, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);

    make_sounds();
    tk_init(&G, (uint32_t)last.QuadPart ^ (uint32_t)GetTickCount64());
    tk_render(&G, FB);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    timeBeginPeriod(1);            /* so Sleep(1) really is about a millisecond */

    /* Fixed 60 Hz simulation, however fast or slow the display is. */
    const double step = 1.0 / 60.0;
    double pending = 0.0;
    int running = 1;
    while (running) {
        MSG m;
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT) { running = 0; break; }
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (!running) break;

        QueryPerformanceCounter(&now);
        pending += (double)(now.QuadPart - last.QuadPart) / (double)freq.QuadPart;
        last = now;
        if (pending > 0.25) pending = 0.25;          /* after a stall, do not fast-forward */

        int ticked = 0;
        while (pending >= step) {
            tk_input in = read_input();
            tk_update(&G, &in);
            play_events(G.events);
            pending -= step;
            ticked = 1;
        }
        if (ticked) {
            tk_render(&G, FB);
            HDC dc = GetDC(hwnd);
            present(hwnd, dc);
            ReleaseDC(hwnd, dc);
        }
        Sleep(1);
    }

    timeEndPeriod(1);
    PlaySoundA(NULL, NULL, 0);
    return 0;
}
