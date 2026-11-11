/* core.c - Minesweeper engine. See core.h for the data model. */
#include "core.h"

#include <string.h>

/* ---------------------------------------------------------------- PRNG -- */

static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

static uint64_t splitmix64(uint64_t *state)
{
    uint64_t z = (*state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

void ms_rng_seed(ms_rng *r, uint64_t seed)
{
    for (int i = 0; i < 4; i++) r->s[i] = splitmix64(&seed);
}

uint64_t ms_rng_next(ms_rng *r)
{
    uint64_t *s = r->s;
    uint64_t result = rotl(s[1] * 5, 7) * 9;
    uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl(s[3], 45);
    return result;
}

/* Lemire's multiply-and-reject method: no modulo bias. */
uint32_t ms_rng_below(ms_rng *r, uint32_t n)
{
    uint64_t m = (uint64_t)(uint32_t)(ms_rng_next(r) >> 32) * n;
    uint32_t low = (uint32_t)m;
    if (low < n) {
        uint32_t threshold = (0u - n) % n;
        while (low < threshold) {
            m = (uint64_t)(uint32_t)(ms_rng_next(r) >> 32) * n;
            low = (uint32_t)m;
        }
    }
    return (uint32_t)(m >> 32);
}

/* --------------------------------------------------------------- board -- */

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void ms_init(ms_game *g, int w, int h, int mines, uint32_t seed)
{
    memset(g, 0, sizeof *g);
    g->w = clampi(w, 2, MS_MAX_W);
    g->h = clampi(h, 2, MS_MAX_H);
    g->mines = clampi(mines, 1, g->w * g->h - 1);
    g->seed = seed;
    g->state = MS_READY;
    g->boom = -1;
}

int ms_neighbors(const ms_game *g, int idx, int out[8])
{
    int x = idx % g->w, y = idx / g->w, n = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int nx = x + dx, ny = y + dy;
            if ((dx || dy) && nx >= 0 && nx < g->w && ny >= 0 && ny < g->h)
                out[n++] = ny * g->w + nx;
        }
    }
    return n;
}

/*
 * 3BV (Bechtel's Board Benchmark Value): the least number of left clicks that
 * clears the board. Every connected region of zeros costs one click and takes
 * its numbered border with it; every remaining numbered cell costs one more.
 */
static int compute_3bv(const ms_game *g)
{
    uint8_t seen[MS_MAX_CELLS] = {0};
    int queue[MS_MAX_CELLS], nb[8];
    int total = g->w * g->h, value = 0;

    for (int i = 0; i < total; i++) {
        if (seen[i] || (g->cell[i] & (MS_MINE | MS_ADJ))) continue;
        int head = 0, tail = 0;
        value++;
        seen[i] = 1;
        queue[tail++] = i;
        while (head < tail) {
            int c = queue[head++];
            int k = ms_neighbors(g, c, nb);
            for (int j = 0; j < k; j++) {
                int v = nb[j];
                if (seen[v]) continue;
                seen[v] = 1;
                if (!(g->cell[v] & MS_ADJ)) queue[tail++] = v;
            }
        }
    }
    for (int i = 0; i < total; i++)
        if (!seen[i] && !(g->cell[i] & MS_MINE)) value++;
    return value;
}

/* Lay the mines, keeping `safe` and, when there is room, its neighbours clear. */
static void deal(ms_game *g, int safe)
{
    int total = g->w * g->h;
    uint8_t excluded[MS_MAX_CELLS] = {0};
    int pool[MS_MAX_CELLS], nb[8], n = 0;

    excluded[safe] = 1;
    int k = ms_neighbors(g, safe, nb);
    if (g->mines <= total - 1 - k)
        for (int j = 0; j < k; j++) excluded[nb[j]] = 1;

    for (int i = 0; i < total; i++)
        if (!excluded[i]) pool[n++] = i;

    /* Partial Fisher-Yates: the first `mines` slots become a uniform sample. */
    ms_rng rng;
    ms_rng_seed(&rng, ((uint64_t)g->seed << 32) ^ (uint64_t)(uint32_t)safe);
    for (int i = 0; i < g->mines; i++) {
        int j = i + (int)ms_rng_below(&rng, (uint32_t)(n - i));
        int t = pool[i]; pool[i] = pool[j]; pool[j] = t;
        g->cell[pool[i]] |= MS_MINE;
    }

    for (int i = 0; i < total; i++) {
        int adjacent = 0;
        k = ms_neighbors(g, i, nb);
        for (int j = 0; j < k; j++) adjacent += (g->cell[nb[j]] & MS_MINE) != 0;
        g->cell[i] = (uint8_t)((g->cell[i] & ~MS_ADJ) | adjacent);
    }
    g->bbbv = compute_3bv(g);
}

