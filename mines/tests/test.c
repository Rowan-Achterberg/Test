/*
 * test.c - Engine and solver tests. Runs natively on any platform:
 *
 *     make test
 *
 * The solver is checked two ways: against a brute-force enumeration of every
 * mine layout on small boards, and by letting it play thousands of full games
 * while asserting that a cell it calls "proven safe" is never a mine.
 */
#include "../src/core.h"
#include "../src/solver.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0, checks = 0;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { failures++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static int count_mines(const ms_game *g)
{
    int n = 0;
    for (int i = 0; i < g->w * g->h; i++) n += (g->cell[i] & MS_MINE) != 0;
    return n;
}

static void test_rng(void)
{
    ms_rng a, b;
    ms_rng_seed(&a, 42);
    ms_rng_seed(&b, 42);
    for (int i = 0; i < 1000; i++) CHECK(ms_rng_next(&a) == ms_rng_next(&b));

    int hist[7] = {0};
    for (int i = 0; i < 70000; i++) {
        uint32_t v = ms_rng_below(&a, 7);
        CHECK(v < 7);
        hist[v]++;
    }
    for (int i = 0; i < 7; i++) CHECK(hist[i] > 9500 && hist[i] < 10500);
}

static void test_deal(void)
{
    static const int cfg[][3] = { {9, 9, 10}, {16, 16, 40}, {30, 16, 99}, {30, 24, 200}, {4, 4, 15} };
    for (size_t c = 0; c < sizeof cfg / sizeof cfg[0]; c++) {
        for (uint32_t seed = 1; seed <= 200; seed++) {
            ms_game g;
            ms_init(&g, cfg[c][0], cfg[c][1], cfg[c][2], seed);
            int x = (int)(seed % (uint32_t)g.w), y = (int)((seed / 7) % (uint32_t)g.h);
            int idx = y * g.w + x, nb[8];
            int r = ms_reveal(&g, x, y);

            CHECK(r > 0);                               /* first probe never explodes */
            CHECK(count_mines(&g) == g.mines);
            CHECK(!(g.cell[idx] & MS_MINE));
            int k = ms_neighbors(&g, idx, nb);
            if (g.mines <= g.w * g.h - 1 - k) {
                CHECK((g.cell[idx] & MS_ADJ) == 0);     /* and opens a region */
                CHECK(r > 1);
            }
            for (int i = 0; i < g.w * g.h; i++) {       /* numbers match the mines */
                int adj = 0;
                k = ms_neighbors(&g, i, nb);
                for (int j = 0; j < k; j++) adj += (g.cell[nb[j]] & MS_MINE) != 0;
                CHECK((g.cell[i] & MS_ADJ) == adj);
            }
            CHECK(g.bbbv >= 1 && g.bbbv <= g.w * g.h - g.mines);

            ms_game h;                                  /* same inputs, same board */
            ms_init(&h, cfg[c][0], cfg[c][1], cfg[c][2], seed);
            ms_reveal(&h, x, y);
            CHECK(memcmp(g.cell, h.cell, sizeof g.cell) == 0);
        }
    }
}

static void test_rules(void)
{
    ms_game g;
    ms_init(&g, 9, 9, 10, 7);
    CHECK(ms_toggle_flag(&g, 0, 0) == 1 && g.flags == 1);
    CHECK(ms_reveal(&g, 0, 0) == 0);                    /* flagged cells do not open */
    CHECK(ms_toggle_flag(&g, 0, 0) == 1 && g.flags == 0);
    CHECK(ms_reveal(&g, 4, 4) > 0 && g.state == MS_RUNNING);
    CHECK(ms_reveal(&g, 4, 4) == 0);                    /* already open */
    CHECK(ms_toggle_flag(&g, 4, 4) == 0);
    CHECK(ms_reveal(&g, -1, 0) == 0 && ms_reveal(&g, 9, 0) == 0);

    /* Opening a mine loses and freezes the board. */
    for (int i = 0; i < 81; i++) {
        if (g.cell[i] & MS_MINE) {
            CHECK(ms_reveal(&g, i % 9, i / 9) == -1);
            CHECK(g.state == MS_LOST && g.boom == i);
            break;
        }
    }
    CHECK(ms_reveal(&g, 0, 0) == 0);

    /* Opening every safe cell wins, in exactly 3BV clicks if played optimally. */
    ms_init(&g, 16, 16, 40, 99);
    ms_reveal(&g, 8, 8);
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < 256; i++) {
            int zero = !(g.cell[i] & (MS_MINE | MS_ADJ));
            if (!(g.cell[i] & MS_MINE) && (pass == 1 || zero)) ms_reveal(&g, i % 16, i / 16);
        }
    CHECK(g.state == MS_WON);
    CHECK(g.flags == g.mines && g.opened == 256 - 40);
    CHECK(g.clicks == g.bbbv);

    /* Chording: flag the real mines around a number, then chord it. */
    ms_init(&g, 9, 9, 10, 1234);
    ms_reveal(&g, 4, 4);
    int done = 0;
    for (int i = 0; i < 81 && !done; i++) {
        int nb[8], hidden = 0;
        if (!(g.cell[i] & MS_OPEN) || !(g.cell[i] & MS_ADJ)) continue;
        int k = ms_neighbors(&g, i, nb);
        for (int j = 0; j < k; j++)
            if (!(g.cell[nb[j]] & (MS_OPEN | MS_MINE))) hidden++;
        if (!hidden) continue;
        CHECK(ms_chord(&g, i % 9, i / 9) == 0);         /* not enough flags yet */
        for (int j = 0; j < k; j++)
            if (g.cell[nb[j]] & MS_MINE) ms_toggle_flag(&g, nb[j] % 9, nb[j] / 9);
        CHECK(ms_chord(&g, i % 9, i / 9) >= hidden);
        CHECK(g.state != MS_LOST);
        done = 1;
    }
    CHECK(done);
}

