/* game.c - Tank Defender rules and simulation. See game.h for the model. */
#include "game.h"

#include <string.h>

#define BASE_X 192
#define BASE_Y 384
#define PLAYER_X 128
#define PLAYER_Y 384
#define MAX_ON_FIELD 4

static const int DX[4] = { 0, 1, 0, -1 };
static const int DY[4] = { -1, 0, 1, 0 };

/* Per enemy kind: speed (1/16 px per tick), bullet speed (px), armour, score. */
static const struct { int speed, bullet, hp, score; } KIND[4] = {
    { 12, 4, 1, 100 },   /* basic */
    { 30, 4, 1, 200 },   /* fast  */
    { 16, 8, 1, 300 },   /* power */
    { 12, 4, 4, 400 },   /* armor */
};

/* ---------------------------------------------------------------- maps -- */

/*
 * One character per 32 px tile:
 *   .  open ground      #  brick        @  steel
 *   ~  water            %  trees
 * The top row holds the enemy spawn points; the base and its brick fort are
 * added at the bottom centre when a stage loads.
 */
static const char *const MAPS[TK_NUM_MAPS][TK_TILES] = {
    {   /* 1: courtyard */
        ".............",
        ".##.#...#.##.",
        ".#..#.@.#..#.",
        "....#...#....",
        ".##...#...##.",
        "....%%#%%....",
        "#.#.%...%.#.#",
        "....%%#%%....",
        ".##...#...##.",
        "....#...#....",
        ".#..#.@.#..#.",
        ".##.......##.",
        ".............",
    },
    {   /* 2: river */
        ".............",
        ".#.#..@..#.#.",
        ".#.#.....#.#.",
        ".....###.....",
        "~~~~.....~~~~",
        "....%%.%%....",
        ".##.%...%.##.",
        "....%%.%%....",
        "~~.~~~.~~~.~~",
        ".............",
        ".#.##...##.#.",
        ".#....@....#.",
        ".............",
    },
    {   /* 3: forest */
        ".............",
        ".%%%.#.#.%%%.",
        ".%@%.#.#.%@%.",
        ".%%%.....%%%.",
        "....##.##....",
        ".#..#...#..#.",
        ".#.%%%.%%%.#.",
        ".#..#...#..#.",
        "....##.##....",
        ".%%%.....%%%.",
        ".%#%.#.#.%#%.",
        ".%%%.....%%%.",
        ".............",
    },
    {   /* 4: bunkers */
        ".............",
        ".@#...#...#@.",
        ".##.#.#.#.##.",
        "....#...#....",
        ".#.###.###.#.",
        ".#...........",
        ".#.@.~~~.@.#.",
        "...........#.",
        ".#.###.###.#.",
        "....#...#....",
        ".##.#...#.##.",
        ".@#.......#@.",
        ".............",
    },
    {   /* 5: maze */
        ".............",
        ".####.#.####.",
        ".#....#....#.",
        ".#.##.#.##.#.",
        "...#.....#...",
        ".#.#.@#@.#.#.",
        ".#....~....#.",
        ".#.#.@#@.#.#.",
        "...#.....#...",
        ".#.##...##.#.",
        ".#.........#.",
        ".####...####.",
        ".............",
    },
};

const char *const *tk_map(int index)
{
    return MAPS[((index % TK_NUM_MAPS) + TK_NUM_MAPS) % TK_NUM_MAPS];
}

/* ------------------------------------------------------------- helpers -- */

static uint32_t rnd(tk_game *g)
{
    uint32_t x = g->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g->rng = x;
}

static int rnd_below(tk_game *g, int n) { return (int)(rnd(g) % (uint32_t)n); }

static int overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

static void fill_cells(tk_game *g, int cx, int cy, int w, int h, int kind)
{
    for (int y = cy; y < cy + h; y++)
        for (int x = cx; x < cx + w; x++)
            if (x >= 0 && x < TK_CELLS && y >= 0 && y < TK_CELLS) g->cell[y][x] = (uint8_t)kind;
}

