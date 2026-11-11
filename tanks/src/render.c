/*
 * render.c - draws the game into a 512x448 pixel buffer.
 *
 * Everything is drawn from rectangles: there are no image files. Sprites are
 * described once, facing up, and rotated by mapping coordinates.
 */
#include "game.h"

#include <stdio.h>
#include <string.h>

#define RGB(r, g, b) ((uint32_t)(((r) << 16) | ((g) << 8) | (b)))

#define FIELD_X 16
#define FIELD_Y 16
#define PANEL_X 448

#define C_BORDER RGB(0x74, 0x74, 0x74)
#define C_BLACK  RGB(0x00, 0x00, 0x00)
#define C_WHITE  RGB(0xf4, 0xf4, 0xf4)
#define C_GREY   RGB(0xa0, 0xa4, 0xac)
#define C_DARK   RGB(0x20, 0x22, 0x28)
#define C_RED    RGB(0xd8, 0x3a, 0x2e)
#define C_ORANGE RGB(0xf0, 0x8a, 0x24)
#define C_YELLOW RGB(0xf6, 0xd0, 0x3c)
#define C_GREEN  RGB(0x3c, 0xa0, 0x4c)
#define C_BLUE   RGB(0x2c, 0x6c, 0xd8)

/* ------------------------------------------------------------ primitives -- */

static void rect(uint32_t *fb, int x, int y, int w, int h, uint32_t c)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > TK_FB_W ? TK_FB_W : x + w, y1 = y + h > TK_FB_H ? TK_FB_H : y + h;
    for (int py = y0; py < y1; py++)
        for (int px = x0; px < x1; px++) fb[py * TK_FB_W + px] = c;
}

/* A rectangle given in a sprite's own 32x32 "facing up" space, rotated to dir. */
static void srect(uint32_t *fb, int ox, int oy, int dir, int u, int v, int w, int h, uint32_t c)
{
    switch (dir) {
    case TK_UP:    rect(fb, ox + u, oy + v, w, h, c); break;
    case TK_RIGHT: rect(fb, ox + 32 - v - h, oy + u, h, w, c); break;
    case TK_DOWN:  rect(fb, ox + 32 - u - w, oy + 32 - v - h, w, h, c); break;
    default:       rect(fb, ox + v, oy + 32 - u - w, h, w, c); break;
    }
}

