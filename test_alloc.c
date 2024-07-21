#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "alloc.h"

#define TEST_ALLOC_DEBUG_VERBOSE 0

extern int (*get_alloc_class)(size_t size);

void test_KV_alloc_pool_init()
{
    size_t size = MIN_ALLOCATION_POOL_SIZE;
    struct KV_alloc_pool *pool = KV_alloc_pool_init(size, false);

    memset(pool->data, -1, size);

    assert(pool->size == size);
    assert(pool->data != NULL);
    assert(pool->data[size - 1] == -1);

    KV_alloc_pool_free(pool);
}

void test_KV_malloc()
{
    size_t size = MIN_ALLOCATION_POOL_SIZE;
    struct KV_alloc_pool *pool = KV_alloc_pool_init(size, false);

    size_t alloc_size = 32;
    char *alloc = (char *)KV_malloc(pool, alloc_size);

    assert(alloc != NULL);
    assert(pool->offset == (alloc_size + ALLOCATION_SIZE_OVERHEAD));

    assert(*(uint64_t *)(alloc - 8) == (alloc_size + ALLOCATION_SIZE_OVERHEAD));

    char *alloc2 = (char *)KV_malloc(pool, alloc_size);
    assert(alloc2 != NULL);
    assert(pool->offset == ((alloc_size + ALLOCATION_SIZE_OVERHEAD) * 2));

    assert(((alloc2 + alloc_size) - pool->data) == 80);

    KV_alloc_pool_free(pool);
}

void test_KV_free()
{
    size_t size = MIN_ALLOCATION_POOL_SIZE;
    struct KV_alloc_pool *pool = KV_alloc_pool_init(size, false);

    size_t alloc_size = 16;
    char *alloc = (char *)KV_malloc(pool, alloc_size);

    KV_free(pool, alloc);
    const char *item = get_freelist_item(pool, 1);

    assert(item != NULL);                  // Check if chunk is placed in the right position in freelist
    assert(*(uint64_t *)item == 24);       // Check size
    assert(*(char **)(item + 8) == NULL);  // Previous chunk should be null; This is the head
    assert(*(char **)(item + 16) == NULL); // Next chunk should be null; We haven't free'd other similar sized chunks

    alloc = (char *)KV_malloc(pool, alloc_size); // We'll get allocation from freelist

    item = get_freelist_item(pool, 1);
    assert(item == NULL); // We got our allocation from the freelist; The class should be empty now

    KV_free(pool, alloc);

    KV_alloc_pool_free(pool);
}

void test_KV_freelist_class_linked_list()
{
    size_t size = MIN_ALLOCATION_POOL_SIZE;
    struct KV_alloc_pool *pool = KV_alloc_pool_init(size, false);

    size_t alloc_size = 40;
    char *alloc = (char *)KV_malloc(pool, alloc_size);
    char *alloc2 = (char *)KV_malloc(pool, alloc_size);

    // Garbage data we're going to test for later
    memset(alloc, -1, alloc_size);
    memset(alloc2, -2, alloc_size);

    KV_free(pool, alloc);
    KV_free(pool, alloc2);

    const char *item = get_freelist_item(pool, 4);
    assert(item != NULL);

    assert(*(uint64_t *)item == 48);       // Check size
    assert(*(char **)(item + 8) == NULL);  // Previous chunk should be null; Last allocation makes the new head
    assert(*(char **)(item + 16) != NULL); // Next chunk should be non-empty; We free'd 2 allocations of the same size

    char *next_alloc = *(char **)(item + 16);
    assert(next_alloc != NULL);
    char *prev_alloc = *(char **)(next_alloc + 8); // Get head pointer from pointer madness; Just want to know if the pointers are placed correctly

    // Check that the chunks are arranged correctly in the freelist class; We use the garbage data we put on the chunks
    assert(*(next_alloc + ALLOCATION_SIZE_OVERHEAD + MAX_ALLOCATION_OVERHEAD) == -1);
    assert(*(prev_alloc + ALLOCATION_SIZE_OVERHEAD + MAX_ALLOCATION_OVERHEAD) == -2);

    assert(*(uint64_t *)next_alloc == 48);       // Check size
    assert(*(char **)(next_alloc + 8) != NULL);  // Previous chunk should be null; This is the head
    assert(*(char **)(next_alloc + 16) == NULL); // Next chunk should be null; We haven't free'd other similar sized chunks

    KV_alloc_pool_free(pool);
}