static void add_boom(tk_game *g, int cx, int cy, int big)
{
    for (int i = 0; i < TK_MAX_BOOMS; i++) {
        if (g->boom[i].alive) continue;
        g->boom[i] = (tk_boom){ 1, cx, cy, 0, big };
        return;
    }
}

/* Would a 32x32 box at (x, y) hit terrain, the base, or the field edge? */
int tk_solid_at(const tk_game *g, int x, int y)
{
    if (x < 0 || y < 0 || x > TK_FIELD - 32 || y > TK_FIELD - 32) return 1;
    if (overlap(x, y, 32, 32, BASE_X, BASE_Y, 32, 32)) return 1;
    for (int cy = y / 8; cy <= (y + 31) / 8; cy++)
        for (int cx = x / 8; cx <= (x + 31) / 8; cx++) {
            int c = g->cell[cy][cx];
            if (c == TK_BRICK || c == TK_STEEL || c == TK_WATER) return 1;
        }
    return 0;
}

/* --------------------------------------------------------------- stage -- */

static void load_map(tk_game *g, int index)
{
    const char *const *map = tk_map(index);
    memset(g->cell, TK_EMPTY, sizeof g->cell);
    for (int ty = 0; ty < TK_TILES; ty++) {
        for (int tx = 0; tx < TK_TILES; tx++) {
            int kind = TK_EMPTY;
            switch (map[ty][tx]) {
            case '#': kind = TK_BRICK; break;
            case '@': kind = TK_STEEL; break;
            case '~': kind = TK_WATER; break;
            case '%': kind = TK_TREE;  break;
            }
            fill_cells(g, tx * 4, ty * 4, 4, 4, kind);
        }
    }
    /* Keep the spawn points and the player's start clear. */
    fill_cells(g, 0, 0, 4, 4, TK_EMPTY);
    fill_cells(g, 24, 0, 4, 4, TK_EMPTY);
    fill_cells(g, 48, 0, 4, 4, TK_EMPTY);
    fill_cells(g, PLAYER_X / 8, PLAYER_Y / 8, 4, 4, TK_EMPTY);
    /* The base sits in a two-cell-thick brick fort, open ground around it. */
    fill_cells(g, 20, 44, 12, 8, TK_EMPTY);
    fill_cells(g, 22, 46, 8, 6, TK_BRICK);
    fill_cells(g, 24, 48, 4, 4, TK_EMPTY);
}

static void place_player(tk_game *g)
{
    tk_tank *p = &g->player;
    memset(p, 0, sizeof *p);
    p->state = TK_ACTIVE;
    p->x = PLAYER_X;
    p->y = PLAYER_Y;
    p->dir = TK_UP;
    p->speed = 28;
    g->shield = 180;
    g->respawn = 0;
}

void tk_start_stage(tk_game *g, int stage)
{
    /* Enemy mix by stage: basic, fast, power, armor. */
    static const int MIX[5][4] = {
        { 14, 4, 2, 0 }, { 10, 6, 2, 2 }, { 8, 6, 3, 3 }, { 6, 6, 4, 4 }, { 4, 6, 5, 5 },
    };
    const int *mix = MIX[stage > 5 ? 4 : stage - 1];
    int n = 0;

    g->stage = stage;
    load_map(g, stage - 1);
    memset(g->enemy, 0, sizeof g->enemy);
    memset(g->bullet, 0, sizeof g->bullet);
    memset(g->boom, 0, sizeof g->boom);
    place_player(g);

    for (int kind = 0; kind < 4; kind++)
        for (int i = 0; i < mix[kind]; i++) g->queue[n++] = kind;
    for (int i = n - 1; i > 0; i--) {             /* Fisher-Yates shuffle */
        int j = rnd_below(g, i + 1), t = g->queue[i];
        g->queue[i] = g->queue[j];
        g->queue[j] = t;
    }
    g->spawned = g->killed = 0;
    g->spawn_timer = 60;
    g->spawn_slot = 0;
    g->freeze = 0;
    g->pickup = TK_PICK_NONE;
    g->base_alive = 1;
    g->state = TK_INTRO;
    g->state_timer = 120;
    g->events |= TK_EV_START;
}

