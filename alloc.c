#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include "mmap.h"
#include "alloc.h"

#define ALIGN_MASK(SZ) ((SZ) - (1UL))
#define ALIGN_TO_SIZE(X, MASK) ((MASK + X) & ~MASK)
#define IS_ALIGNED(X, MASK) ((X & ALIGN_MASK(MASK)) == 0)

static struct KV_alloc_pool *alloc_pool[MAX_ALLOCATION_POOLS_NUM] ALLOC_UNUSED;
static int num_pools;
static struct KV_alloc_freelist alloc_freelist ALLOC_UNUSED;  // We may implement a global freelist later
#if ALLOC_DEBUG_STATS
static struct alloc_stats *stats = NULL;
#endif

static int KV_get_freelist_alloc_class(size_t size);

int (*get_alloc_class)(size_t size) = &KV_get_freelist_alloc_class;

void memory_barrier(void)
{
    asm("" ::: "memory");
}

void s_lock(struct KV_alloc_pool *pool, mtx_t *lock)
{
#ifdef CONCURRENT_ACCESS
    if (pool->allow_concurrent_allocs)
        mtx_lock(lock);
#endif
}

void s_unlock(struct KV_alloc_pool *pool, mtx_t *lock)
{
#ifdef CONCURRENT_ACCESS
    if (pool->allow_concurrent_allocs)
        mtx_unlock(lock);
#endif
}

void alloc_lock(struct KV_alloc_pool *pool, int n)
{
#ifdef CONCURRENT_ACCESS
    if (pool->allow_concurrent_allocs)
    {
        mtx_lock(&pool->alloc_freelist->lock[n]);
    }
#endif
}

void alloc_unlock(struct KV_alloc_pool *pool, int n)
{
#ifdef CONCURRENT_ACCESS
    if (pool->allow_concurrent_allocs)
    {
        mtx_unlock(&pool->alloc_freelist->lock[n]);
    }
#endif
}

const char *get_freelist_item(struct KV_alloc_pool *pool, int idx)
{
    return (const char *)pool->alloc_freelist->freelist[idx];
}

static void *KV_mmap_allocate(size_t size)
{
    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
    if (data == MAP_FAILED)
    {
        fprintf(stderr, "mmap_allocate: unable to allocate size=%u: %s\n", (unsigned)size, strerror(errno));
        return NULL;
    }
#if ALLOC_DEBUG_STATS
    s_lock(pool, &stats->lock);
    stats->alloc_size += size;
    s_unlock(pool, &stats->lock);
#endif
    return data;
}

static int KV_mmap_deallocate(void *ptr, size_t size)
{
    if (ptr == NULL)
    {
        return 0;
    }
#if ALLOC_DEBUG_STATS
    s_lock(pool, &stats->lock);
    stats->alloc_size -= size;
    s_unlock(pool, &stats->lock);
#endif
    return munmap(ptr, size);
}

struct KV_alloc_pool *KV_alloc_pool_init(size_t size, bool allow_concurrent_access)
{
    size = ALIGN_TO_SIZE(size, ALIGN_MASK(MIN_ALLOCATION_POOL_SIZE));
    struct KV_alloc_pool *pool = NULL;

    if ((num_pools + 1) > MAX_ALLOCATION_POOLS_NUM)
    {
        fprintf(stderr, "KV_alloc_pool_init: exceeded memory size= %u\n", (unsigned)size);
        return NULL;
    }

    pool = malloc(sizeof(struct KV_alloc_pool));
    if (pool == NULL)
    {
        fprintf(stderr, "KV_alloc_pool_init: malloc: unable to allocate size= %u: %s\n", (unsigned)size, strerror(errno));
        return NULL;
    }

    pool->offset = pool->size = 0;
    pool->data = NULL;

#if ALLOC_DEBUG_STATS
    pool->stats = malloc(sizeof(struct alloc_stats));
    if (pool->stats == NULL)
    {
        fprintf(stderr, "KV_alloc_pool_init: unable to allocate stats");
        return NULL;
    }
    memset(pool->stats, 0, sizeof(struct alloc_stats));
    stats = pool->stats;
    mtx_init(&pool->stats->lock, mtx_plain);
#endif

    pool->data = KV_mmap_allocate(size);
    if (pool->data == NULL)
    {
        fprintf(stderr, "KV_alloc_pool_init: mmap_allocate: unable to allocate size=%u: %s\n", (unsigned)size, strerror(errno));
        return NULL;
    }

    pool->size += size;

    pool->alloc_freelist = malloc(sizeof(struct KV_alloc_freelist));
    if (pool->alloc_freelist == NULL)
    {
        fprintf(stderr, "KV_alloc_pool_init: unable to allocate local freelist array");
        return NULL;
    }

    for (size_t i = 0; i < MAX_FREELIST_NUM_CLASSES; i++)
    {
        pool->alloc_freelist->freelist[i] = NULL;
        mtx_init(&pool->alloc_freelist->lock[i], mtx_plain);
    }
    pool->allow_concurrent_allocs = allow_concurrent_access;

