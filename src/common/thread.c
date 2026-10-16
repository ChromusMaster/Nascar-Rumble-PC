#include "thread.h"

#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef HANDLE nr_thr;
typedef CRITICAL_SECTION nr_mtx;
typedef CONDITION_VARIABLE nr_cnd;
#define MTX_INIT(m) InitializeCriticalSection(m)
#define MTX_FREE(m) DeleteCriticalSection(m)
#define MTX_LOCK(m) EnterCriticalSection(m)
#define MTX_UNLOCK(m) LeaveCriticalSection(m)
#define CND_INIT(c) InitializeConditionVariable(c)
#define CND_FREE(c) ((void)0)
#define CND_WAIT(c, m) SleepConditionVariableCS(c, m, INFINITE)
#define CND_BCAST(c) WakeAllConditionVariable(c)
#else
#include <pthread.h>
#include <unistd.h>
typedef pthread_t nr_thr;
typedef pthread_mutex_t nr_mtx;
typedef pthread_cond_t nr_cnd;
#define MTX_INIT(m) pthread_mutex_init(m, NULL)
#define MTX_FREE(m) pthread_mutex_destroy(m)
#define MTX_LOCK(m) pthread_mutex_lock(m)
#define MTX_UNLOCK(m) pthread_mutex_unlock(m)
#define CND_INIT(c) pthread_cond_init(c, NULL)
#define CND_FREE(c) pthread_cond_destroy(c)
#define CND_WAIT(c, m) pthread_cond_wait(c, m)
#define CND_BCAST(c) pthread_cond_broadcast(c)
#endif

struct nr_pool {
    int nthreads;
    nr_thr thr[16];
    nr_mtx m;
    nr_cnd start, done;
    /* current job */
    nr_job_fn fn;
    void *ctx;
    int n, next, finished;
    unsigned generation;
    bool quit;
};

int nr_cpu_count(void)
{
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwNumberOfProcessors;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
#endif
}

/* takes and runs indices until none are left; called with the lock held */
static void drain(nr_pool *p)
{
    while (p->next < p->n) {
        int i = p->next++;
        nr_job_fn fn = p->fn;
        void *ctx = p->ctx;
        MTX_UNLOCK(&p->m);
        fn(ctx, i);
        MTX_LOCK(&p->m);
        if (++p->finished == p->n) CND_BCAST(&p->done);
    }
}

#ifdef _WIN32
static DWORD WINAPI worker(LPVOID arg)
#else
static void *worker(void *arg)
#endif
{
    nr_pool *p = (nr_pool *)arg;
    unsigned seen = 0;
    MTX_LOCK(&p->m);
    for (;;) {
        while (!p->quit && p->generation == seen) CND_WAIT(&p->start, &p->m);
        if (p->quit) break;
        seen = p->generation;
        drain(p);
    }
    MTX_UNLOCK(&p->m);
    return 0;
}

nr_pool *nr_pool_new(int threads)
{
    nr_pool *p = (nr_pool *)calloc(1, sizeof(nr_pool));
    int i;
    if (!p) return NULL;
    if (threads <= 0) threads = nr_cpu_count();
    if (threads > 16) threads = 16;
    MTX_INIT(&p->m);
    CND_INIT(&p->start);
    CND_INIT(&p->done);
    /* the calling thread works too, so start threads-1 helpers */
    for (i = 0; i < threads - 1; i++) {
#ifdef _WIN32
        p->thr[i] = CreateThread(NULL, 0, worker, p, 0, NULL);
        if (!p->thr[i]) break;
#else
        if (pthread_create(&p->thr[i], NULL, worker, p) != 0) break;
#endif
    }
    p->nthreads = i + 1;
    return p;
}

void nr_pool_free(nr_pool *p)
{
    int i;
    if (!p) return;
    MTX_LOCK(&p->m);
    p->quit = true;
    CND_BCAST(&p->start);
    MTX_UNLOCK(&p->m);
    for (i = 0; i < p->nthreads - 1; i++) {
#ifdef _WIN32
        WaitForSingleObject(p->thr[i], INFINITE);
        CloseHandle(p->thr[i]);
#else
        pthread_join(p->thr[i], NULL);
#endif
    }
    MTX_FREE(&p->m);
    CND_FREE(&p->start);
    CND_FREE(&p->done);
    free(p);
}

int nr_pool_size(const nr_pool *p) { return p ? p->nthreads : 1; }

void nr_pool_run(nr_pool *p, int n, nr_job_fn fn, void *ctx)
{
    int i;
    if (n <= 0) return;
    if (!p || p->nthreads <= 1 || n == 1) {
        for (i = 0; i < n; i++) fn(ctx, i);
        return;
    }
    MTX_LOCK(&p->m);
    p->fn = fn;
    p->ctx = ctx;
    p->n = n;
    p->next = 0;
    p->finished = 0;
    p->generation++;
    CND_BCAST(&p->start);
    drain(p);
    while (p->finished < p->n) CND_WAIT(&p->done, &p->m);
    MTX_UNLOCK(&p->m);
}
