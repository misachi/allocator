#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <assert.h>
#include <time.h>

#include "alloc.h"

const int64_t alloc_num = 100000000;
const int alloc_size = 24;
const int num_threads = 8;

static int random0(int min, int max)
{
    return min + rand() / (RAND_MAX / (max - min + 1) + 1);
}

void init_sizes()
{
    srand(time(NULL));
}

static __attribute__((noinline)) int pool_alloc_free(void *arg)
{
    for (size_t i = 0; i < alloc_num; i++)
    {
        char *alloc = KV_malloc((struct KV_alloc_pool *)arg, alloc_size);
        assert(alloc != NULL);
        KV_free((struct KV_alloc_pool *)arg, alloc);
    }
    return EXIT_SUCCESS;
}

static __attribute__((noinline)) int pool_alloc_free_rand_size(void *arg)
{
    int alloc_size = random0(8, 256);
    for (size_t i = 0; i < alloc_num; i++)
    {
        char *alloc = KV_malloc((struct KV_alloc_pool *)arg, alloc_size);
        assert(alloc != NULL);
        KV_free((struct KV_alloc_pool *)arg, alloc);
    }
    return EXIT_SUCCESS;
}

static __attribute__((noinline)) int pool_malloc_free(void *arg ALLOC_UNUSED)
{
    for (size_t i = 0; i < alloc_num; i++)
    {
        char *alloc = malloc(alloc_size);
        assert(alloc != NULL); // We touch alloc to ensure actual memory allocation. Without this we only get a promise from the system -- giving the undesired effect of very fast allocations
        free(alloc);
    }
    return EXIT_SUCCESS;
}

static __attribute__((noinline)) int pool_malloc_free_rand_size(void *arg ALLOC_UNUSED)
{
    int alloc_size = random0(8, 256);
    for (size_t i = 0; i < alloc_num; i++)
    {
        char *alloc = malloc(alloc_size);
        assert(alloc != NULL);
        free(alloc);
    }
    return EXIT_SUCCESS;
}

// #if defined(__linux__)
void bench_pool_allocs_same_alloc_size_single_thread()
{
    clock_t start, end;
    double cpu_time_used;
    size_t size = 2224154624;
    struct KV_alloc_pool *pool = KV_alloc_pool_init(size, false);

    start = clock();
    pool_alloc_free(pool);
    end = clock();

    KV_alloc_pool_free(pool);

    cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;

    printf("%s => %f seconds %f MB/s\n", __FUNCTION__, cpu_time_used, (((alloc_num * alloc_size) / (1024 * 1024)) / cpu_time_used));
}

void bench_pool_allocs_same_alloc_size_multiple_threads_shared_pool()
{
    clock_t start, end;
    double cpu_time_used;
    size_t size = 2224154624;
    struct KV_alloc_pool *pool = KV_alloc_pool_init(size, true);
    thrd_t threads[num_threads];

    start = clock();
    for (size_t i = 0; i < num_threads; i++)
    {
        thrd_create(&threads[i], pool_alloc_free, (void *)pool);
    }

    for (size_t i = 0; i < num_threads; i++)
    {
        thrd_join(threads[i], NULL);
    }

    end = clock();

    KV_alloc_pool_free(pool);

    cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;

    printf("%s => %f seconds %f MB/s\n", __FUNCTION__, cpu_time_used, (((alloc_num * alloc_size * num_threads) / (1024 * 1024)) / cpu_time_used));
}

void bench_pool_allocs_random_size_multiple_threads_shared_pool()
{
    clock_t start, end;
    double cpu_time_used;
    size_t size = 2224154624;
    struct KV_alloc_pool *pool = KV_alloc_pool_init(size, true);
    thrd_t threads[num_threads];
    // struct alloc_block res;
    // res.pool = pool;

    start = clock();
    for (size_t i = 0; i < num_threads; i++)
    {
        thrd_create(&threads[i], pool_alloc_free_rand_size, (void *)pool);
    }

    for (size_t i = 0; i < num_threads; i++)
    {
        thrd_join(threads[i], NULL);
    }

    end = clock();

    KV_alloc_pool_free(pool);

    cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;

    printf("%s => %f seconds %f MB/s\n", __FUNCTION__, cpu_time_used, (((alloc_num * alloc_size * num_threads) / (1024 * 1024)) / cpu_time_used));
}