static void new_game(tk_game *g)
{
    g->score = 0;
    g->lives = 3;
    g->star = 0;
    tk_start_stage(g, 1);
}

void tk_init(tk_game *g, uint32_t seed)
{
    memset(g, 0, sizeof *g);
    g->rng = seed ? seed : 0x9E3779B9u;
    load_map(g, 0);
    g->base_alive = 1;
    g->state = TK_TITLE;
}

static void game_over(tk_game *g)
{
    if (g->state == TK_OVER) return;
    g->state = TK_OVER;
    g->state_timer = 0;
    if (g->score > g->hiscore) g->hiscore = g->score;
    g->events |= TK_EV_GAMEOVER;
}

/* --------------------------------------------------------------- tanks -- */

/* Other tanks block a move unless the two already overlap (fresh spawns). */
static int tank_in_way(const tk_game *g, const tk_tank *t, int nx, int ny)
{
    const tk_tank *p = &g->player;
    if (t != p && p->state == TK_ACTIVE &&
        overlap(nx, ny, 32, 32, p->x, p->y, 32, 32) && !overlap(t->x, t->y, 32, 32, p->x, p->y, 32, 32))
        return 1;
    for (int i = 0; i < TK_MAX_ENEMIES; i++) {
        const tk_tank *o = &g->enemy[i];
        if (o == t || o->state != TK_ACTIVE) continue;
        if (overlap(nx, ny, 32, 32, o->x, o->y, 32, 32) && !overlap(t->x, t->y, 32, 32, o->x, o->y, 32, 32))
            return 1;
    }
    return 0;
}

/* Face `dir`, snapping to the 16 px grid on a turn so corridors line up. */
static void turn(const tk_game *g, tk_tank *t, int dir)
{
    if (dir == t->dir) return;
    if ((dir ^ t->dir) & 1) {
        int nx = t->x, ny = t->y;
        if (dir == TK_UP || dir == TK_DOWN) nx = (t->x + 8) / 16 * 16;
        else ny = (t->y + 8) / 16 * 16;
        if (!tk_solid_at(g, nx, ny) && !tank_in_way(g, t, nx, ny)) { t->x = nx; t->y = ny; }
    }
    t->dir = dir;
}

/* Advance along the facing direction. Returns 0 if it could not move at all. */
static int advance(const tk_game *g, tk_tank *t)
{
    int moved = 0;
    t->sub += t->speed;
    while (t->sub >= 16) {
        int nx = t->x + DX[t->dir], ny = t->y + DY[t->dir];
        t->sub -= 16;
        if (tk_solid_at(g, nx, ny) || tank_in_way(g, t, nx, ny)) { t->sub = 0; break; }
        t->x = nx;
        t->y = ny;
        moved = 1;
    }
    if (moved) t->tread++;
    return moved;
}

static int bullets_of(const tk_game *g, int owner)
{
    int n = 0;
    for (int i = 0; i < TK_MAX_BULLETS; i++) n += g->bullet[i].alive && g->bullet[i].owner == owner;
    return n;
}

static void fire(tk_game *g, const tk_tank *t, int owner, int speed, int power)
{
    for (int i = 0; i < TK_MAX_BULLETS; i++) {
        tk_bullet *b = &g->bullet[i];
        if (b->alive) continue;
        b->alive = 1;
        b->x = t->x + 12 + DX[t->dir] * 12;
        b->y = t->y + 12 + DY[t->dir] * 12;
        b->dir = t->dir;
        b->speed = speed;
        b->owner = owner;
        b->power = power;
        if (owner < 0) g->events |= TK_EV_SHOT;
        return;
    }
}

