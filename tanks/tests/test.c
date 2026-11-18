/*
 * test.c - Tank Defender tests. Runs natively on any platform:
 *
 *     make test                 run the checks
 *     build/test shots DIR      also write sample frames to DIR as .ppm
 *
 * The game is a pure function of its seed and inputs, so a scripted player
 * can run hundreds of thousands of ticks while the rules are checked on
 * every one of them.
 */
#include "../src/game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0, checks = 0;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { if (failures++ < 20) printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

/* Keyboard-style input for player 1: a direction, fire held, start and pause presses. */
static tk_input keys(int dir, int fire, int start, int pause)
{
    tk_input in = { { { dir, -1, 0, fire, 0 }, { -1, -1, 0, 0, 0 } }, start, pause, -1 };
    return in;
}

static tk_input menu(int dir)
{
    tk_input in = keys(-1, 0, 0, 0);
    in.menu = dir;
    return in;
}

static uint32_t bot_rng = 1;
static uint32_t bot_rand(void)
{
    bot_rng ^= bot_rng << 13; bot_rng ^= bot_rng >> 17; bot_rng ^= bot_rng << 5;
    return bot_rng;
}

/* A crude player: wander, change heading now and then, keep firing. */
static tk_input bot_input(const tk_game *g, int *dir)
{
    if (bot_rand() % 40 == 0) *dir = (int)(bot_rand() % 5) - 1;
    return keys(*dir, 1, (g->state == TK_TITLE || g->state == TK_OVER) && (g->frame % 20) == 0, 0);
}

static int iabs_t(int v) { return v < 0 ? -v : v; }

static int box_hit(int ax, int ay, int bx, int by)
{
    return ax < bx + 32 && bx < ax + 32 && ay < by + 32 && by < ay + 32;
}

/* Rules that must hold on every tick of play. */
static void check_world(const tk_game *g)
{
    CHECK(g->state >= TK_TITLE && g->state <= TK_OVER);
    CHECK(g->lives >= 0 && g->score >= 0);
    CHECK(g->killed <= g->spawned && g->spawned <= TK_STAGE_ENEMIES);
    CHECK(g->star >= 0 && g->star <= 3);
    if (g->state != TK_PLAY) return;

    if (g->player.state == TK_ACTIVE) CHECK(!tk_solid_at(g, g->player.x, g->player.y));
    if (g->ally.state == TK_ACTIVE) CHECK(g->players == 2 && !tk_solid_at(g, g->ally.x, g->ally.y));
    CHECK(g->ally_lives >= 0 && g->ally_lives < 100);
    int alive = 0;
    for (int i = 0; i < TK_MAX_ENEMIES; i++) {
        const tk_tank *e = &g->enemy[i];
        if (e->state == TK_GONE) continue;
        alive++;
        CHECK(!tk_solid_at(g, e->x, e->y));              /* never inside a wall or off the field */
        CHECK(e->hp >= 1 && e->kind >= TK_BASIC && e->kind <= TK_ARMOR);
        CHECK(e->dir >= TK_UP && e->dir <= TK_LEFT);
    }
    CHECK(alive <= 4);
    CHECK(alive + g->killed == g->spawned);
    for (int i = 0; i < TK_MAX_BULLETS; i++) {
        const tk_bullet *b = &g->bullet[i];
        if (b->alive) CHECK(b->x >= 0 && b->y >= 0 && b->x <= TK_FIELD - 8 && b->y <= TK_FIELD - 8);
    }
}

static void test_maps(void)
{
    for (int m = 0; m < TK_NUM_MAPS; m++) {
        const char *const *map = tk_map(m);
        int bricks = 0;
        for (int row = 0; row < TK_TILES; row++) {
            CHECK(strlen(map[row]) == TK_TILES);
            for (int col = 0; col < TK_TILES; col++) {
                CHECK(strchr(".#@~%", map[row][col]) != NULL);
                bricks += map[row][col] == '#';
            }
        }
        CHECK(bricks > 10);
        CHECK(strcmp(map[0], ".............") == 0);     /* spawn row stays open */

        tk_game g;
        tk_init(&g, 7);
        tk_start_stage(&g, m + 1);
        CHECK(!tk_solid_at(&g, 0, 0) && !tk_solid_at(&g, 192, 0) && !tk_solid_at(&g, 384, 0));
        CHECK(!tk_solid_at(&g, g.player.x, g.player.y));
        CHECK(tk_solid_at(&g, 192, 384));                /* the base */
        CHECK(g.cell[46][22] == TK_BRICK && g.cell[51][29] == TK_BRICK);   /* its fort */
    }
}

