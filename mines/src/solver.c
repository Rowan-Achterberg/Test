/* solver.c - Exact mine probabilities. See solver.h for the method. */
#include "solver.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define NODE_BUDGET 3000000L

typedef struct {
    int n;        /* cells in this component                              */
    int first;    /* offset of its cells in work.order                    */
    int kmax;     /* most mines it could hold                             */
    double *cnt;  /* cnt[k]: solutions with exactly k mines               */
    double *mc;   /* mc[i*(kmax+1)+k]: of those, how many have cell i set */
} component;

typedef struct {
    /* One constraint per open cell that still has hidden neighbours. */
    int ncon;
    int con_need[MS_MAX_CELLS];      /* mines still to place            */
    int con_left[MS_MAX_CELLS];      /* cells still unassigned          */
    int con_n[MS_MAX_CELLS];
    int con_cell[MS_MAX_CELLS][8];
    uint8_t con_seen[MS_MAX_CELLS];

    /* Constraints each hidden cell takes part in. */
    int ncc[MS_MAX_CELLS];
    int cc[MS_MAX_CELLS][8];

    uint8_t visited[MS_MAX_CELLS];
    int order[MS_MAX_CELLS];         /* frontier cells, component by component */
    int queue[MS_MAX_CELLS];
    uint8_t val[MS_MAX_CELLS];       /* current assignment during search       */

    component *comp;
    int ncomp;

    /* search state */
    const component *cur;
    long nodes;
    int over;
} work;

/* Depth-first assignment of cell i of the current component; k mines so far. */
static void enumerate(work *w, int i, int k)
{
    const component *c = w->cur;

    if (++w->nodes > NODE_BUDGET) { w->over = 1; return; }

    if (i == c->n) {
        /* Every constraint is exhausted here, so need == 0 for all of them. */
        c->cnt[k] += 1.0;
        for (int j = 0; j < c->n; j++)
            if (w->val[j]) c->mc[j * (c->kmax + 1) + k] += 1.0;
        return;
    }

    int cell = w->order[c->first + i];
    for (int v = 0; v <= 1; v++) {
        if (v && k + 1 > c->kmax) break;
        int ok = 1;
        for (int t = 0; t < w->ncc[cell]; t++) {
            int q = w->cc[cell][t];
            w->con_left[q]--;
            w->con_need[q] -= v;
            if (w->con_need[q] < 0 || w->con_need[q] > w->con_left[q]) ok = 0;
        }
        if (ok) {
            w->val[i] = (uint8_t)v;
            enumerate(w, i + 1, k + v);
        }
        for (int t = 0; t < w->ncc[cell]; t++) {
            int q = w->cc[cell][t];
            w->con_left[q]++;
            w->con_need[q] += v;
        }
        if (w->over) return;
    }
}

/* out = a * b as polynomials, truncated to degree m. */
static void convolve(const double *a, const double *b, double *out, int m)
{
    for (int i = 0; i <= m; i++) out[i] = 0.0;
    for (int i = 0; i <= m; i++) {
        if (a[i] == 0.0) continue;
        for (int j = 0; i + j <= m; j++) out[i + j] += a[i] * b[j];
    }
}

static void free_work(work *w)
{
    if (!w) return;
    if (w->comp) {
        for (int i = 0; i < w->ncomp; i++) { free(w->comp[i].cnt); free(w->comp[i].mc); }
        free(w->comp);
    }
    free(w);
}