/*
 * Open one hidden, unflagged cell and cascade through zeros breadth first.
 * Returns the number of cells opened, or -1 if `start` is a mine.
 */
static int open_from(ms_game *g, int start)
{
    int queue[MS_MAX_CELLS], nb[8];
    int head = 0, tail = 0, count = 0;

    if (g->cell[start] & MS_MINE) {
        g->cell[start] |= MS_OPEN;
        g->boom = start;
        g->state = MS_LOST;
        return -1;
    }
    g->cell[start] |= MS_OPEN;
    queue[tail++] = start;
    while (head < tail) {
        int c = queue[head++];
        count++;
        if (g->cell[c] & MS_ADJ) continue;
        int k = ms_neighbors(g, c, nb);
        for (int j = 0; j < k; j++) {
            int v = nb[j];
            if (g->cell[v] & (MS_OPEN | MS_FLAG)) continue;
            g->cell[v] |= MS_OPEN;
            queue[tail++] = v;
        }
    }
    g->opened += count;
    return count;
}

static void check_win(ms_game *g)
{
    int total = g->w * g->h;
    if (g->state != MS_RUNNING || g->opened != total - g->mines) return;
    g->state = MS_WON;
    g->flags = 0;
    for (int i = 0; i < total; i++) {
        if (g->cell[i] & MS_MINE) { g->cell[i] |= MS_FLAG; g->flags++; }
        else g->cell[i] &= (uint8_t)~MS_FLAG;
    }
}

static int in_play(const ms_game *g, int x, int y)
{
    return (g->state == MS_READY || g->state == MS_RUNNING) &&
           x >= 0 && x < g->w && y >= 0 && y < g->h;
}

int ms_reveal(ms_game *g, int x, int y)
{
    if (!in_play(g, x, y)) return 0;
    int idx = y * g->w + x;
    if (g->cell[idx] & (MS_OPEN | MS_FLAG)) return 0;
    if (g->state == MS_READY) {
        deal(g, idx);
        g->state = MS_RUNNING;
    }
    g->clicks++;
    int opened = open_from(g, idx);
    check_win(g);
    return opened;
}

int ms_chord(ms_game *g, int x, int y)
{
    if (!in_play(g, x, y) || g->state != MS_RUNNING) return 0;
    int idx = y * g->w + x, nb[8], flagged = 0, hidden = 0;
    if (!(g->cell[idx] & MS_OPEN) || !(g->cell[idx] & MS_ADJ)) return 0;

    int k = ms_neighbors(g, idx, nb);
    for (int j = 0; j < k; j++) {
        if (g->cell[nb[j]] & MS_FLAG) flagged++;
        else if (!(g->cell[nb[j]] & MS_OPEN)) hidden++;
    }
    if (flagged != (g->cell[idx] & MS_ADJ) || hidden == 0) return 0;

    int opened = 0;
    g->clicks++;
    for (int j = 0; j < k; j++) {
        if (g->cell[nb[j]] & (MS_OPEN | MS_FLAG)) continue;
        int r = open_from(g, nb[j]);
        if (r < 0) return -1;
        opened += r;
    }
    check_win(g);
    return opened;
}

int ms_toggle_flag(ms_game *g, int x, int y)
{
    if (!in_play(g, x, y)) return 0;
    int idx = y * g->w + x;
    if (g->cell[idx] & MS_OPEN) return 0;
    g->cell[idx] ^= MS_FLAG;
    g->flags += (g->cell[idx] & MS_FLAG) ? 1 : -1;
    return 1;
}