    return (struct KV_alloc_pool *)pool;
}

void KV_alloc_pool_free(struct KV_alloc_pool *pool)
{
    if (pool != NULL)
    {
        if (KV_mmap_deallocate(pool->data, pool->size) == -1)
        {
            perror("mmap_deallocate");
        }
#if ALLOC_DEBUG_STATS
        printf("Deallocating size=%zu\n", stats->alloc_size);
        printf("Freelist: Hits=%i, Misses=%i, Size=%zu\n", stats->fr_hits, stats->fr_misses, stats->fr_alloc_size);
        printf("Small Allocs: Num allocated=%i, Total Size=%zu, Num In Use=%i, In use Size=%zu\n", stats->num_allocs, stats->alloc_size, stats->num_allocs_in_use, stats->allocs_in_use_size);
        printf("Large Allocs: Num allocated=%i, Size=%zu\n", stats->num_large_allocs, stats->large_allocs_size);
        mtx_destroy(&pool->stats->lock);
        free(pool->stats);
#endif
        for (size_t i = 0; i < MAX_FREELIST_NUM_CLASSES; i++)
        {
            mtx_destroy(&pool->alloc_freelist->lock[i]);
        }

        free(pool);
    }
}

static int KV_get_freelist_alloc_class(size_t size)
{
    size = ALIGN_TO_SIZE(size, ALIGN_MASK(ALLOCATION_CLASSES_INCR_SIZE));
    if (size < MIN_ALLOCATION_CLASS_SIZE + (ALLOCATION_CLASSES_INCR_SIZE * MAX_FREELIST_NUM_CLASSES))
    {
        return (size - MIN_ALLOCATION_CLASS_SIZE) / ALLOCATION_CLASSES_INCR_SIZE;
    }
    return -1;
}

static ALLOC_UNUSED void *KV_remove_from_freelist_head(struct KV_alloc_pool *pool, size_t size)
{
    assert(IS_ALIGNED(size, ALLOCATION_CLASSES_INCR_SIZE));
    struct KV_alloc_freelist *alloc_freelist = pool->alloc_freelist;

    char *next_alloc = NULL;
    int alloc_class = KV_get_freelist_alloc_class(size);
    assert(alloc_class >= 0 && alloc_class < MAX_FREELIST_NUM_CLASSES);

    alloc_lock(pool, alloc_class);
    char *alloc_class_head = alloc_freelist->freelist[alloc_class];

    // TODO: Implement best fit strategy
    if (!alloc_class_head)
    {
        alloc_unlock(pool, alloc_class);
#if ALLOC_DEBUG_STATS
        s_lock(pool, &stats->lock);
        stats->fr_misses += 1;
        s_unlock(pool, &stats->lock);
#endif
        return NULL;
    }

    if (size <= MAX_ALLOCATION_OVERHEAD)
    {
        next_alloc = *(char **)(alloc_class_head + 8);
        alloc_freelist->freelist[alloc_class] = next_alloc;
    }
    else
    {
        assert(*(char **)(alloc_class_head + 8) == NULL); // Ensure we have the head

        // Replace head with next alloc chunk
        next_alloc = *(char **)(alloc_class_head + 16);
        if (next_alloc)
        {
            *(char **)(next_alloc + 8) = NULL; // Previous chunk
        }
        alloc_freelist->freelist[alloc_class] = next_alloc;
    }
    alloc_unlock(pool, alloc_class);

#if ALLOC_DEBUG_STATS
    s_lock(pool, &stats->lock);
    stats->fr_hits += 1;
    stats->fr_alloc_size -= size;
    s_unlock(pool, &stats->lock);
#endif

    return alloc_class_head;
}

static ALLOC_UNUSED void KV_add_to_freelist(struct KV_alloc_pool *pool, char *alloc_start, size_t size)
{
    struct KV_alloc_freelist *alloc_freelist = pool->alloc_freelist;
    char *alloc_class_head = NULL; // First chunk from freelist class
    int alloc_class = KV_get_freelist_alloc_class(size);
    assert(alloc_class >= 0 && alloc_class < MAX_FREELIST_NUM_CLASSES);

    alloc_lock(pool, alloc_class);
    alloc_class_head = alloc_freelist->freelist[alloc_class];

    if (size <= MAX_ALLOCATION_OVERHEAD)
    {
        if (!alloc_class_head) // Empty
        {
            alloc_freelist->freelist[alloc_class] = alloc_start;
            *(char **)(alloc_start + 8) = NULL; // Next
        }
        else
        {
            alloc_freelist->freelist[alloc_class] = alloc_start;
            *(char **)(alloc_start + 8) = alloc_class_head; // New head next chunk

            assert(*(char **)(alloc_start + 8) != NULL);
        }
    }
    else
    {
        if (!alloc_class_head) // Empty
        {
            alloc_freelist->freelist[alloc_class] = alloc_start;
            *(char **)(alloc_start + 8) = NULL;
            *(char **)(alloc_start + 16) = NULL;
        }
        else
        {
            assert(*(char **)(alloc_class_head + 8) == NULL);

            alloc_freelist->freelist[alloc_class] = alloc_start;
            *(char **)(alloc_start + 8) = NULL;              // New head previous chunk
            *(char **)(alloc_start + 16) = alloc_class_head; // New head next chunk
            *(char **)(alloc_class_head + 8) = alloc_start;  // Old head previous chunk

            assert(*(char **)(alloc_start + 16) != NULL);
            assert(*(char **)(alloc_class_head + 8) != NULL);
        }
    }

    alloc_unlock(pool, alloc_class);

#if ALLOC_DEBUG_STATS
    s_lock(pool, &stats->lock);
    stats->fr_alloc_size += size;
    s_unlock(pool, &stats->lock);
#endif
}