void bench_pool_allocs_multiple_threads_local_pool()
{
    clock_t start, end;
    double cpu_time_used;
    // int num_threads = 3;
    size_t size = 2224154624/num_threads;
    struct KV_alloc_pool *pools[num_threads];

    for (size_t i = 0; i < num_threads; i++)
    {
        pools[i] = KV_alloc_pool_init(size, false);
    }

    thrd_t threads[num_threads];

    start = clock();
    for (size_t i = 0; i < num_threads; i++)
    {
        thrd_create(&threads[i], pool_alloc_free_rand_size, (void *)pools[i]);
    }

    for (size_t i = 0; i < num_threads; i++)
    {
        thrd_join(threads[i], NULL);
    }

    end = clock();

    for (size_t i = 0; i < num_threads; i++)
    {
        KV_alloc_pool_free(pools[i]);
    }

    cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;

    printf("%s => %f seconds %f MB/s\n", __FUNCTION__, cpu_time_used, (((alloc_num * alloc_size * num_threads) / (1024 * 1024)) / cpu_time_used));
}

void bench_malloc_same_alloc_size_single_thread()
{
    clock_t start, end;
    double cpu_time_used;

    start = clock();
    pool_malloc_free(NULL);
    end = clock();

    cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;

    printf("%s => %f seconds %f MB/s\n", __FUNCTION__, cpu_time_used, (((alloc_num * alloc_size) / (1024 * 1024)) / cpu_time_used));
}

void bench_malloc_same_alloc_size_multiple_threads()
{
    clock_t start, end;
    double cpu_time_used;
    thrd_t threads[num_threads];

    start = clock();
    for (size_t i = 0; i < num_threads; i++)
    {
        thrd_create(&threads[i], pool_malloc_free, NULL);
    }

    for (size_t i = 0; i < num_threads; i++)
    {
        thrd_join(threads[i], NULL);
    }
    end = clock();

    cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;

    printf("%s => %f seconds %f MB/s\n", __FUNCTION__, cpu_time_used, (((alloc_num * alloc_size * num_threads) / (1024 * 1024)) / cpu_time_used));
}

void bench_malloc_random_size_multiple_threads()
{
    clock_t start, end;
    double cpu_time_used;
    thrd_t threads[num_threads];
    // struct alloc_block res;

    start = clock();
    for (size_t i = 0; i < num_threads; i++)
    {
        thrd_create(&threads[i], pool_malloc_free_rand_size, NULL);
    }

    for (size_t i = 0; i < num_threads; i++)
    {
        thrd_join(threads[i], NULL);
    }
    end = clock();

    cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;

    printf("%s => %f seconds %f MB/s\n", __FUNCTION__, cpu_time_used, (((alloc_num * alloc_size * num_threads) / (1024 * 1024)) / cpu_time_used));
}
// #endif

int main(int argc ALLOC_UNUSED, char *argv[] ALLOC_UNUSED)
{
// #if defined(__linux__)
#if CONCURRENT_ACCESS
    init_sizes();
    printf("==============================MULTITHREADED==================================\n");
    printf("    **************************SHARED POOL***************************\n");
    bench_pool_allocs_same_alloc_size_multiple_threads_shared_pool();
    bench_pool_allocs_random_size_multiple_threads_shared_pool();
    printf("    **************************LOCAL POOL***************************\n");
    bench_pool_allocs_multiple_threads_local_pool();
    printf("=============================================================================\n");
    bench_malloc_same_alloc_size_multiple_threads();
    bench_malloc_random_size_multiple_threads();
    printf("=============================================================================\n\n");
#else
    printf("==============================SINGLETHREADED=================================\n");
    bench_malloc_same_alloc_size_single_thread();
    bench_pool_allocs_same_alloc_size_single_thread();
    bench_malloc_same_alloc_size_single_thread();
    bench_pool_allocs_multiple_threads_local_pool();
    printf("=============================================================================\n\n");
#endif // CONCURRENT_ACCESS
// #endif
    return EXIT_SUCCESS;
}