/* 5x7 font: digits, capitals and a little punctuation. One byte per row. */
static const uint8_t *glyph(char ch)
{
    static const uint8_t DIGITS[10][7] = {
        {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
        {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E},
        {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
        {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
        {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    };
    static const uint8_t LETTERS[26][7] = {
        {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
        {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
        {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
        {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
        {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
        {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
        {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
        {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
        {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
        {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
        {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
        {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
        {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    };
    static const uint8_t DASH[7]  = {0x00,0x00,0x00,0x1F,0x00,0x00,0x00};
    static const uint8_t COLON[7] = {0x00,0x04,0x00,0x00,0x00,0x04,0x00};
    static const uint8_t DOT[7]   = {0x00,0x00,0x00,0x00,0x00,0x06,0x06};
    static const uint8_t BANG[7]  = {0x04,0x04,0x04,0x04,0x04,0x00,0x04};
    static const uint8_t SLASH[7] = {0x01,0x02,0x02,0x04,0x08,0x08,0x10};
    static const uint8_t PLUS[7]  = {0x00,0x04,0x04,0x1F,0x04,0x04,0x00};
    static const uint8_t BLANK[7] = {0};

    if (ch >= '0' && ch <= '9') return DIGITS[ch - '0'];
    if (ch >= 'A' && ch <= 'Z') return LETTERS[ch - 'A'];
    if (ch >= 'a' && ch <= 'z') return LETTERS[ch - 'a'];
    switch (ch) {
    case '-': return DASH;
    case ':': return COLON;
    case '.': return DOT;
    case '!': return BANG;
    case '/': return SLASH;
    case '+': return PLUS;
    }
    return BLANK;
}

static int text_width(const char *s, int scale) { return (int)strlen(s) * 6 * scale - scale; }

static void text(uint32_t *fb, int x, int y, const char *s, int scale, uint32_t c)
{
    for (; *s; s++, x += 6 * scale) {
        const uint8_t *g = glyph(*s);
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (g[row] & (0x10 >> col)) rect(fb, x + col * scale, y + row * scale, scale, scale, c);
    }
}

static void text_centred(uint32_t *fb, int cx, int y, const char *s, int scale, uint32_t c)
{
    text(fb, cx - text_width(s, scale) / 2, y, s, scale, c);
}

/* --------------------------------------------------------------- sprites -- */

typedef struct { uint32_t body, light, dark, track; } palette;

static void draw_tank(uint32_t *fb, int ox, int oy, int dir, int tread, const palette *p)
{
    /* tracks, with treads that scroll as the tank moves */
    srect(fb, ox, oy, dir, 2, 3, 7, 27, p->track);
    srect(fb, ox, oy, dir, 23, 3, 7, 27, p->track);
    for (int v = 3 + (tread / 2) % 4; v < 29; v += 4) {
        srect(fb, ox, oy, dir, 2, v, 7, 1, C_BLACK);
        srect(fb, ox, oy, dir, 23, v, 7, 1, C_BLACK);
    }
    /* hull */
    srect(fb, ox, oy, dir, 9, 6, 14, 22, p->body);
    srect(fb, ox, oy, dir, 9, 6, 14, 2, p->light);
    srect(fb, ox, oy, dir, 9, 26, 14, 2, p->dark);
    /* turret and barrel */
    srect(fb, ox, oy, dir, 11, 12, 10, 11, p->dark);
    srect(fb, ox, oy, dir, 12, 13, 8, 9, p->light);
    srect(fb, ox, oy, dir, 14, 1, 4, 13, p->light);
    srect(fb, ox, oy, dir, 14, 1, 4, 2, p->dark);
}

static palette enemy_palette(const tk_tank *e, uint32_t frame)
{
    static const palette KINDS[4] = {
        { RGB(0xb4, 0xb8, 0xc0), RGB(0xe8, 0xea, 0xee), RGB(0x70, 0x74, 0x7c), RGB(0x50, 0x54, 0x5c) },   /* basic: steel */
        { RGB(0x2c, 0xa8, 0xa0), RGB(0x8c, 0xe8, 0xe0), RGB(0x14, 0x68, 0x64), RGB(0x10, 0x48, 0x48) },   /* fast: teal   */
        { RGB(0xb0, 0x5c, 0xd0), RGB(0xe4, 0xb0, 0xf4), RGB(0x6c, 0x30, 0x88), RGB(0x48, 0x20, 0x5c) },   /* power: violet */
        { RGB(0x48, 0x90, 0x48), RGB(0x9c, 0xd8, 0x90), RGB(0x24, 0x58, 0x28), RGB(0x1c, 0x3c, 0x20) },   /* armor: green */
    };
    palette p = KINDS[e->kind];
    if (e->kind == TK_ARMOR && e->hp <= 2) {        /* armour wears down to bare metal */
        p.body = e->hp == 2 ? RGB(0xc0, 0xa8, 0x48) : RGB(0xb4, 0xb8, 0xc0);
        p.light = e->hp == 2 ? RGB(0xf0, 0xe0, 0x90) : RGB(0xe8, 0xea, 0xee);
    }
    if (e->flash > 0) p.body = p.light = C_WHITE;
    if (e->bonus && (frame / 8) % 2) {              /* pickup carriers blink red */
        p.body = C_RED;
        p.light = RGB(0xff, 0x90, 0x80);
        p.dark = RGB(0x80, 0x18, 0x14);
    }
    return p;
}

/* A four-pointed sparkle where an enemy is about to appear. */
static void draw_spawn(uint32_t *fb, int ox, int oy, uint32_t frame)
{
    int r = 4 + (int)((frame / 3) % 4) * 3;
    rect(fb, ox + 16 - r, oy + 14, 2 * r, 4, C_WHITE);
    rect(fb, ox + 14, oy + 16 - r, 4, 2 * r, C_WHITE);
    rect(fb, ox + 16 - r / 2, oy + 16 - r / 2, r, r, C_YELLOW);
}

static void draw_boom(uint32_t *fb, const tk_boom *b)
{
    static const uint32_t RING[4] = { C_WHITE, C_YELLOW, C_ORANGE, C_RED };
    int life = b->big ? 30 : 10;
    int r = (b->big ? 6 : 3) + b->age * (b->big ? 18 : 6) / life;
    int stage = b->age * 4 / life;
    int x = FIELD_X + b->x, y = FIELD_Y + b->y;

    /* a plus-shaped burst that cools from white to red as it grows */
    rect(fb, x - r, y - r / 2, 2 * r, r, RING[stage]);
    rect(fb, x - r / 2, y - r, r, 2 * r, RING[stage]);
    if (stage < 3) rect(fb, x - r / 2, y - r / 2, r, r, RING[stage > 0 ? stage - 1 : 0]);
}

static void draw_cell(uint32_t *fb, int cx, int cy, int kind, uint32_t frame)
{
    int x = FIELD_X + cx * 8, y = FIELD_Y + cy * 8;
    switch (kind) {
    case TK_BRICK:
        rect(fb, x, y, 8, 8, RGB(0xb0, 0x4c, 0x28));
        rect(fb, x, y + 3, 8, 1, RGB(0x58, 0x24, 0x14));          /* mortar */
        rect(fb, x, y + 7, 8, 1, RGB(0x58, 0x24, 0x14));
        rect(fb, x + ((cx + cy) & 1 ? 2 : 6), y, 1, 3, RGB(0x58, 0x24, 0x14));
        rect(fb, x + ((cx + cy) & 1 ? 6 : 2), y + 4, 1, 3, RGB(0x58, 0x24, 0x14));
        rect(fb, x, y, 8, 1, RGB(0xd4, 0x74, 0x48));
        break;
    case TK_STEEL:
        rect(fb, x, y, 8, 8, RGB(0x8c, 0x94, 0xa0));
        rect(fb, x, y, 8, 1, C_WHITE);
        rect(fb, x, y, 1, 8, C_WHITE);
        rect(fb, x, y + 7, 8, 1, RGB(0x48, 0x4c, 0x58));
        rect(fb, x + 7, y, 1, 8, RGB(0x48, 0x4c, 0x58));
        rect(fb, x + 3, y + 3, 2, 2, RGB(0xd8, 0xdc, 0xe4));      /* rivet */
        break;
    case TK_WATER:
        rect(fb, x, y, 8, 8, RGB(0x1c, 0x50, 0xb8));
        rect(fb, x + (int)((frame / 20 + (uint32_t)cy) % 2) * 4, y + 2, 4, 1, RGB(0x78, 0xb0, 0xf8));
        rect(fb, x + (int)((frame / 20 + (uint32_t)cy + 1) % 2) * 4, y + 6, 4, 1, RGB(0x78, 0xb0, 0xf8));
        break;
    case TK_TREE:
        rect(fb, x, y, 8, 8, RGB(0x1c, 0x6c, 0x2c));
        rect(fb, x + 1, y + 1, 3, 3, RGB(0x44, 0xa8, 0x48));
        rect(fb, x + 5, y + 4, 2, 3, RGB(0x44, 0xa8, 0x48));
        rect(fb, x + 5, y, 2, 2, RGB(0x10, 0x48, 0x20));
        rect(fb, x + 1, y + 6, 2, 2, RGB(0x10, 0x48, 0x20));
        break;
    }
}

/* The base: a command post flying a pennant, or rubble once it is hit. */
static void draw_base(uint32_t *fb, int alive)
{
    int x = FIELD_X + 192, y = FIELD_Y + 384;
    if (!alive) {
        rect(fb, x + 3, y + 18, 26, 12, RGB(0x44, 0x40, 0x3c));
        rect(fb, x + 7, y + 12, 9, 8, RGB(0x60, 0x5c, 0x58));
        rect(fb, x + 18, y + 14, 8, 6, RGB(0x30, 0x2c, 0x28));
        rect(fb, x + 12, y + 22, 4, 4, C_ORANGE);
        rect(fb, x + 20, y + 24, 3, 3, C_RED);
        return;
    }
    rect(fb, x + 3, y + 20, 26, 10, RGB(0x70, 0x78, 0x88));       /* bunker */
    rect(fb, x + 3, y + 20, 26, 2, RGB(0xb8, 0xc0, 0xcc));
    rect(fb, x + 12, y + 24, 8, 6, C_DARK);                       /* door */
    rect(fb, x + 8, y + 2, 2, 18, C_WHITE);                       /* pole */
    rect(fb, x + 10, y + 3, 15, 10, C_YELLOW);                    /* pennant */
    rect(fb, x + 10, y + 3, 15, 2, RGB(0xff, 0xf0, 0xa0));
    rect(fb, x + 15, y + 6, 5, 5, C_RED);                         /* its badge */
    rect(fb, x + 17, y + 5, 1, 7, C_RED);
    rect(fb, x + 14, y + 8, 7, 1, C_RED);
}

static void draw_pickup(uint32_t *fb, const tk_game *g)
{
    static const char LABEL[6] = { ' ', 'S', '+', 'F', 'B', 'U' };   /* shield, life, freeze, bomb, upgrade */
    static const uint32_t TINT[6] = { 0, C_BLUE, C_GREEN, RGB(0x60, 0xc8, 0xe8), C_RED, C_YELLOW };
    int x = FIELD_X + g->pickup_x, y = FIELD_Y + g->pickup_y;
    char s[2] = { LABEL[g->pickup], 0 };

    if ((g->frame / 10) % 3 == 0) return;                          /* blink */
    rect(fb, x + 3, y + 3, 26, 26, C_WHITE);
    rect(fb, x + 5, y + 5, 22, 22, TINT[g->pickup]);
    text(fb, x + 9, y + 6, s, 3, C_WHITE);
}

/* ------------------------------------------------------------------ hud -- */

static void draw_panel(uint32_t *fb, const tk_game *g)
{
    static const palette ICON = { RGB(0xb4, 0xb8, 0xc0), C_WHITE, RGB(0x70, 0x74, 0x7c), C_DARK };
    char buf[16];
    int left = TK_STAGE_ENEMIES - g->spawned;
    int x = PANEL_X + 6;

    text(fb, x, 20, "ENEMY", 1, C_BLACK);
    for (int i = 0; i < left; i++) {
        int ix = x + 4 + (i % 2) * 20, iy = 32 + (i / 2) * 12;
        rect(fb, ix, iy + 2, 10, 7, ICON.track);
        rect(fb, ix + 2, iy + 3, 6, 5, ICON.body);
        rect(fb, ix + 4, iy, 2, 5, ICON.body);
    }

    text(fb, x, 176, "LIVES", 1, C_BLACK);
    snprintf(buf, sizeof buf, "%d", g->lives);
    text(fb, x + 14, 188, buf, 2, C_BLACK);

    text(fb, x, 224, "STAGE", 1, C_BLACK);
    snprintf(buf, sizeof buf, "%d", g->stage ? g->stage : 1);
    text(fb, x + 14, 236, buf, 2, C_BLACK);

    text(fb, x, 272, "SCORE", 1, C_BLACK);
    snprintf(buf, sizeof buf, "%d", g->score);
    text(fb, x, 284, buf, 1, C_BLACK);

    text(fb, x, 308, "BEST", 1, C_BLACK);
    snprintf(buf, sizeof buf, "%d", g->hiscore > g->score ? g->hiscore : g->score);
    text(fb, x, 320, buf, 1, C_BLACK);

    if (g->star > 0) {
        text(fb, x, 344, "GUN", 1, C_BLACK);
        for (int i = 0; i < g->star; i++) rect(fb, x + i * 12, 356, 9, 9, C_YELLOW);
    }
    if (g->freeze > 0) text(fb, x, 380, "FROZEN", 1, C_BLUE);
}

static void draw_title(uint32_t *fb, const tk_game *g)
{
    static const palette GOLD = { RGB(0xe8, 0xc0, 0x30), RGB(0xff, 0xf0, 0x98), RGB(0x98, 0x74, 0x10), RGB(0x60, 0x48, 0x08) };
    char buf[32];

    rect(fb, 0, 0, TK_FB_W, TK_FB_H, C_BLACK);
    for (int i = 0; i < TK_FB_W / 8; i++) {                  /* brick bands top and bottom */
        draw_cell(fb, i - 2, -2, TK_BRICK, 0);
        draw_cell(fb, i - 2, -1, TK_BRICK, 0);
        draw_cell(fb, i - 2, 52, TK_BRICK, 0);
        draw_cell(fb, i - 2, 53, TK_BRICK, 0);
    }
    text_centred(fb, TK_FB_W / 2, 64, "TANK", 9, C_ORANGE);
    text_centred(fb, TK_FB_W / 2, 140, "DEFENDER", 6, C_YELLOW);
    draw_tank(fb, TK_FB_W / 2 - 16, 204, TK_UP, (int)g->frame / 2, &GOLD);

    if ((g->frame / 30) % 2 == 0) text_centred(fb, TK_FB_W / 2, 256, "PRESS ENTER TO START", 2, C_WHITE);
    text_centred(fb, TK_FB_W / 2, 300, "ARROWS OR W A S D - MOVE", 1, C_GREY);
    text_centred(fb, TK_FB_W / 2, 314, "SPACE - FIRE", 1, C_GREY);
    text_centred(fb, TK_FB_W / 2, 328, "P - PAUSE    M - SOUND    ESC - QUIT", 1, C_GREY);
    text_centred(fb, TK_FB_W / 2, 352, "DEFEND THE FLAG. DESTROY 20 TANKS TO CLEAR A STAGE.", 1, C_GREY);
    snprintf(buf, sizeof buf, "BEST %d", g->hiscore);
    text_centred(fb, TK_FB_W / 2, 384, buf, 2, C_GREEN);
}

/* --------------------------------------------------------------- render -- */

void tk_render(const tk_game *g, uint32_t *fb)
{
    static const palette PLAYER[2] = {
        { RGB(0xe8, 0xc0, 0x30), RGB(0xff, 0xf0, 0x98), RGB(0x98, 0x74, 0x10), RGB(0x60, 0x48, 0x08) },
        { RGB(0xf8, 0xe0, 0x60), C_WHITE, RGB(0xb0, 0x88, 0x18), RGB(0x60, 0x48, 0x08) },
    };
    char buf[32];
    int cx = FIELD_X + TK_FIELD / 2;

    if (g->state == TK_TITLE) { draw_title(fb, g); return; }

    rect(fb, 0, 0, TK_FB_W, TK_FB_H, C_BORDER);
    rect(fb, PANEL_X - 8, 0, TK_FB_W - PANEL_X + 8, TK_FB_H, C_BORDER);
    rect(fb, FIELD_X, FIELD_Y, TK_FIELD, TK_FIELD, C_BLACK);

    if (g->state == TK_INTRO) {
        /* a curtain announcing the stage */
        rect(fb, FIELD_X, FIELD_Y, TK_FIELD, TK_FIELD, C_BORDER);
        snprintf(buf, sizeof buf, "STAGE %d", g->stage);
        text_centred(fb, cx, FIELD_Y + 190, buf, 4, C_BLACK);
        draw_panel(fb, g);
        return;
    }

    for (int y = 0; y < TK_CELLS; y++)
        for (int x = 0; x < TK_CELLS; x++)
            if (g->cell[y][x] != TK_EMPTY && g->cell[y][x] != TK_TREE) draw_cell(fb, x, y, g->cell[y][x], g->frame);

    draw_base(fb, g->base_alive);
    if (g->pickup) draw_pickup(fb, g);

    for (int i = 0; i < TK_MAX_ENEMIES; i++) {
        const tk_tank *e = &g->enemy[i];
        if (e->state == TK_SPAWNING) draw_spawn(fb, FIELD_X + e->x, FIELD_Y + e->y, g->frame);
        else if (e->state == TK_ACTIVE) {
            palette p = enemy_palette(e, g->frame);
            draw_tank(fb, FIELD_X + e->x, FIELD_Y + e->y, e->dir, e->tread, &p);
        }
    }

    if (g->player.state == TK_ACTIVE) {
        int px = FIELD_X + g->player.x, py = FIELD_Y + g->player.y;
        draw_tank(fb, px, py, g->player.dir, g->player.tread, &PLAYER[g->star > 0]);
        if (g->shield > 0 && (g->shield > 90 || (g->frame / 4) % 2)) {   /* flickers before it ends */
            uint32_t c = (g->frame / 3) % 2 ? C_WHITE : RGB(0x78, 0xb0, 0xf8);
            rect(fb, px - 1, py - 1, 34, 2, c);
            rect(fb, px - 1, py + 31, 34, 2, c);
            rect(fb, px - 1, py - 1, 2, 34, c);
            rect(fb, px + 31, py - 1, 2, 34, c);
        }
    }

    for (int i = 0; i < TK_MAX_BULLETS; i++) {
        const tk_bullet *b = &g->bullet[i];
        if (!b->alive) continue;
        rect(fb, FIELD_X + b->x + 1, FIELD_Y + b->y + 1, 6, 6, b->power ? C_ORANGE : C_WHITE);
        rect(fb, FIELD_X + b->x + 2, FIELD_Y + b->y + 2, 4, 4, RGB(0xd0, 0xd4, 0xdc));
    }

    /* Trees go on top: tanks and shells pass underneath the canopy. */
    for (int y = 0; y < TK_CELLS; y++)
        for (int x = 0; x < TK_CELLS; x++)
            if (g->cell[y][x] == TK_TREE) draw_cell(fb, x, y, TK_TREE, g->frame);

    for (int i = 0; i < TK_MAX_BOOMS; i++)
        if (g->boom[i].alive) draw_boom(fb, &g->boom[i]);

    /* Anything drawn past the field edge is covered by the frame again. */
    rect(fb, 0, 0, TK_FB_W, FIELD_Y, C_BORDER);
    rect(fb, 0, FIELD_Y + TK_FIELD, TK_FB_W, TK_FB_H - FIELD_Y - TK_FIELD, C_BORDER);
    rect(fb, 0, 0, FIELD_X, TK_FB_H, C_BORDER);
    rect(fb, FIELD_X + TK_FIELD, 0, PANEL_X - 8 - FIELD_X - TK_FIELD, TK_FB_H, C_BORDER);
    rect(fb, PANEL_X - 8, 0, TK_FB_W - PANEL_X + 8, TK_FB_H, C_BORDER);
    draw_panel(fb, g);

    if (g->state == TK_PAUSE) {
        rect(fb, cx - 80, FIELD_Y + 184, 160, 40, C_BLACK);
        text_centred(fb, cx, FIELD_Y + 194, "PAUSED", 3, (g->frame / 30) % 2 ? C_YELLOW : C_WHITE);
    } else if (g->state == TK_CLEAR) {
        rect(fb, cx - 130, FIELD_Y + 170, 260, 64, C_BLACK);
        text_centred(fb, cx, FIELD_Y + 180, "STAGE CLEAR", 3, C_GREEN);
        snprintf(buf, sizeof buf, "SCORE %d", g->score);
        text_centred(fb, cx, FIELD_Y + 212, buf, 2, C_WHITE);
    } else if (g->state == TK_OVER) {
        /* the banner rises from the bottom of the field, then settles */
        int rise = g->state_timer * 3;
        int y = FIELD_Y + TK_FIELD - (rise > 250 ? 250 : rise);
        rect(fb, cx - 116, y - 10, 232, 70, C_BLACK);
        text_centred(fb, cx, y, "GAME OVER", 4, C_RED);
        if (g->state_timer > 120 && (g->frame / 30) % 2 == 0) text_centred(fb, cx, y + 38, "PRESS ENTER", 2, C_WHITE);
        rect(fb, 0, FIELD_Y + TK_FIELD, PANEL_X - 8, TK_FB_H - FIELD_Y - TK_FIELD, C_BORDER);
    }
}