static void test_shooting(void)
{
    tk_game g;
    tk_input fire = keys(-1, 1, 0, 0), idle = keys(-1, 0, 0, 0);
    tk_init(&g, 3);
    tk_start_stage(&g, 1);
    g.state = TK_PLAY;
    g.spawn_timer = 100000;                              /* keep enemies out of this test */
    memset(g.cell, TK_EMPTY, sizeof g.cell);
    for (int x = 0; x < TK_CELLS; x++) { g.cell[20][x] = TK_BRICK; g.cell[19][x] = TK_BRICK; g.cell[10][x] = TK_STEEL; }

    /* One shell clears a slab one cell deep and a tank wide. */
    tk_update(&g, &fire);
    for (int i = 0; i < 120; i++) tk_update(&g, &idle);
    int gone20 = 0, gone19 = 0;
    for (int x = 0; x < TK_CELLS; x++) { gone20 += g.cell[20][x] == TK_EMPTY; gone19 += g.cell[19][x] == TK_EMPTY; }
    CHECK(gone20 == 4 && gone19 == 0);
    CHECK(g.cell[20][16] == TK_EMPTY && g.cell[20][19] == TK_EMPTY && g.cell[20][15] == TK_BRICK);

    /* A second and third: through the brick, then stopped by steel. */
    for (int shot = 0; shot < 2; shot++) {
        tk_update(&g, &fire);
        for (int i = 0; i < 120; i++) tk_update(&g, &idle);
    }
    gone19 = 0;
    for (int x = 0; x < TK_CELLS; x++) gone19 += g.cell[19][x] == TK_EMPTY;
    CHECK(gone19 == 4);
    for (int x = 0; x < TK_CELLS; x++) CHECK(g.cell[10][x] == TK_STEEL);

    /* A fully upgraded gun breaks steel. */
    g.star = 3;
    tk_update(&g, &fire);
    for (int i = 0; i < 120; i++) tk_update(&g, &idle);
    CHECK(g.cell[10][16] == TK_EMPTY && g.cell[10][15] == TK_STEEL);

    /* Walls stop tanks: drive up into the brick row and stay below it. */
    tk_input up = keys(TK_UP, 0, 0, 0);
    for (int x = 0; x < TK_CELLS; x++) g.cell[30][x] = TK_BRICK;
    for (int i = 0; i < 600; i++) tk_update(&g, &up);
    CHECK(g.player.y == 31 * 8);
    CHECK(g.player.x == 128);
}

/* Play many games through to game over and back to the title. */
static void test_full_games(void)
{
    int games = 0, best_stage = 0, dir = -1;
    long ticks = 0;
    tk_game g;
    tk_init(&g, 2024);
    while (games < 30 && ticks < 3000000) {
        int was = g.state;
        tk_input in = bot_input(&g, &dir);
        tk_update(&g, &in);
        ticks++;
        check_world(&g);
        if (g.stage > best_stage) best_stage = g.stage;
        if (was != TK_OVER && g.state == TK_OVER) {
            games++;
            CHECK(g.lives == 0 || !g.base_alive);
            CHECK(g.hiscore >= g.score);
        }
    }
    printf("  %d games to game over in %ld ticks; furthest stage %d, best score %d\n",
           games, ticks, best_stage, g.hiscore);
    CHECK(games == 30);
}

/* With the test hook on, the bot must clear stage after stage. */
static void test_stage_progress(void)
{
    int dir = -1;
    long ticks = 0;
    tk_game g;
    tk_input start = keys(-1, 0, 1, 0);
    tk_init(&g, 99);
    g.invincible = 1;
    tk_update(&g, &start);
    while (g.stage < 8 && ticks < 2000000) {
        tk_input in = bot_input(&g, &dir);
        tk_update(&g, &in);
        ticks++;
        check_world(&g);
        CHECK(g.state != TK_OVER);
        /* Two tanks may only overlap where one of them has just appeared. */
        for (int i = 0; i < TK_MAX_ENEMIES && g.state == TK_PLAY; i++) {
            const tk_tank *e = &g.enemy[i];
            if (e->state == TK_ACTIVE && g.player.state == TK_ACTIVE && e->y > 40 && g.player.y < 340)
                CHECK(!box_hit(e->x, e->y, g.player.x, g.player.y));
        }
    }
    printf("  reached stage %d in %ld ticks (%.1f minutes of play), score %d\n",
           g.stage, ticks, (double)ticks / 3600.0, g.score);
    CHECK(g.stage == 8);
}

