/*
 * widget_cache.c — Opt-in per-widget pixel cache pool
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/widget_cache.h>
#include <stdlib.h>

#define LUI_CACHE_DEFAULT_BUDGET ((size_t)64u * 1024u * 1024u)  /* 64 MiB */

/* Process-global pool of cached widgets.  Small and single-threaded — the
 * widget tree is drawn from one thread. */
typedef struct {
    lui_widget_t **entries;     /* registered cached widgets */
    int            count, cap;
    size_t         used_bytes;
    size_t         budget_bytes;
    uint32_t       frame;
    bool           inited;
} lui_cache_pool_t;

static lui_cache_pool_t g_pool;

static void pool_init_default(void)
{
    if (!g_pool.inited) {
        g_pool.budget_bytes = LUI_CACHE_DEFAULT_BUDGET;
        g_pool.inited = true;
    }
}

static size_t cache_bytes(const lui_widget_cache_t *c)
{
    if (!c || !c->surface) return 0;
    return (size_t)c->surface->height *
           (size_t)c->surface->stride * sizeof(uint32_t);
}

static void pool_register(lui_widget_t *w)
{
    for (int i = 0; i < g_pool.count; i++)
        if (g_pool.entries[i] == w) return;
    if (g_pool.count == g_pool.cap) {
        int nc = g_pool.cap ? g_pool.cap * 2 : 8;
        lui_widget_t **ne =
            (lui_widget_t **)realloc(g_pool.entries, (size_t)nc * sizeof(*ne));
        if (!ne) return;
        g_pool.entries = ne;
        g_pool.cap = nc;
    }
    g_pool.entries[g_pool.count++] = w;
}

static void pool_unregister(lui_widget_t *w)
{
    for (int i = 0; i < g_pool.count; i++) {
        if (g_pool.entries[i] == w) {
            g_pool.entries[i] = g_pool.entries[--g_pool.count];
            return;
        }
    }
}

static void cache_free_surface(lui_widget_t *w)
{
    lui_widget_cache_t *c = w->cache;
    if (c && c->surface) {
        size_t b = cache_bytes(c);
        g_pool.used_bytes = (g_pool.used_bytes > b) ? g_pool.used_bytes - b : 0;
        lvg_surface_destroy(c->surface);
        c->surface = NULL;
        c->w = c->h = 0;
    }
    w->flags &= ~LUI_WIDGET_CACHE_VALID;
}

void lui_widget_enable_cache(lui_widget_t *w, lui_cache_priority_t prio)
{
    if (!w) return;
    pool_init_default();
    if (!w->cache) {
        w->cache = (lui_widget_cache_t *)calloc(1, sizeof(*w->cache));
        if (!w->cache) return;
    }
    w->cache->priority = prio;
    w->flags |=  LUI_WIDGET_CACHE;
    w->flags &= ~LUI_WIDGET_CACHE_VALID;
    pool_register(w);
}

void lui_widget_disable_cache(lui_widget_t *w)
{
    if (!w || !w->cache) return;
    cache_free_surface(w);
    pool_unregister(w);
    free(w->cache);
    w->cache = NULL;
    w->flags &= ~(LUI_WIDGET_CACHE | LUI_WIDGET_CACHE_VALID);
}

void lui_widget_cache_set_budget(size_t bytes)
{
    pool_init_default();
    g_pool.budget_bytes = bytes;
}

void lui_widget_cache_get_stats(size_t *used_bytes, size_t *budget_bytes,
                                int *live_count)
{
    pool_init_default();
    if (used_bytes)   *used_bytes   = g_pool.used_bytes;
    if (budget_bytes) *budget_bytes = g_pool.budget_bytes;
    if (live_count) {
        int n = 0;
        for (int i = 0; i < g_pool.count; i++)
            if (g_pool.entries[i]->cache && g_pool.entries[i]->cache->surface)
                n++;
        *live_count = n;
    }
}

void lui_widget_cache_tick(void)
{
    pool_init_default();
    g_pool.frame++;
}

uint32_t lui_widget_cache_frame(void)
{
    return g_pool.frame;
}

void lui_widget_cache_touch(lui_widget_t *w)
{
    if (w && w->cache)
        w->cache->last_used = g_pool.frame;
}

/* Evict cached surfaces — lowest priority, then least-recently-used — until at
 * least @need bytes can be allocated, never evicting @keep. */
static void pool_evict_for(size_t need, lui_widget_t *keep)
{
    while (g_pool.used_bytes + need > g_pool.budget_bytes) {
        lui_widget_t *victim = NULL;
        for (int i = 0; i < g_pool.count; i++) {
            lui_widget_t *e = g_pool.entries[i];
            if (e == keep || !e->cache || !e->cache->surface) continue;
            if (!victim) { victim = e; continue; }
            const lui_widget_cache_t *vc = victim->cache, *ec = e->cache;
            if (ec->priority < vc->priority ||
                (ec->priority == vc->priority && ec->last_used < vc->last_used))
                victim = e;
        }
        if (!victim) break;   /* nothing else to free */
        cache_free_surface(victim);
    }
}

bool lui_widget_cache_ensure(lui_widget_t *w, int width, int height)
{
    if (!w || !w->cache || width <= 0 || height <= 0) return false;
    pool_init_default();

    lui_widget_cache_t *c = w->cache;
    if (c->surface && c->w == width && c->h == height)
        return true;                       /* already the right size */

    if (c->surface)
        cache_free_surface(w);             /* size changed: drop old */

    size_t need = (size_t)width * (size_t)height * sizeof(uint32_t);
    if (need > g_pool.budget_bytes)
        return false;                      /* too big to ever fit */

    pool_evict_for(need, w);
    if (g_pool.used_bytes + need > g_pool.budget_bytes)
        return false;                      /* could not free enough */

    c->surface = lvg_surface_create(width, height);
    if (!c->surface) return false;
    c->w = width;
    c->h = height;
    c->last_used = g_pool.frame;
    g_pool.used_bytes += cache_bytes(c);
    return true;
}