ms_solve_status ms_analyze(const ms_game *g, ms_analysis *out)
{
    const int total = g->w * g->h;
    const int M = g->mines;
    ms_solve_status status = MS_SOLVE_NOMEM;
    double *pre = NULL, *suf = NULL, *oth = NULL, *wt = NULL, *lf = NULL;
    int nb[8];

    memset(out, 0, sizeof *out);
    out->best = -1;
    for (int i = 0; i < total; i++) out->p[i] = -1.0;

    work *w = calloc(1, sizeof *w);
    if (!w) return MS_SOLVE_NOMEM;

    /* 1. Constraints. Flags are the player's guesses and are ignored. */
    for (int i = 0; i < total; i++) {
        if (!(g->cell[i] & MS_OPEN)) continue;
        int q = w->ncon, n = 0;
        int k = ms_neighbors(g, i, nb);
        for (int j = 0; j < k; j++)
            if (!(g->cell[nb[j]] & MS_OPEN)) w->con_cell[q][n++] = nb[j];
        if (n == 0) continue;
        w->con_n[q] = w->con_left[q] = n;
        w->con_need[q] = g->cell[i] & MS_ADJ;
        for (int j = 0; j < n; j++) {
            int c = w->con_cell[q][j];
            w->cc[c][w->ncc[c]++] = q;
        }
        w->ncon++;
    }

    /* 2. Components, by breadth-first search across shared constraints.
          BFS order also keeps related cells adjacent, which prunes early. */
    int hidden = 0, frontier = 0;
    w->comp = calloc((size_t)total + 1, sizeof *w->comp);
    if (!w->comp) goto done;
    for (int i = 0; i < total; i++) {
        if (g->cell[i] & MS_OPEN) continue;
        hidden++;
        if (w->ncc[i] == 0 || w->visited[i]) continue;

        component *c = &w->comp[w->ncomp++];
        int head = 0, tail = 0;
        c->first = frontier;
        w->visited[i] = 1;
        w->queue[tail++] = i;
        while (head < tail) {
            int cell = w->queue[head++];
            w->order[frontier++] = cell;
            for (int t = 0; t < w->ncc[cell]; t++) {
                int q = w->cc[cell][t];
                if (w->con_seen[q]) continue;
                w->con_seen[q] = 1;
                for (int j = 0; j < w->con_n[q]; j++) {
                    int v = w->con_cell[q][j];
                    if (!w->visited[v]) { w->visited[v] = 1; w->queue[tail++] = v; }
                }
            }
        }
        c->n = frontier - c->first;
        c->kmax = c->n < M ? c->n : M;
    }
    const int N = hidden - frontier;   /* sea: hidden cells with no information */
    out->components = w->ncomp;

    /* 3. Enumerate each component. */
    for (int ci = 0; ci < w->ncomp; ci++) {
        component *c = &w->comp[ci];
        c->cnt = calloc((size_t)c->kmax + 1, sizeof(double));
        c->mc = calloc((size_t)c->n * (size_t)(c->kmax + 1), sizeof(double));
        if (!c->cnt || !c->mc) goto done;
        w->cur = c;
        enumerate(w, 0, 0);
        if (w->over) { out->nodes = w->nodes; status = MS_SOLVE_BUDGET; goto done; }
    }
    out->nodes = w->nodes;

    /* 4. Sea weights: wt[r] is proportional to C(N, r), the number of ways to
          hide r mines in the sea. Computed in log space and rescaled, since
          only ratios matter and C(480, 99) is around 1e104. */
    const int C = w->ncomp;
    const size_t row = (size_t)M + 1;
    pre = calloc((size_t)(C + 1) * row, sizeof(double));
    suf = calloc((size_t)(C + 1) * row, sizeof(double));
    oth = calloc(row, sizeof(double));
    wt = calloc(row, sizeof(double));
    lf = calloc((size_t)N + 2, sizeof(double));
    if (!pre || !suf || !oth || !wt || !lf) goto done;

    for (int i = 2; i <= N; i++) lf[i] = lf[i - 1] + log((double)i);
    {
        int rmax = N < M ? N : M;
        double peak = 0.0;
        for (int r = 0; r <= rmax; r++) {
            double v = lf[N] - lf[r] - lf[N - r];
            if (v > peak) peak = v;
        }
        for (int r = 0; r <= rmax; r++) wt[r] = exp(lf[N] - lf[r] - lf[N - r] - peak);
    }

    /* 5. Prefix and suffix products of the per-component polynomials, so the
          product of "all components but one" is a single convolution. */
    {
        double *poly = calloc(row, sizeof(double));
        if (!poly) goto done;
        pre[0] = 1.0;
        for (int ci = 0; ci < C; ci++) {
            memset(poly, 0, row * sizeof(double));
            memcpy(poly, w->comp[ci].cnt, ((size_t)w->comp[ci].kmax + 1) * sizeof(double));
            convolve(pre + (size_t)ci * row, poly, pre + (size_t)(ci + 1) * row, M);
        }
        suf[(size_t)C * row] = 1.0;
        for (int ci = C - 1; ci >= 0; ci--) {
            memset(poly, 0, row * sizeof(double));
            memcpy(poly, w->comp[ci].cnt, ((size_t)w->comp[ci].kmax + 1) * sizeof(double));
            convolve(poly, suf + (size_t)(ci + 1) * row, suf + (size_t)ci * row, M);
        }
        free(poly);
    }

    /* Total weight of all consistent layouts. */
    const double *all = pre + (size_t)C * row;
    double total_weight = 0.0;
    for (int j = 0; j <= M; j++) total_weight += all[j] * wt[M - j];
    if (!(total_weight > 0.0)) { status = MS_SOLVE_INCONSISTENT; goto done; }

    /* 6. Frontier cells. Mine and safe weights are kept as separate sums of
          non-negative terms, so "proven" means an exact zero, not a rounded one. */
    for (int ci = 0; ci < C; ci++) {
        const component *c = &w->comp[ci];
        convolve(pre + (size_t)ci * row, suf + (size_t)(ci + 1) * row, oth, M);
        for (int i = 0; i < c->n; i++) {
            double mine = 0.0, safe = 0.0;
            for (int k = 0; k <= c->kmax; k++) {
                double rest = 0.0;   /* weight of everything outside, given k here */
                for (int j = 0; j + k <= M; j++) rest += oth[j] * wt[M - k - j];
                double m = c->mc[i * (c->kmax + 1) + k];
                mine += m * rest;
                safe += (c->cnt[k] - m) * rest;
            }
            int cell = w->order[c->first + i];
            out->p[cell] = mine / (mine + safe);
            if (mine == 0.0) out->verdict[cell] = MS_SAFE;
            else if (safe == 0.0) out->verdict[cell] = MS_SURE_MINE;
        }
    }

    /* 7. Sea cells all share one probability: E[mines in sea] / N. */
    if (N > 0) {
        double mine = 0.0, safe = 0.0;
        for (int j = 0; j <= M; j++) {
            int r = M - j;
            if (r > N) continue;
            double weight = all[j] * wt[r];
            mine += weight * (double)r / (double)N;
            safe += weight * (double)(N - r) / (double)N;
        }
        double p = mine / (mine + safe);
        uint8_t verdict = mine == 0.0 ? MS_SAFE : safe == 0.0 ? MS_SURE_MINE : MS_UNKNOWN;
        for (int i = 0; i < total; i++) {
            if ((g->cell[i] & MS_OPEN) || w->ncc[i]) continue;
            out->p[i] = p;
            out->verdict[i] = verdict;
        }
    }

    for (int i = 0; i < total; i++) {
        if (g->cell[i] & MS_OPEN) continue;
        if (out->verdict[i] == MS_SAFE) out->n_safe++;
        if (out->verdict[i] == MS_SURE_MINE) out->n_mine++;
        if (out->best < 0 || out->p[i] < out->p[out->best]) out->best = i;
    }
    status = MS_SOLVE_OK;

done:
    free(pre); free(suf); free(oth); free(wt); free(lf);
    free_work(w);
    return status;
}