/* Two-player mode: the computer ally must pull its weight and never hurt its own side. */
static void test_ally(void)
{
    tk_input idle = keys(-1, 0, 0, 0), down = menu(TK_DOWN), up = menu(TK_UP), start = keys(-1, 0, 1, 0);
    tk_game g;

    /* The title menu: down picks two players, up goes back to one. */
    tk_init(&g, 11);
    CHECK(g.players == 1);
    tk_update(&g, &down);
    CHECK(g.players == 2);
    tk_update(&g, &idle);
    tk_update(&g, &up);
    CHECK(g.players == 1);
    tk_update(&g, &start);
    CHECK(g.lives == TK_LIVES && g.ally_lives == 0 && g.ally.state == TK_GONE);

    /* With the human standing still, the ally alone clears stage after stage. */
    tk_init(&g, 12);
    tk_update(&g, &down);
    tk_update(&g, &start);
    CHECK(g.players == 2 && g.lives == TK_LIVES && g.ally_lives == TK_LIVES);
    g.invincible = 1;
    long ticks = 0;
    while (g.stage < 6 && ticks < 1500000) {
        tk_update(&g, &idle);
        ticks++;
        check_world(&g);
        if (g.state == TK_PLAY && g.ally.state == TK_ACTIVE && g.player.state == TK_ACTIVE)
            CHECK(!box_hit(g.ally.x, g.ally.y, g.player.x, g.player.y) || g.ally.y > 340);
    }
    printf("  ally alone cleared 5 stages in %ld ticks (%.1f minutes of play)\n", ticks, (double)ticks / 3600.0);
    CHECK(g.stage == 6);

    /* Real games with an idle human: however they end, the ally never shoots the base. */
    int by_ally = 0, games = 0, ally_lost = 0;
    for (uint32_t seed = 1; seed <= 60; seed++) {
        tk_init(&g, seed * 7919u);
        tk_update(&g, &down);
        tk_update(&g, &start);
        for (long t = 0; t < 400000 && g.state != TK_OVER; t++) { tk_update(&g, &idle); check_world(&g); }
        if (g.state != TK_OVER) continue;
        games++;
        if (!g.base_alive && g.base_killer == TK_OWNER_ALLY) by_ally++;
        if (g.ally_lives < TK_LIVES) ally_lost++;
    }
    printf("  %d unattended 2-player games; base shot by the ally in %d; ally lost tanks in %d\n", games, by_ally, ally_lost);
    CHECK(games == 60 && by_ally == 0);
    CHECK(ally_lost > 0);                                 /* the ally is not immune */
}

/* Count the player's shots over a run of identical ticks. */
static int shots_during(tk_game *g, tk_input in, int ticks)
{
    int n = 0;
    for (int i = 0; i < ticks; i++) { tk_update(g, &in); n += (g->events & TK_EV_SHOT) != 0; }
    return n;
}

/* Gamepad controls: aim without driving, drive straight ahead, tap and hold to fire. */
static void test_controls(void)
{
    tk_game g;
    tk_input idle = keys(-1, 0, 0, 0), in;
    tk_init(&g, 5);
    tk_start_stage(&g, 1);
    g.state = TK_PLAY;
    g.invincible = 1;
    g.spawn_timer = 1000000;                              /* no enemies in this test */
    memset(g.cell, TK_EMPTY, sizeof g.cell);
    g.player.x = 96; g.player.y = 200;

    /* Holding fire shoots exactly four times a second. */
    in = idle; in.p[0].fire = 1;
    CHECK(shots_during(&g, in, 120) == 2 * TK_FIRE_HZ);
    CHECK(shots_during(&g, idle, 60) == 0);

    /* Each press is one shot, however fast the presses come. */
    int taps = 0;
    for (int i = 0; i < 5; i++) {
        in = idle; in.p[0].shot = 1;
        taps += shots_during(&g, in, 1);
        taps += shots_during(&g, idle, 7);
    }
    CHECK(taps == 5);
    shots_during(&g, idle, 200);

    /* A press while already holding still fires at once, then the rhythm restarts. */
    in = idle; in.p[0].fire = 1;
    CHECK(shots_during(&g, in, 8) == 1);
    in.p[0].shot = 1;
    CHECK(shots_during(&g, in, 1) == 1);
    in.p[0].shot = 0;
    CHECK(shots_during(&g, in, 14) == 0);
    CHECK(shots_during(&g, in, 1) == 1);
    shots_during(&g, idle, 200);

    /* The d-pad turns the tank on the spot. */
    for (int dir = TK_UP; dir <= TK_LEFT; dir++) {
        int x = g.player.x, y = g.player.y;
        in = idle; in.p[0].aim = dir;
        tk_update(&g, &in);
        CHECK(g.player.dir == dir);
        CHECK(iabs_t(g.player.x - x) <= 8 && iabs_t(g.player.y - y) <= 8);   /* only the grid snap of a turn */
        x = g.player.x; y = g.player.y;
        for (int i = 0; i < 30; i++) tk_update(&g, &in);
        CHECK(g.player.x == x && g.player.y == y);
    }

    /* The stick drives along the heading, whatever the heading is. */
    static const int DXT[4] = { 0, 1, 0, -1 }, DYT[4] = { -1, 0, 1, 0 };
    for (int dir = TK_UP; dir <= TK_LEFT; dir++) {
        in = idle; in.p[0].aim = dir;
        tk_update(&g, &in);
        int x = g.player.x, y = g.player.y;
        in = idle; in.p[0].forward = 1;
        for (int i = 0; i < 32; i++) tk_update(&g, &in);
        CHECK(g.player.dir == dir);
        CHECK(g.player.x == x + DXT[dir] * 56 && g.player.y == y + DYT[dir] * 56);
    }
}

