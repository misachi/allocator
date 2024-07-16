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

static int KV_get_freelist_alloc_class(size_t size);

int (*get_alloc_class)(size_t size) = &KV_get_freelist_alloc_class;

void memory_barrier(void)
{
    asm("" ::: "memory");
}

void alloc_lock(struct KV_alloc_freelist *alloc, int n)
{
    uint8_t expected = 0;
    while (1)
    {
        if (__atomic_compare_exchange_n(&alloc->lock[n], &expected, (uint8_t)1, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
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
    return data;
}

static int KV_mmap_deallocate(void *ptr, size_t size)
{
    if (ptr == NULL)
    {
        return 0;
    }

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
    pool->data = KV_mmap_allocate(size);
    if (pool->data == NULL)
    {
        fprintf(stderr, "KV_alloc_pool_init: mmap_allocate: unable to allocate size= %u: %s\n", (unsigned)size, strerror(errno));
        return NULL;
    }
    pool->size += size;
    num_pools += 1;
    alloc_pool[num_pools - 1] = pool;

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
        free(pool);
    }
}

static int KV_get_freelist_alloc_class(size_t size)
{
    size = ALIGN_TO_SIZE(size, ALIGN_MASK(ALLOCATION_CLASSES_INCR_SIZE));
    if (size <= MIN_ALLOCATION_CLASS_SIZE + (ALLOCATION_CLASSES_INCR_SIZE * MAX_FREELIST_NUM_CLASSES))
    {
        return (size - MIN_ALLOCATION_CLASS_SIZE) / ALLOCATION_CLASSES_INCR_SIZE;
    }
    return MAX_FREELIST_NUM_CLASSES - 1;
}

static void *KV_remove_from_freelist_head(size_t size)
{
    assert(IS_ALIGNED(size, ALLOCATION_CLASSES_INCR_SIZE));

    int alloc_class = KV_get_freelist_alloc_class(size);
    assert(alloc_class < MAX_FREELIST_NUM_CLASSES);

    alloc_lock(&alloc_freelist, alloc_class);
    char *alloc_class_head = alloc_freelist.freelist[alloc_class];

    char *next_alloc = NULL;
    // char *prev_alloc = NULL;

    // TODO: Implement best fit strategy
    if (!alloc_class_head)
    {
        alloc_unlock(&alloc_freelist, alloc_class);
        return NULL;
    }

    assert(*(char **)(alloc_class_head + 8) == NULL); // Ensure we have the head

    // Replace head with next alloc chunk
    next_alloc = *(char **)(alloc_class_head + 16);
    if (next_alloc)
    {
        *(char **)(next_alloc + 8) = NULL; // Previous chunk
        alloc_freelist.freelist[alloc_class] = next_alloc;
    }
    else
    {
        alloc_freelist.freelist[alloc_class] = next_alloc;
    }
    alloc_unlock(&alloc_freelist, alloc_class);

    return alloc_class_head;
}

static void KV_add_to_freelist(char *alloc_start, size_t size)
{
    char *alloc_class_head = NULL; // First chunk from freelist class
    int alloc_class = KV_get_freelist_alloc_class(size);
    assert(alloc_class < MAX_FREELIST_NUM_CLASSES);

    alloc_lock(&alloc_freelist, alloc_class);
    alloc_class_head = alloc_freelist.freelist[alloc_class];

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
    alloc_unlock(&alloc_freelist, alloc_class);
}

void *KV_malloc(struct KV_alloc_pool *pool, size_t size)
{
    size = ALIGN_TO_SIZE(size + ALLOCATION_SIZE_OVERHEAD, ALIGN_MASK(ALLOCATION_CLASSES_INCR_SIZE));
    char *alloc = KV_remove_from_freelist_head(size);
    int offset; // Local offset; We assume no share between concurrent threads

    if (alloc)
    {
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
#ifdef SIKV_VERBOSE
        printf("memory limit of current pool exceeded for size=%u\n", (unsigned)size);
#endif
        return NULL;
    }

    while (1)
    {
        if (__atomic_compare_exchange_n(&pool->offset, &offset, offset + size, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
        {
            break;
        }
    }

    alloc = pool->data + offset;
    *(uint64_t *)alloc = size;
    return (void *)(alloc + ALLOCATION_SIZE_OVERHEAD);
}

void KV_free(struct KV_alloc_pool *pool, void *ptr)
{
    // int64_t size = *(int64_t *)(ptr - ALLOCATION_SIZE_OVERHEAD);
    // int alloc_class = KV_get_freelist_alloc_class(size);
    char *alloc_start = (char *)(ptr)-ALLOCATION_SIZE_OVERHEAD;
    uint64_t size = *(uint64_t *)alloc_start;
    KV_add_to_freelist(alloc_start, size);
}
