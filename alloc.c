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

static struct KV_alloc_pool *alloc_pool[MAX_ALLOCATION_POOLS_NUM];
static int num_pools;
static struct KV_alloc_freelist alloc_freelist;
#if ALLOC_DEBUG_STATS
static struct alloc_stats *stats = NULL;
#endif

static int KV_get_freelist_alloc_class(size_t size);

int (*get_alloc_class)(size_t size) = &KV_get_freelist_alloc_class;

void memory_barrier(void)
{
    asm("" ::: "memory");
}

void s_lock(int8_t *lock)
{
    while (1)
    {
        if (__atomic_test_and_set(lock, __ATOMIC_RELEASE))
        {
            break;
        }
    }
}

void s_unlock(int8_t *lock)
{
    assert(*lock == 1);
    lock = 0;
}

void alloc_lock(struct KV_alloc_freelist *alloc, int n)
{
    uint8_t expected = 0;
    while (1)
    {
        if (__atomic_compare_exchange_n(&alloc->lock[n], &expected, (uint8_t)1, 0, __ATOMIC_RELEASE, __ATOMIC_RELAXED))
        {
            break;
        }
    }
}

void alloc_unlock(struct KV_alloc_freelist *alloc, int n)
{
    // This thread is the only one that has a lock on the object
    assert(alloc->lock[n] == 1);
    alloc->lock[n] = 0;
}

const char *get_freelist_item(int idx)
{
    return (const char *)alloc_freelist.freelist[idx];
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
    s_lock(&stats->lock);
    stats->alloc_size += size;
    s_unlock(&stats->lock);
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
    s_lock(&stats->lock);
    stats->alloc_size -= size;
    s_unlock(&stats->lock);
#endif
    // memset(ptr, 0, size);
    return munmap(ptr, size);
}

struct KV_alloc_pool *KV_alloc_pool_init(size_t size)
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
    // pool->prev = pool->next = NULL;

#if ALLOC_DEBUG_STATS
    pool->stats = malloc(sizeof(struct alloc_stats));
    if (pool->stats == NULL)
    {
        fprintf(stderr, "KV_alloc_pool_init: unable to allocate stats");
        return NULL;
    }
    memset(pool->stats, 0, sizeof(struct alloc_stats));
    stats = pool->stats;
#endif

    pool->data = KV_mmap_allocate(size);
    if (pool->data == NULL)
    {
        fprintf(stderr, "KV_alloc_pool_init: mmap_allocate: unable to allocate size= %u: %s\n", (unsigned)size, strerror(errno));
        return NULL;
    }
    pool->size += size;
    num_pools += 1;
    alloc_pool[num_pools - 1] = pool;

    for (size_t i = 0; i < MAX_FREELIST_NUM_CLASSES; i++)
    {
        alloc_freelist.freelist[i] = NULL;
    }

    return (struct KV_alloc_pool *)pool;
}

