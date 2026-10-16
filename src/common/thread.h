/* Minimal worker pool (POSIX threads or Win32), used by the software
 * renderer to rasterise horizontal bands in parallel. */
#ifndef NR_THREAD_H
#define NR_THREAD_H

#include "common.h"

typedef void (*nr_job_fn)(void *ctx, int index);
typedef struct nr_pool nr_pool;

int nr_cpu_count(void);
/* threads = 0 -> one per logical CPU (at most 16) */
nr_pool *nr_pool_new(int threads);
void nr_pool_free(nr_pool *p);
int nr_pool_size(const nr_pool *p);
/* Runs fn(ctx, i) for i in [0, n) on the pool (the caller participates) and
 * returns when all calls have finished. */
void nr_pool_run(nr_pool *p, int n, nr_job_fn fn, void *ctx);

#endif
