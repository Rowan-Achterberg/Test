/*
 * solver.h - Exact mine probabilities from public information only.
 *
 * The solver never looks at where the mines are. It reads the open numbers,
 * treats each one as a linear constraint over its hidden neighbours, and
 * counts every mine layout consistent with all of them:
 *
 *   1. Hidden cells touching a number form the frontier; the rest is "sea".
 *   2. The frontier splits into independent components (cells linked by a
 *      shared constraint). Each component is enumerated by backtracking,
 *      giving, for every k, the number of solutions with k mines and how
 *      often each cell is a mine among them.
 *   3. Components are combined by polynomial convolution, weighted by the
 *      C(sea, mines left) ways to scatter the remaining mines over the sea.
 *
 * The result is the exact probability that each hidden cell is a mine,
 * assuming every consistent layout is equally likely.
 */
#ifndef MS_SOLVER_H
#define MS_SOLVER_H

#include "core.h"

typedef enum {
    MS_SOLVE_OK,
    MS_SOLVE_BUDGET,        /* search space too large; no result          */
    MS_SOLVE_INCONSISTENT,  /* no layout matches the numbers (not a game) */
    MS_SOLVE_NOMEM
} ms_solve_status;

enum { MS_UNKNOWN = 0, MS_SAFE = 1, MS_SURE_MINE = 2 };

typedef struct {
    double  p[MS_MAX_CELLS];        /* P(mine); -1 for open cells           */
    uint8_t verdict[MS_MAX_CELLS];  /* MS_SAFE / MS_SURE_MINE when proven   */
    int     n_safe, n_mine;         /* how many cells are proven either way */
    int     best;                   /* hidden cell least likely to be a mine, or -1 */
    int     components;
    long    nodes;                  /* search nodes visited                 */
} ms_analysis;

ms_solve_status ms_analyze(const ms_game *g, ms_analysis *out);

#endif