void KV_alloc_pool_free(struct KV_alloc_pool *pool)
{

#if ALLOC_DEBUG_STATS
    printf("Deallocating size=%zu\n", stats->alloc_size);
    printf("Freelist: Hits=%i, Misses=%i, Size=%zu\n", stats->fr_hits, stats->fr_misses, stats->fr_alloc_size);
    printf("Small Allocs: Num allocated=%i, Total Size=%zu, Num In Use=%i, In use Size=%zu\n", stats->num_allocs, stats->alloc_size, stats->num_allocs_in_use, stats->allocs_in_use_size);
    printf("Large Allocs: Num allocated=%i, Size=%zu\n", stats->num_large_allocs, stats->large_allocs_size);
#endif
    if (pool != NULL)
    {
        if (KV_mmap_deallocate(pool->data, pool->size) == -1)
        {
            perror("mmap_deallocate");
        }
#if ALLOC_DEBUG_STATS
        free(pool->stats);
#endif
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

static void *KV_remove_from_freelist_head(size_t size)
{
    assert(IS_ALIGNED(size, ALLOCATION_CLASSES_INCR_SIZE));

    char *next_alloc = NULL;
    int alloc_class = KV_get_freelist_alloc_class(size);
    assert(alloc_class >= 0 && alloc_class < MAX_FREELIST_NUM_CLASSES);

    alloc_lock(&alloc_freelist, alloc_class);
    char *alloc_class_head = alloc_freelist.freelist[alloc_class];

    // TODO: Implement best fit strategy
    if (!alloc_class_head)
    {
#if ALLOC_DEBUG_STATS
        s_lock(&stats->lock);
        stats->fr_misses += 1;
        s_unlock(&stats->lock);
#endif
        alloc_unlock(&alloc_freelist, alloc_class);
        return NULL;
    }

    if (size <= MAX_ALLOCATION_OVERHEAD)
    {
        next_alloc = *(char **)(alloc_class_head + 8);
        alloc_freelist.freelist[alloc_class] = next_alloc;
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
        alloc_freelist.freelist[alloc_class] = next_alloc;
    }
    alloc_unlock(&alloc_freelist, alloc_class);

#if ALLOC_DEBUG_STATS
    s_lock(&stats->lock);
    stats->fr_hits += 1;
    stats->fr_alloc_size -= size;
    s_unlock(&stats->lock);
#endif

    return alloc_class_head;
}

static void KV_add_to_freelist(char *alloc_start, size_t size)
{
    char *alloc_class_head = NULL; // First chunk from freelist class
    int alloc_class = KV_get_freelist_alloc_class(size);
    assert(alloc_class >= 0 && alloc_class < MAX_FREELIST_NUM_CLASSES);

    alloc_lock(&alloc_freelist, alloc_class);
    alloc_class_head = alloc_freelist.freelist[alloc_class];

    if (size <= MAX_ALLOCATION_OVERHEAD)
    {
        if (!alloc_class_head) // Empty
        {
            alloc_freelist.freelist[alloc_class] = alloc_start;
            *(char **)(alloc_start + 8) = NULL; // Next
        }
        else
        {
            alloc_freelist.freelist[alloc_class] = alloc_start;
            *(char **)(alloc_start + 8) = alloc_class_head; // New head next chunk

            assert(*(char **)(alloc_start + 8) != NULL);
        }
    }
    else
    {
        if (!alloc_class_head) // Empty
        {
            alloc_freelist.freelist[alloc_class] = alloc_start;
            *(char **)(alloc_start + 8) = NULL;
            *(char **)(alloc_start + 16) = NULL;
        }
        else
        {
            assert(*(char **)(alloc_class_head + 8) == NULL);

            alloc_freelist.freelist[alloc_class] = alloc_start;
            *(char **)(alloc_start + 8) = NULL;              // New head previous chunk
            *(char **)(alloc_start + 16) = alloc_class_head; // New head next chunk
            *(char **)(alloc_class_head + 8) = alloc_start;  // Old head previous chunk

            assert(*(char **)(alloc_start + 16) != NULL);
            assert(*(char **)(alloc_class_head + 8) != NULL);
        }
    }

    alloc_unlock(&alloc_freelist, alloc_class);

#if ALLOC_DEBUG_STATS
    s_lock(&stats->lock);
    stats->fr_alloc_size += size;
    s_unlock(&stats->lock);
#endif
}

void *KV_malloc(struct KV_alloc_pool *pool, size_t size)
{
    char *alloc = NULL;
    int offset; // Local offset; We assume no share between concurrent threads

    // size = ALIGN_TO_SIZE(size + ALLOCATION_SIZE_OVERHEAD, ALIGN_MASK(ALLOCATION_CLASSES_INCR_SIZE));
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
        s_lock(&stats->lock);
        stats->large_allocs_size += size;
        stats->num_large_allocs += 1;
        s_unlock(&stats->lock);
#endif
        return (void *)(alloc + ALLOCATION_SIZE_OVERHEAD);
    }

    alloc = KV_remove_from_freelist_head(size);
    if (alloc)
    {
#if ALLOC_DEBUG_STATS
        s_lock(&stats->lock);
        stats->num_allocs_in_use += 1;
        stats->allocs_in_use_size += size;
        s_unlock(&stats->lock);
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

    while (1)
    {
        if (__atomic_compare_exchange_n(&pool->offset, &offset, offset + size, 0, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
        {
            if (pool->offset > pool->size)
            {
                pool->offset -= size;
                fprintf(stderr, "memory limit of current pool exceeded for size=%u\n", (unsigned)size);
                return NULL;
            }
            break;
        }
        offset = pool->offset; // Refresh; Just in case we need to
    }

    alloc = pool->data + offset;
    *(uint64_t *)alloc = size;
#if ALLOC_DEBUG_STATS
    s_lock(&stats->lock);
    stats->num_allocs += 1;
    stats->num_allocs_in_use += 1;
    stats->allocs_in_use_size += size;
    s_unlock(&stats->lock);
#endif
    return (void *)(alloc + ALLOCATION_SIZE_OVERHEAD);
}

void KV_free(struct KV_alloc_pool *pool, void *ptr)
{
    // int64_t size = *(int64_t *)(ptr - ALLOCATION_SIZE_OVERHEAD);
    // int alloc_class = KV_get_freelist_alloc_class(size);
    char *alloc_start = (char *)(ptr)-ALLOCATION_SIZE_OVERHEAD;
    uint64_t size = *(uint64_t *)alloc_start;

    if (size > MIN_ALLOCATION_CLASS_SIZE + (ALLOCATION_CLASSES_INCR_SIZE * MAX_FREELIST_NUM_CLASSES))
    {
#if ALLOC_DEBUG_STATS
        s_lock(&stats->lock);
        stats->large_allocs_size -= size;
        stats->num_large_allocs -= 1;
        s_unlock(&stats->lock);
#endif
        KV_mmap_deallocate(alloc_start, size);
    }
    else
    {
        KV_add_to_freelist(alloc_start, size);
#if ALLOC_DEBUG_STATS
        s_lock(&stats->lock);
        stats->num_allocs_in_use -= 1;
        stats->allocs_in_use_size -= size;
        s_unlock(&stats->lock);
#endif
    }
}