static void update_player(tk_game *g, const tk_input *in)
{
    tk_tank *p = &g->player;

    if (p->state != TK_ACTIVE) {
        if (g->respawn > 0 && --g->respawn == 0) place_player(g);
        return;
    }
    if (g->shield > 0) g->shield--;
    if (p->cooldown > 0) p->cooldown--;

    if (in->dir >= 0) {
        turn(g, p, in->dir);
        advance(g, p);
    }
    if (in->fire && p->cooldown == 0 && bullets_of(g, -1) < (g->star >= 2 ? 2 : 1)) {
        fire(g, p, -1, g->star >= 1 ? 9 : 6, g->star >= 3);
        p->cooldown = 12;
    }

    if (g->pickup && overlap(p->x, p->y, 32, 32, g->pickup_x, g->pickup_y, 32, 32)) {
        switch (g->pickup) {
        case TK_PICK_SHIELD: g->shield = 600; break;
        case TK_PICK_LIFE:   g->lives++; break;
        case TK_PICK_FREEZE: g->freeze = 480; break;
        case TK_PICK_STAR:   if (g->star < 3) g->star++; break;
        case TK_PICK_BOMB:
            for (int i = 0; i < TK_MAX_ENEMIES; i++) {
                tk_tank *e = &g->enemy[i];
                if (e->state != TK_ACTIVE) continue;
                add_boom(g, e->x + 16, e->y + 16, 1);
                e->state = TK_GONE;
                g->killed++;
            }
            g->events |= TK_EV_EXPLODE;
            break;
        }
        g->score += 500;
        g->pickup = TK_PICK_NONE;
        g->events |= TK_EV_PICKUP;
    }
}

/* Pick a new heading: usually towards the base, sometimes at random. */
static void rethink(tk_game *g, tk_tank *e)
{
    int dir;
    if (rnd_below(g, 100) < 55) {
        int dx = BASE_X - e->x, dy = BASE_Y - e->y;
        if (dy > 0 && (dx == 0 || rnd_below(g, 2))) dir = TK_DOWN;
        else if (dx != 0) dir = dx > 0 ? TK_RIGHT : TK_LEFT;
        else dir = TK_DOWN;
    } else {
        dir = rnd_below(g, 4);
    }
    turn(g, e, dir);
    e->timer = 40 + rnd_below(g, 160);
    e->blocked = 0;
}

static void update_enemies(tk_game *g)
{
    int on_field = 0;

    for (int i = 0; i < TK_MAX_ENEMIES; i++) {
        tk_tank *e = &g->enemy[i];
        if (e->state == TK_GONE) continue;
        on_field++;
        if (e->flash > 0) e->flash--;

        if (e->state == TK_SPAWNING) {
            /* Materialise once the countdown ends and nothing stands on the spot. */
            if (e->timer > 0) e->timer--;
            else {
                int clear = !(g->player.state == TK_ACTIVE &&
                              overlap(e->x, e->y, 32, 32, g->player.x, g->player.y, 32, 32));
                for (int j = 0; j < TK_MAX_ENEMIES && clear; j++)
                    if (j != i && g->enemy[j].state == TK_ACTIVE &&
                        overlap(e->x, e->y, 32, 32, g->enemy[j].x, g->enemy[j].y, 32, 32)) clear = 0;
                if (clear) { e->state = TK_ACTIVE; e->timer = 30 + rnd_below(g, 60); }
            }
            continue;
        }
        if (g->freeze > 0) continue;

        if (e->cooldown > 0) e->cooldown--;
        if (e->timer > 0) e->timer--;
        if (!advance(g, e)) e->blocked++;
        else e->blocked = 0;
        if (e->timer == 0 || e->blocked > 6 + rnd_below(g, 20)) rethink(g, e);

        if (e->cooldown == 0 && bullets_of(g, i) == 0 && rnd_below(g, 32) == 0) {
            fire(g, e, i, KIND[e->kind].bullet, 0);
            e->cooldown = 30;
        }
    }
    if (g->freeze > 0) g->freeze--;

    /* Bring in the next enemy at one of the three gates along the top. */
    if (g->spawn_timer > 0) g->spawn_timer--;
    if (g->spawned < TK_STAGE_ENEMIES && on_field < MAX_ON_FIELD && g->spawn_timer == 0) {
        for (int i = 0; i < TK_MAX_ENEMIES; i++) {
            tk_tank *e = &g->enemy[i];
            if (e->state != TK_GONE) continue;
            memset(e, 0, sizeof *e);
            e->state = TK_SPAWNING;
            e->x = g->spawn_slot * 192;
            e->y = 0;
            e->dir = TK_DOWN;
            e->kind = g->queue[g->spawned];
            e->hp = KIND[e->kind].hp;
            e->speed = KIND[e->kind].speed;
            e->bonus = g->spawned == 3 || g->spawned == 10 || g->spawned == 17;
            e->timer = 50;
            g->spawn_slot = (g->spawn_slot + 1) % 3;
            g->spawned++;
            g->spawn_timer = g->stage >= 4 ? 100 : 140;
            break;
        }
    }
}