/* Two humans: the second pad drives the green tank; play goes on until both are out. */
static void test_two_humans(void)
{
    tk_game g;
    tk_input idle = keys(-1, 0, 0, 0), start = keys(-1, 0, 1, 0), down = menu(TK_DOWN), in;
    tk_init(&g, 21);
    tk_update(&g, &down);
    tk_update(&g, &down);
    tk_update(&g, &down);                                 /* the menu stops at the last entry */
    CHECK(g.mode == TK_MODE_DUO && g.players == 2);
    tk_update(&g, &start);
    CHECK(g.lives == TK_LIVES && g.ally_lives == TK_LIVES);
    while (g.state == TK_INTRO) tk_update(&g, &idle);
    g.spawn_timer = 1000000;

    /* Nobody touching pad 2: the green tank stays where it is (no computer driving it). */
    int ax = g.ally.x, ay = g.ally.y;
    for (int i = 0; i < 300; i++) tk_update(&g, &idle);
    CHECK(g.ally.state == TK_ACTIVE && g.ally.x == ax && g.ally.y == ay);

    /* Pad 2 drives and shoots the green tank only. */
    int px = g.player.x, py = g.player.y;
    in = idle; in.p[1].forward = 1; in.p[1].fire = 1;
    int green = 0, yellow = 0;
    for (int t = 0; t < 40; t++) {
        tk_update(&g, &in);
        for (int i = 0; i < TK_MAX_BULLETS; i++) {
            green += g.bullet[i].alive && g.bullet[i].owner == TK_OWNER_ALLY;
            yellow += g.bullet[i].alive && g.bullet[i].owner == TK_OWNER_PLAYER;
        }
    }
    CHECK(g.ally.y < ay && g.ally.x == ax);
    CHECK(g.player.x == px && g.player.y == py);
    CHECK(green >= 1 && yellow == 0);

    /* Player 1 loses the last tank: the game continues for player 2. */
    g.lives = 1; g.shield = 0;
    g.bullet[0] = (tk_bullet){ 1, g.player.x + 12, g.player.y + 4, TK_DOWN, 4, 0, 0 };   /* an enemy shell */
    for (int i = 0; i < 20; i++) tk_update(&g, &idle);
    CHECK(g.lives == 0 && g.player.state == TK_GONE && g.state == TK_PLAY);
    for (int i = 0; i < 200; i++) tk_update(&g, &idle);
    CHECK(g.player.state == TK_GONE);                     /* and does not come back */

    /* Player 2 loses the last tank as well: now it is over. */
    g.ally_lives = 1; g.ally_shield = 0;
    g.bullet[0] = (tk_bullet){ 1, g.ally.x + 12, g.ally.y + 4, TK_DOWN, 4, 0, 0 };
    for (int i = 0; i < 20 && g.state == TK_PLAY; i++) tk_update(&g, &idle);
    CHECK(g.ally_lives == 0 && g.state == TK_OVER);

    /* Long random play on both pads keeps every rule intact. */
    tk_init(&g, 22);
    tk_update(&g, &down); tk_update(&g, &down); tk_update(&g, &start);
    g.invincible = 1;
    long ticks = 0;
    int aim = -1;
    bot_rng = 9;
    while (g.stage < 4 && ticks < 1500000) {
        in = idle;
        if (bot_rand() % 30 == 0) aim = (int)(bot_rand() % 4);
        in.p[0].aim = aim; in.p[0].forward = bot_rand() % 4 != 0; in.p[0].fire = 1;
        in.p[1].aim = (aim + 1) & 3; in.p[1].forward = bot_rand() % 3 != 0; in.p[1].shot = bot_rand() % 20 == 0;
        tk_update(&g, &in);
        ticks++;
        check_world(&g);
    }
    printf("  two gamepad bots cleared 3 stages in %ld ticks\n", ticks);
    CHECK(g.stage == 4);
}