void *KV_malloc(struct KV_alloc_pool *pool, size_t size)
{
    char *alloc = NULL;
    uint64_t offset; // Local offset; We assume no share between concurrent threads

    if (size <= (MIN_ALLOCATION_CLASS_SIZE - ALLOCATION_SIZE_OVERHEAD))
    {
        size = MIN_ALLOCATION_CLASS_SIZE;
    }
    else
    {
        size = ALIGN_TO_SIZE(size + ALLOCATION_SIZE_OVERHEAD, ALIGN_MASK(ALLOCATION_CLASSES_INCR_SIZE));
    }

    if (size > MIN_ALLOCATION_CLASS_SIZE + (ALLOCATION_CLASSES_INCR_SIZE * MAX_FREELIST_NUM_CLASSES))
    {
        alloc = KV_mmap_allocate(size);
        if (alloc == NULL)
        {
            fprintf(stderr, "KV_malloc: mmap_allocate: unable to allocate size= %u: %s\n", (unsigned)size, strerror(errno));
            return NULL;
        }
        *(uint64_t *)alloc = size;
#if ALLOC_DEBUG_STATS
        s_lock(pool, &stats->lock);
        stats->large_allocs_size += size;
        stats->num_large_allocs += 1;
        s_unlock(pool, &stats->lock);
#endif
        return (void *)(alloc + ALLOCATION_SIZE_OVERHEAD);
    }

    alloc = KV_remove_from_freelist_head(pool, size);
    if (alloc)
    {
#if ALLOC_DEBUG_STATS
        s_lock(pool, &stats->lock);
        stats->num_allocs_in_use += 1;
        stats->allocs_in_use_size += size;
        s_unlock(pool, &stats->lock);
#endif
        return (void *)(alloc + ALLOCATION_SIZE_OVERHEAD);
    }

    if (!pool)
    {
        fprintf(stderr, "KV_malloc: invalid memory pool");
        return NULL;
    }

    if (!pool->data)
    {
        fprintf(stderr, "KV_malloc: invalid memory address");
        return NULL;
    }
    offset = pool->offset;

    if ((offset + size) > pool->size)
    {
        fprintf(stderr, "memory limit of current pool exceeded for size=%u\n", (unsigned)size);
        return NULL;
    }

#ifdef CONCURRENT_ACCESS
    if (pool->allow_concurrent_allocs)
    {
        while (1)
        {
            if (__atomic_compare_exchange_n(&pool->offset, &offset, offset + size, 0, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
            {
                if (pool->offset > pool->size)
                {
                    pool->offset -= size;
                    fprintf(stderr, "memory limit exceeded for size=%u\n", (unsigned)size);
                    return NULL;
                }
                break;
            }
        }

        alloc = pool->data + offset;
    }
    else
    {
        pool->offset += size;
        alloc = pool->data + offset;
    }
#else
    pool->offset += size;
    alloc = pool->data + offset;
#endif

    *(uint64_t *)alloc = size;
#if ALLOC_DEBUG_STATS
    s_lock(pool, &stats->lock);
    stats->num_allocs += 1;
    stats->num_allocs_in_use += 1;
    stats->allocs_in_use_size += size;
    s_unlock(pool, &stats->lock);
#endif
    return (void *)(alloc + ALLOCATION_SIZE_OVERHEAD);
}

void KV_free(struct KV_alloc_pool *pool, void *ptr)
{
    char *alloc_start = (char *)(ptr)-ALLOCATION_SIZE_OVERHEAD;
    uint64_t size = *(uint64_t *)alloc_start;

    if (size > MIN_ALLOCATION_CLASS_SIZE + (ALLOCATION_CLASSES_INCR_SIZE * MAX_FREELIST_NUM_CLASSES))
    {
#if ALLOC_DEBUG_STATS
        s_lock(pool, &stats->lock);
        stats->large_allocs_size -= size;
        stats->num_large_allocs -= 1;
        s_unlock(pool, &stats->lock);
#endif
        KV_mmap_deallocate(alloc_start, size);
    }
    else
    {
        KV_add_to_freelist(pool, alloc_start, size);
#if ALLOC_DEBUG_STATS
        s_lock(pool, &stats->lock);
        stats->num_allocs_in_use -= 1;
        stats->allocs_in_use_size -= size;
        s_unlock(pool, &stats->lock);
#endif
    }
}