static void drop_pickup(tk_game *g)
{
    static const int KINDS[6] = {
        TK_PICK_SHIELD, TK_PICK_LIFE, TK_PICK_FREEZE, TK_PICK_BOMB, TK_PICK_STAR, TK_PICK_STAR,
    };
    g->pickup = KINDS[rnd_below(g, 6)];
    g->pickup_x = rnd_below(g, 13) * 32;
    g->pickup_y = (1 + rnd_below(g, 10)) * 32;
    g->pickup_timer = 900;
}

static void kill_player(tk_game *g)
{
    tk_tank *p = &g->player;
    if (g->invincible) return;
    add_boom(g, p->x + 16, p->y + 16, 1);
    p->state = TK_GONE;
    g->star = 0;
    g->events |= TK_EV_DIE;
    if (--g->lives <= 0) { g->lives = 0; game_over(g); }
    else g->respawn = 70;
}

/* ------------------------------------------------------------- bullets -- */

/*
 * A bullet meets terrain at its leading edge. Bricks along that row or
 * column give way across the width of a tank; steel stops everything except
 * a fully upgraded shell. Returns 1 if the bullet was stopped.
 */
static int hit_terrain(tk_game *g, const tk_bullet *b)
{
    int vertical = b->dir == TK_UP || b->dir == TK_DOWN;
    int lead = vertical ? (b->dir == TK_UP ? b->y : b->y + 7) / 8
                        : (b->dir == TK_LEFT ? b->x : b->x + 7) / 8;
    int a0 = (vertical ? b->x : b->y) / 8, a1 = ((vertical ? b->x : b->y) + 7) / 8;
    int hit = 0;

    for (int a = a0; a <= a1; a++) {
        int c = vertical ? g->cell[lead][a] : g->cell[a][lead];
        if (c == TK_BRICK || c == TK_STEEL) hit = 1;
    }
    if (!hit) return 0;

    /* Clear a slab one cell deep and 32 px wide, centred on the bullet. */
    int centre = (vertical ? b->x : b->y) + 4;
    for (int a = (centre - 16) / 8; a <= (centre + 15) / 8; a++) {
        if (a < 0 || a >= TK_CELLS) continue;
        uint8_t *c = vertical ? &g->cell[lead][a] : &g->cell[a][lead];
        if (*c == TK_BRICK) { *c = TK_EMPTY; g->events |= TK_EV_BRICK; }
        else if (*c == TK_STEEL) {
            if (b->power) *c = TK_EMPTY;
            g->events |= TK_EV_STEEL;
        }
    }
    return 1;
}

