#ifndef _ALLOC_H
#define _ALLOC_H

#include <stdatomic.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MAX_FREELIST_NUM_CLASSES (int)32
#define MAX_ALLOCATION_POOL_SIZE (1UL) << (64)
#define MIN_ALLOCATION_POOL_SIZE (1UL) << (20) // 1MB
#define MAX_ALLOCATION_OVERHEAD (int)16
#define MAX_ALLOCATION_POOLS_NUM (int)10
#define ALLOCATION_CLASSES_INCR_SIZE (int)8
#define MIN_ALLOCATION_CLASS_SIZE (int)16
#define ALLOCATION_SIZE_OVERHEAD (uint64_t)8

struct KV_alloc_pool
{
    // bool is_full;
    int size;
    // int pos;    // index to be used
    int offset;
    char *data; // Base address of memory
    // struct KV_alloc_pool *prev;
    // struct KV_alloc_pool *next;
};

struct KV_alloc_freelist
{
    uint8_t lock[MAX_FREELIST_NUM_CLASSES];
    char *freelist[MAX_FREELIST_NUM_CLASSES];
};

struct KV_alloc_pool *KV_alloc_pool_init(size_t size);
void KV_alloc_pool_free(struct KV_alloc_pool *pool);
void *KV_malloc(struct KV_alloc_pool *pool, size_t size);
void KV_free(struct KV_alloc_pool *pool, void *ptr);
const char* get_freelist_item(int idx);

void memory_barrier(void);

void alloc_lock(struct KV_alloc_freelist* alloc, int n);
void alloc_unlock(struct KV_alloc_freelist* alloc, int n);

#endif // _ALLOC_H