/* Reference: enumerate every way to place the mines on the hidden cells. */
static int brute_force(const ms_game *g, double *p)
{
    int total = g->w * g->h, hid[MS_MAX_CELLS], n = 0, nb[8];
    double count[MS_MAX_CELLS] = {0}, layouts = 0;

    for (int i = 0; i < total; i++)
        if (!(g->cell[i] & MS_OPEN)) hid[n++] = i;
    if (n > 22) return 0;

    for (uint32_t mask = 0; mask < (1u << n); mask++) {
        int bits = 0, ok = 1;
        uint8_t mine[MS_MAX_CELLS] = {0};
        for (int j = 0; j < n; j++)
            if (mask >> j & 1) { mine[hid[j]] = 1; bits++; }
        if (bits != g->mines) continue;
        for (int i = 0; i < total && ok; i++) {
            if (!(g->cell[i] & MS_OPEN)) continue;
            int adj = 0, k = ms_neighbors(g, i, nb);
            for (int j = 0; j < k; j++) adj += mine[nb[j]];
            ok = adj == (g->cell[i] & MS_ADJ);
        }
        if (!ok) continue;
        layouts++;
        for (int j = 0; j < n; j++)
            if (mask >> j & 1) count[hid[j]]++;
    }
    for (int j = 0; j < n; j++) p[hid[j]] = count[hid[j]] / layouts;
    return 1;
}

static void test_solver_exact(void)
{
    static const int cfg[][3] = { {5, 5, 4}, {6, 5, 6}, {6, 6, 8}, {7, 5, 5}, {8, 4, 7} };
    int compared = 0;
    ms_analysis *an = malloc(sizeof *an);

    for (size_t c = 0; c < sizeof cfg / sizeof cfg[0]; c++) {
        for (uint32_t seed = 1; seed <= 60; seed++) {
            ms_game g;
            ms_rng rng;
            ms_init(&g, cfg[c][0], cfg[c][1], cfg[c][2], seed * 2654435761u);
            ms_rng_seed(&rng, seed);
            ms_reveal(&g, (int)ms_rng_below(&rng, (uint32_t)g.w), (int)ms_rng_below(&rng, (uint32_t)g.h));

            for (int step = 0; step < 12 && g.state == MS_RUNNING; step++) {
                double ref[MS_MAX_CELLS];
                if (brute_force(&g, ref)) {
                    CHECK(ms_analyze(&g, an) == MS_SOLVE_OK);
                    for (int i = 0; i < g.w * g.h; i++) {
                        if (g.cell[i] & MS_OPEN) { CHECK(an->p[i] == -1.0); continue; }
                        CHECK(fabs(an->p[i] - ref[i]) < 1e-9);
                        CHECK((an->verdict[i] == MS_SAFE) == (ref[i] == 0.0));
                        CHECK((an->verdict[i] == MS_SURE_MINE) == (ref[i] == 1.0));
                        compared++;
                    }
                }
                /* Open a random safe cell to reach a new position. */
                int safe[MS_MAX_CELLS], ns = 0;
                for (int i = 0; i < g.w * g.h; i++)
                    if (!(g.cell[i] & (MS_OPEN | MS_MINE))) safe[ns++] = i;
                if (!ns) break;
                int pick = safe[ms_rng_below(&rng, (uint32_t)ns)];
                ms_reveal(&g, pick % g.w, pick / g.w);
            }
        }
    }
    free(an);
    printf("  solver vs brute force: %d cell probabilities compared\n", compared);
    CHECK(compared > 5000);
}

/* Let the solver play: open proven-safe cells, otherwise the best guess. */
static void test_solver_play(const char *name, int w, int h, int mines, int games)
{
    int won = 0, budget = 0;
    long max_nodes = 0;
    ms_analysis *an = malloc(sizeof *an);

    for (int n = 0; n < games; n++) {
        ms_game g;
        ms_init(&g, w, h, mines, 0xC0FFEE00u + (uint32_t)n);
        ms_reveal(&g, w / 2, h / 2);
        while (g.state == MS_RUNNING) {
            ms_solve_status st = ms_analyze(&g, an);
            if (st == MS_SOLVE_BUDGET) { budget++; break; }
            CHECK(st == MS_SOLVE_OK);
            if (st != MS_SOLVE_OK) break;
            if (an->nodes > max_nodes) max_nodes = an->nodes;

            int moved = 0;
            for (int i = 0; i < w * h; i++) {
                if (an->verdict[i] == MS_SURE_MINE) CHECK(g.cell[i] & MS_MINE);
                if (an->verdict[i] != MS_SAFE || (g.cell[i] & MS_OPEN)) continue;
                CHECK(!(g.cell[i] & MS_MINE));          /* a proof must never be wrong */
                ms_reveal(&g, i % w, i / w);
                moved = 1;
            }
            if (!moved) ms_reveal(&g, an->best % w, an->best / w);
        }
        won += g.state == MS_WON;
    }
    free(an);
    printf("  %-12s %2dx%-2d %3d mines: solver won %4d/%d (%.1f%%), "
           "budget exceeded in %d games, max %ld nodes\n",
           name, w, h, mines, won, games, 100.0 * won / games, budget, max_nodes);
}

int main(void)
{
    printf("rng\n");           test_rng();
    printf("deal\n");          test_deal();
    printf("rules\n");         test_rules();
    printf("solver, exact\n"); test_solver_exact();
    printf("solver, play\n");
    test_solver_play("beginner", 9, 9, 10, 2000);
    test_solver_play("intermediate", 16, 16, 40, 1000);
    test_solver_play("expert", 30, 16, 99, 500);

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