static void update_bullets(tk_game *g)
{
    for (int i = 0; i < TK_MAX_BULLETS; i++) {
        tk_bullet *b = &g->bullet[i];
        if (!b->alive) continue;

        /* Move in steps no longer than half a cell so nothing is skipped. */
        for (int left = b->speed; left > 0 && b->alive; left -= 4) {
            int step = left < 4 ? left : 4;
            b->x += DX[b->dir] * step;
            b->y += DY[b->dir] * step;

            if (b->x < 0 || b->y < 0 || b->x > TK_FIELD - 8 || b->y > TK_FIELD - 8) {
                b->alive = 0;
                add_boom(g, b->x + 4, b->y + 4, 0);
                if (b->owner < 0) g->events |= TK_EV_STEEL;
                break;
            }
            if (hit_terrain(g, b)) {
                b->alive = 0;
                add_boom(g, b->x + 4, b->y + 4, 0);
                break;
            }
            if (g->base_alive && overlap(b->x, b->y, 8, 8, BASE_X, BASE_Y, 32, 32)) {
                b->alive = 0;
                add_boom(g, BASE_X + 16, BASE_Y + 16, 1);
                if (!g->invincible) {
                    g->base_alive = 0;
                    g->events |= TK_EV_EXPLODE;
                    game_over(g);
                }
                break;
            }

            /* Shells from opposite sides cancel out. */
            for (int j = 0; j < TK_MAX_BULLETS; j++) {
                tk_bullet *o = &g->bullet[j];
                if (j == i || !o->alive || (o->owner < 0) == (b->owner < 0)) continue;
                if (overlap(b->x, b->y, 8, 8, o->x, o->y, 8, 8)) { b->alive = o->alive = 0; break; }
            }
            if (!b->alive) break;

            if (b->owner < 0) {
                for (int j = 0; j < TK_MAX_ENEMIES; j++) {
                    tk_tank *e = &g->enemy[j];
                    if (e->state != TK_ACTIVE || !overlap(b->x, b->y, 8, 8, e->x, e->y, 32, 32)) continue;
                    b->alive = 0;
                    if (--e->hp > 0) {
                        e->flash = 8;
                        add_boom(g, b->x + 4, b->y + 4, 0);
                        g->events |= TK_EV_HIT;
                    } else {
                        add_boom(g, e->x + 16, e->y + 16, 1);
                        g->score += KIND[e->kind].score;
                        if (e->bonus) drop_pickup(g);
                        e->state = TK_GONE;
                        g->killed++;
                        g->events |= TK_EV_EXPLODE;
                    }
                    break;
                }
            } else {
                tk_tank *p = &g->player;
                if (p->state == TK_ACTIVE && overlap(b->x, b->y, 8, 8, p->x, p->y, 32, 32)) {
                    b->alive = 0;
                    if (g->shield > 0) add_boom(g, b->x + 4, b->y + 4, 0);
                    else kill_player(g);
                }
            }
        }
    }
}

/* -------------------------------------------------------------- update -- */

static void update_effects(tk_game *g)
{
    for (int i = 0; i < TK_MAX_BOOMS; i++) {
        tk_boom *b = &g->boom[i];
        if (b->alive && ++b->age >= (b->big ? 30 : 10)) b->alive = 0;
    }
}

void tk_update(tk_game *g, const tk_input *in)
{
    g->events = 0;
    g->frame++;

    switch (g->state) {
    case TK_TITLE:
        if (in->start) new_game(g);
        break;

    case TK_INTRO:
        if (--g->state_timer <= 0) g->state = TK_PLAY;
        break;

    case TK_PAUSE:
        if (in->pause || in->start) g->state = TK_PLAY;
        break;

    case TK_PLAY:
        if (in->pause) { g->state = TK_PAUSE; break; }
        update_player(g, in);
        update_enemies(g);
        update_bullets(g);
        update_effects(g);
        if (g->pickup && --g->pickup_timer <= 0) g->pickup = TK_PICK_NONE;
        if (g->state == TK_PLAY && g->killed >= TK_STAGE_ENEMIES) {
            g->state = TK_CLEAR;
            g->state_timer = 180;
            g->events |= TK_EV_CLEAR;
        }
        break;

    case TK_CLEAR:
        update_effects(g);
        if (--g->state_timer <= 0) tk_start_stage(g, g->stage + 1);
        break;

    case TK_OVER:
        /* The battle winds down behind the banner; then wait for a key. */
        g->state_timer++;
        update_enemies(g);
        update_bullets(g);
        update_effects(g);
        g->state = TK_OVER;
        if (g->state_timer > 120 && in->start) {
            int hi = g->hiscore;
            tk_init(g, g->rng);
            g->hiscore = hi;
        }
        break;
    }
}
