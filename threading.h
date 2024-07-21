#ifndef _ALLOC_THREADING_
#define _ALLOC_THREADING_

#if defined(__linux__)
#include <threads.h>
#elif defined(_WIN32)
#include <windows.h>

#define mtx_plain 0x0
#define mtx_timed 0x1
#define mtx_recursive 0x2

#define thrd_success 0x0
#define thrd_error 0x2
#define thrd_nomem 0x3

#define ALLOC_UNUSED __attribute__((unused))

typedef HANDLE mtx_t;
typedef HANDLE thrd_t;
typedef int (*thrd_start_t)(void *arg);
void mtx_init(mtx_t *mtx, int type);
int mtx_lock(mtx_t *mtx);
int mtx_unlock(mtx_t *mtx);
void mtx_destroy(mtx_t *mtx);
int thrd_create(thrd_t *thr, thrd_start_t func, void *arg);
int thrd_join(thrd_t thr, int *res);
#endif

#endif // _ALLOC_THREADING_