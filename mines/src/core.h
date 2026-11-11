/*
 * core.h - Minesweeper engine. Platform independent, no allocation, no I/O.
 *
 * The board is a flat array of bytes. Each cell packs its whole state:
 *
 *      bit 7   6      5      4      3..0
 *          -   FLAG   OPEN   MINE   number of adjacent mines
 *
 * A game is a pure function of (width, height, mines, seed, first cell opened):
 * mines are laid by a seeded Fisher-Yates shuffle on the first probe, so the
 * first probe and its eight neighbours are always safe and every game can be
 * replayed exactly.
 */
#ifndef MS_CORE_H
#define MS_CORE_H

#include <stdint.h>

#define MS_MAX_W     30
#define MS_MAX_H     24
#define MS_MAX_CELLS (MS_MAX_W * MS_MAX_H)

enum {
    MS_ADJ  = 0x0F,
    MS_MINE = 0x10,
    MS_OPEN = 0x20,
    MS_FLAG = 0x40
};

typedef enum { MS_READY, MS_RUNNING, MS_WON, MS_LOST } ms_state;

typedef struct {
    int      w, h, mines;
    uint32_t seed;
    ms_state state;
    int      opened;   /* number of open cells                         */
    int      flags;    /* number of flagged cells                      */
    int      boom;     /* index of the mine that was opened, or -1     */
    int      bbbv;     /* 3BV: minimum clicks needed (0 until dealt)   */
    int      clicks;   /* effective opening actions made by the player */
    uint8_t  cell[MS_MAX_CELLS];
} ms_game;

/* xoshiro256** seeded through splitmix64. */
typedef struct { uint64_t s[4]; } ms_rng;
void     ms_rng_seed(ms_rng *r, uint64_t seed);
uint64_t ms_rng_next(ms_rng *r);
uint32_t ms_rng_below(ms_rng *r, uint32_t n);   /* uniform in [0, n), unbiased */

/* Start a new game. Arguments are clamped to the supported range. */
void ms_init(ms_game *g, int w, int h, int mines, uint32_t seed);

/* Indices of the cells adjacent to idx. Returns how many were written (3..8). */
int ms_neighbors(const ms_game *g, int idx, int out[8]);

/*
 * Player actions. Each returns the number of cells newly opened,
 * 0 if the action had no effect, or -1 if it opened a mine.
 */
int ms_reveal(ms_game *g, int x, int y);        /* open a hidden cell              */
int ms_chord(ms_game *g, int x, int y);         /* open around a satisfied number  */
int ms_toggle_flag(ms_game *g, int x, int y);   /* returns 1 if the flag changed   */

#endif