void test_alloc_class()
{
    struct KV_alloc_pool *pool = KV_alloc_pool_init(MIN_ALLOCATION_POOL_SIZE, false);
    // int i = 1;
    struct V
    {
        int size, alloc_class;
    } vals[] = {
        {16, 0},
        {24, 1},
        {32, 2},
        {40, 3},
        {48, 4},
        {56, 5},
        {64, 6},
        {72, 7},
        {80, 8},
        {88, 9},
        {96, 10},
        {104, 11},
        {112, 12},
        {120, 13},
        {128, 14},
        {136, 15},
        {144, 16},
        {152, 17},
        {160, 18},
        {168, 19},
        {176, 20},
        {184, 21},
        {192, 22},
        {200, 23},
        {208, 24},
        {216, 25},
        {224, 26},
        {232, 27},
        {240, 28},
        {248, 29},
        {256, 30},
        {264, 31},
        {272, -1},
        {280, -1}}; // Out of bound for 32 allocation classes; Would be allocated using mmap

    for (size_t i = 0; i < (sizeof(vals) / sizeof(struct V)); i++)
    {
        int alloc_class = get_alloc_class(vals[i].size);
#if TEST_ALLOC_DEBUG_VERBOSE
        printf("Checking size=%i for allocation class=%i\n", vals[i].size, vals[i].alloc_class);
#endif
        assert(alloc_class == vals[i].alloc_class);
        assert(get_freelist_item(pool, vals[i].alloc_class) == NULL);
    }
    KV_alloc_pool_free(pool);
}

void test_min_ensure_pointer_links_allocd()
{
    int alloc_num = 10;
    struct KV_alloc_pool *pool = KV_alloc_pool_init(MIN_ALLOCATION_POOL_SIZE, false);
    size_t alloc_size = 8;
    char *alloc[alloc_num];

    for (size_t i = 0; i < alloc_num; i++)
    {
        alloc[i] = (char *)KV_malloc(pool, alloc_size);
    }

    for (size_t i = 0; i < alloc_num; i++)
    {
        KV_free(pool, alloc[i]);
    }

    const char *item = get_freelist_item(pool, 0);

    for (size_t i = 0; i < (alloc_num - 1); i++)
    {
#if TEST_ALLOC_DEBUG_VERBOSE
        printf("Checking allocation index=%zu\n", i);
#endif
        assert(*(char **)(item + 8) != NULL); // Next chunk in freelist should not be null as we free'd <alloc_num> chunks of the same class
        item = *(char **)(item + 8);
    }

#if TEST_ALLOC_DEBUG_VERBOSE
    printf("Last allocation index=%zu\n", i);
#endif
    assert(*(char **)(item + 8) == NULL); // Next chunk should be null since this is the last chunk
    KV_alloc_pool_free(pool);
}

void test_multiple_pool_allocs_stats() {
    int alloc_num = 10;
    struct KV_alloc_pool *pool = KV_alloc_pool_init(MIN_ALLOCATION_POOL_SIZE, false);
    size_t alloc_size = 8;
    char *alloc[alloc_num];

    for (size_t i = 0; i < alloc_num; i++)
    {
        alloc[i] = (char *)KV_malloc(pool, alloc_size);
    }

    for (size_t i = 0; i < alloc_num; i++)
    {
        KV_free(pool, alloc[i]);
    }

    KV_alloc_pool_free(pool);

    pool = KV_alloc_pool_init(MIN_ALLOCATION_POOL_SIZE, false);
    KV_alloc_pool_free(pool);
}

int main(int argc, char *argv[])
{
    test_KV_alloc_pool_init();
    test_KV_malloc();
    test_KV_free();
    test_KV_freelist_class_linked_list();
    test_alloc_class();
    test_min_ensure_pointer_links_allocd();
    test_multiple_pool_allocs_stats();
    return 0;
}
