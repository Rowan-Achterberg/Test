/*
 * game.h - Tank Defender: a top-down tank game in the classic arcade style.
 *
 * The whole game is portable C with no I/O. One call to tk_update() advances
 * the world by 1/60 s from an input snapshot; tk_render() draws it into a
 * 512x448 pixel buffer. The platform layer (main.c) only pumps keys in and
 * copies pixels out, which also makes the game fully testable off-screen.
 *
 * Geometry: the field is 416x416 pixels = 13x13 tiles of 32 px. Terrain is
 * stored in 8 px cells (52x52) so brick walls chip away a slab at a time.
 * Tanks are 32x32 boxes, bullets 8x8.
 */
#ifndef TK_GAME_H
#define TK_GAME_H

#include <stdint.h>

#define TK_FB_W   512
#define TK_FB_H   448
#define TK_FIELD  416          /* field size in pixels             */
#define TK_CELLS  52           /* field size in 8 px terrain cells */
#define TK_TILES  13

#define TK_MAX_ENEMIES   6
#define TK_MAX_BULLETS   16
#define TK_MAX_BOOMS     16
#define TK_STAGE_ENEMIES 20
#define TK_NUM_MAPS      5

enum { TK_EMPTY, TK_BRICK, TK_STEEL, TK_WATER, TK_TREE };
enum { TK_UP, TK_RIGHT, TK_DOWN, TK_LEFT };
enum { TK_TITLE, TK_INTRO, TK_PLAY, TK_PAUSE, TK_CLEAR, TK_OVER };
enum { TK_BASIC, TK_FAST, TK_POWER, TK_ARMOR };
enum { TK_PICK_NONE, TK_PICK_SHIELD, TK_PICK_LIFE, TK_PICK_FREEZE, TK_PICK_BOMB, TK_PICK_STAR };
enum { TK_GONE, TK_SPAWNING, TK_ACTIVE };

/* Things that happened this tick, for the platform layer to make noise. */
enum {
    TK_EV_SHOT     = 1 << 0,
    TK_EV_BRICK    = 1 << 1,
    TK_EV_STEEL    = 1 << 2,
    TK_EV_HIT      = 1 << 3,   /* armoured enemy took a hit */
    TK_EV_EXPLODE  = 1 << 4,
    TK_EV_DIE      = 1 << 5,   /* player destroyed */
    TK_EV_PICKUP   = 1 << 6,
    TK_EV_GAMEOVER = 1 << 7,
    TK_EV_START    = 1 << 8,
    TK_EV_CLEAR    = 1 << 9
};

typedef struct {
    int dir;     /* TK_UP..TK_LEFT, or -1 when no direction is held */
    int fire;    /* held */
    int start;   /* pressed this tick */
    int pause;   /* pressed this tick */
} tk_input;

typedef struct {
    int state;       /* TK_GONE, TK_SPAWNING, TK_ACTIVE                   */
    int x, y, dir;   /* top-left corner in field pixels                   */
    int speed, sub;  /* speed in 1/16 px per tick, and the remainder      */
    int kind, hp;
    int bonus;       /* drops a pickup when destroyed                     */
    int timer;       /* spawn countdown, then ticks until the next rethink */
    int blocked;     /* consecutive ticks spent pushing against something */
    int cooldown;    /* ticks until it may fire again                     */
    int flash;       /* ticks left of the "just hit" flash                */
    int tread;       /* animation phase                                   */
} tk_tank;

typedef struct { int alive, x, y, dir, speed, owner, power; } tk_bullet;   /* owner: -1 player, else enemy index */
typedef struct { int alive, x, y, age, big; } tk_boom;

typedef struct {
    uint8_t   cell[TK_CELLS][TK_CELLS];   /* [row][col] */
    tk_tank   player;
    tk_tank   enemy[TK_MAX_ENEMIES];
    tk_bullet bullet[TK_MAX_BULLETS];
    tk_boom   boom[TK_MAX_BOOMS];

    int state, state_timer;
    int stage, score, hiscore, lives;
    int star;          /* player upgrade level 0..3                     */
    int shield;        /* ticks of invulnerability left                 */
    int respawn;       /* ticks until the player reappears              */
    int freeze;        /* ticks the enemies stay frozen                 */
    int base_alive;

    int queue[TK_STAGE_ENEMIES];   /* enemy kinds in spawn order */
    int spawned, killed;
    int spawn_timer, spawn_slot;

    int pickup, pickup_x, pickup_y, pickup_timer;

    uint32_t events;   /* TK_EV_* raised during the last update */
    uint32_t frame;
    uint32_t rng;
    int      invincible;   /* test hook: player and base cannot be destroyed */
} tk_game;

void tk_init(tk_game *g, uint32_t seed);
void tk_update(tk_game *g, const tk_input *in);
void tk_render(const tk_game *g, uint32_t *fb);   /* fb: TK_FB_W * TK_FB_H pixels, 0x00RRGGBB */

/* Exposed for tests. */
void tk_start_stage(tk_game *g, int stage);
int  tk_solid_at(const tk_game *g, int x, int y);   /* does a 32x32 box at (x, y) hit terrain or leave the field? */
const char *const *tk_map(int index);               /* 13 strings of 13 characters */

#endif