static void test_determinism(void)
{
    tk_game a, b;
    int da = -1, db = -1;
    tk_init(&a, 555);
    tk_init(&b, 555);
    bot_rng = 77;
    for (int i = 0; i < 20000; i++) { tk_input in = bot_input(&a, &da); tk_update(&a, &in); }
    bot_rng = 77;
    for (int i = 0; i < 20000; i++) { tk_input in = bot_input(&b, &db); tk_update(&b, &in); }
    CHECK(memcmp(&a, &b, sizeof a) == 0);                /* same seed and inputs, same world */
}

static void save_ppm(const char *dir, const char *name, const tk_game *g)
{
    static uint32_t fb[TK_FB_W * TK_FB_H];
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { printf("  cannot write %s\n", path); return; }
    tk_render(g, fb);
    fprintf(f, "P6\n%d %d\n255\n", TK_FB_W, TK_FB_H);
    for (int i = 0; i < TK_FB_W * TK_FB_H; i++) {
        unsigned char px[3] = { (unsigned char)(fb[i] >> 16), (unsigned char)(fb[i] >> 8), (unsigned char)fb[i] };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

static void write_shots(const char *dir)
{
    tk_game g;
    tk_input start = keys(-1, 0, 1, 0), pause = keys(-1, 0, 0, 1);
    int d = -1;
    char name[32];

    tk_init(&g, 4242);
    save_ppm(dir, "1-title", &g);
    tk_update(&g, &start);
    save_ppm(dir, "2-intro", &g);
    bot_rng = 5;
    for (int stage = 1; stage <= TK_NUM_MAPS; stage++) {
        tk_start_stage(&g, stage);
        g.invincible = 1;
        for (int i = 0; i < 1500; i++) { tk_input in = bot_input(&g, &d); tk_update(&g, &in); }
        snprintf(name, sizeof name, "3-stage%d", stage);
        save_ppm(dir, name, &g);
    }
    g.star = 2;
    g.pickup = TK_PICK_STAR; g.pickup_x = 96; g.pickup_y = 160; g.pickup_timer = 500;
    g.freeze = 100;
    for (int i = 0; i < 31; i++) { tk_input in = bot_input(&g, &d); tk_update(&g, &in); }
    save_ppm(dir, "4-pickup", &g);
    tk_update(&g, &pause);
    save_ppm(dir, "5-paused", &g);
    tk_update(&g, &pause);
    g.invincible = 0;
    g.lives = 1;
    g.shield = 0;
    for (int i = 0; i < 20000 && g.state != TK_OVER; i++) { tk_input in = keys(-1, 0, 0, 0); tk_update(&g, &in); }
    for (int i = 0; i < 130; i++) { tk_input in = keys(-1, 0, 0, 0); tk_update(&g, &in); }
    save_ppm(dir, "6-gameover", &g);

    /* two-player mode */
    tk_input down = menu(TK_DOWN);
    tk_init(&g, 777);
    tk_update(&g, &down);
    save_ppm(dir, "7-title-2p", &g);
    tk_update(&g, &start);
    g.invincible = 1;
    for (int i = 0; i < 1400; i++) { tk_input in = bot_input(&g, &d); in.p[0].fire = 0; tk_update(&g, &in); }
    save_ppm(dir, "8-ally", &g);
    tk_init(&g, 778);
    g.pads = 2; g.pad_primary = 1;
    tk_update(&g, &down); tk_update(&g, &down);
    save_ppm(dir, "9-title-pads", &g);
    printf("  frames written to %s\n", dir);
}

int main(int argc, char **argv)
{
    printf("maps\n");           test_maps();
    printf("shooting\n");       test_shooting();
    printf("determinism\n");    test_determinism();
    printf("full games\n");     test_full_games();
    printf("stage progress\n"); test_stage_progress();
    printf("ally\n");           test_ally();
    printf("controls\n");       test_controls();
    printf("two humans\n");     test_two_humans();
    if (argc >= 3 && strcmp(argv[1], "shots") == 0) { printf("shots\n"); write_shots(argv[2]); }

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
