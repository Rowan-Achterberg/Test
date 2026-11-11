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

static uint32_t bot_rng = 1;
static uint32_t bot_rand(void)
{
    bot_rng ^= bot_rng << 13; bot_rng ^= bot_rng >> 17; bot_rng ^= bot_rng << 5;
    return bot_rng;
}

/* A crude player: wander, change heading now and then, keep firing. */
static tk_input bot_input(const tk_game *g, int *dir)
{
    tk_input in = { *dir, 1, 0, 0 };
    if (g->state == TK_TITLE || g->state == TK_OVER) in.start = (g->frame % 20) == 0;
    if (bot_rand() % 40 == 0) *dir = (int)(bot_rand() % 5) - 1;
    in.dir = *dir;
    return in;
}

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
    tk_input fire = { -1, 1, 0, 0 }, idle = { -1, 0, 0, 0 };
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
    tk_input up = { TK_UP, 0, 0, 0 };
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
    tk_input start = { -1, 0, 1, 0 };
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
    tk_input start = { -1, 0, 1, 0 }, pause = { -1, 0, 0, 1 };
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
    for (int i = 0; i < 20000 && g.state != TK_OVER; i++) { tk_input in = { -1, 0, 0, 0 }; tk_update(&g, &in); }
    for (int i = 0; i < 130; i++) { tk_input in = { -1, 0, 0, 0 }; tk_update(&g, &in); }
    save_ppm(dir, "6-gameover", &g);
    printf("  frames written to %s\n", dir);
}

int main(int argc, char **argv)
{
    printf("maps\n");           test_maps();
    printf("shooting\n");       test_shooting();
    printf("determinism\n");    test_determinism();
    printf("full games\n");     test_full_games();
    printf("stage progress\n"); test_stage_progress();
    if (argc >= 3 && strcmp(argv[1], "shots") == 0) { printf("shots\n"); write_shots(argv[2]); }

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
