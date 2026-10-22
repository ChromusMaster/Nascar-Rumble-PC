/* Sub-pixel vertex cache ("PGXP"-style precision, experimental).
 *
 * The GTE rounds projected vertices to whole pixels; that rounding makes
 * polygons wobble and, combined with affine texture mapping, makes textures
 * swim. When enabled, every RTPS/RTPT also stores the unrounded position and
 * depth, keyed by the packed integer coordinate the game will send to the
 * GPU. The render recorder looks the coordinate up and, on a hit, gives the
 * enhanced renderers the precise position and the depth for perspective-
 * correct texturing. The console VRAM (what the game sees) still uses the
 * integer coordinates, so the game's behaviour is unchanged. */
#ifndef NR_PGXP_H
#define NR_PGXP_H

#include "../common/common.h"

#define NR_PGXP_SIZE 8192
#define NR_PGXP_MAX_AGE 2u

typedef struct nr_pgxp_s {
    u32 key[NR_PGXP_SIZE];
    float x[NR_PGXP_SIZE], y[NR_PGXP_SIZE], w[NR_PGXP_SIZE];
    u32 gen[NR_PGXP_SIZE];
    bool valid[NR_PGXP_SIZE];
    bool ambiguous[NR_PGXP_SIZE];
    u32 now;
    u64 hits, misses, conflicts, expired;
} nr_pgxp;

static inline u32 nr_pgxp_slot(u32 key) { return (key * 2654435761u) >> 19; }

static inline void nr_pgxp_tick(nr_pgxp *c) { c->now++; }

static inline bool nr_pgxp_same_depth(float a, float b)
{
    float d = a > b ? a - b : b - a, m = a > b ? a : b;
    return d <= 1.0f || d <= m * 0.02f;
}

static inline void nr_pgxp_store(nr_pgxp *c, u32 key, float x, float y, float w)
{
    u32 s = nr_pgxp_slot(key);
    if (c->valid[s] && c->key[s] == key && c->gen[s] == c->now) {
        if (c->ambiguous[s]) return;
        if (!nr_pgxp_same_depth(c->w[s], w)) { c->ambiguous[s] = true; c->conflicts++; return; }
    } else {
        c->ambiguous[s] = false;
    }
    c->key[s] = key; c->x[s] = x; c->y[s] = y; c->w[s] = w; c->gen[s] = c->now; c->valid[s] = true;
}

static inline bool nr_pgxp_lookup(nr_pgxp *c, u32 key, float *x, float *y, float *w)
{
    u32 s = nr_pgxp_slot(key);
    if (!c->valid[s] || c->key[s] != key || c->ambiguous[s]) { c->misses++; return false; }
    if (c->now - c->gen[s] > NR_PGXP_MAX_AGE) { c->expired++; c->misses++; return false; }
    *x = c->x[s]; *y = c->y[s]; *w = c->w[s];
    c->hits++;
    return true;
}

#endif
