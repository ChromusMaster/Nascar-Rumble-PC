#include "rlist.h"

#include <stdlib.h>
#include <string.h>

/* Hard limits protect against runaway games: about 64 MB of commands. */
#define MAX_CMDS (1u << 19)
#define MAX_DATA (1u << 24)

nr_rlist *nr_rlist_new(void)
{
    nr_rlist *l = (nr_rlist *)calloc(1, sizeof(nr_rlist));
    return l;
}

void nr_rlist_free(nr_rlist *l)
{
    if (!l) return;
    free(l->cmd);
    free(l->data);
    free(l->disp24);
    free(l);
}

void nr_rlist_clear(nr_rlist *l)
{
    l->n = 0;
    l->ndata = 0;
    l->overflow = false;
}

nr_rcmd *nr_rlist_push(nr_rlist *l)
{
    nr_rcmd *c;
    if (l->n >= l->cap) {
        u32 nc = l->cap ? l->cap * 2 : 8192;
        nr_rcmd *p;
        if (nc > MAX_CMDS) { l->overflow = true; return NULL; }
        p = (nr_rcmd *)realloc(l->cmd, nc * sizeof(nr_rcmd));
        if (!p) { l->overflow = true; return NULL; }
        l->cmd = p;
        l->cap = nc;
    }
    c = &l->cmd[l->n++];
    memset(c, 0, sizeof(*c));
    return c;
}

u32 nr_rlist_alloc_data(nr_rlist *l, u32 count)
{
    u32 at;
    if (l->ndata > MAX_DATA || count > MAX_DATA - l->ndata) { l->overflow = true; return 0xFFFFFFFFu; }
    if (l->ndata + count > l->data_cap) {
        u32 nc = l->data_cap ? l->data_cap : 1u << 18;
        u16 *p;
        while (nc < l->ndata + count) nc *= 2;
        if (nc > MAX_DATA) { l->overflow = true; return 0xFFFFFFFFu; }
        p = (u16 *)realloc(l->data, (size_t)nc * sizeof(u16));
        if (!p) { l->overflow = true; return 0xFFFFFFFFu; }
        l->data = p;
        l->data_cap = nc;
    }
    at = l->ndata;
    l->ndata += count;
    return at;
}
