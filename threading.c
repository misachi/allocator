#include "threading.h"

#if defined(_WIN32)
void mtx_init(mtx_t *mtx ALLOC_UNUSED, int type ALLOC_UNUSED) {
    mtx = CreateMutex(NULL, FALSE, NULL);
}

int mtx_lock(mtx_t *mtx) {
    DWORD dwWaitResult;

    dwWaitResult = WaitForSingleObject(mtx, INFINITE);

    switch (dwWaitResult)
    {
    case WAIT_OBJECT_0:
        return thrd_success;
    case WAIT_ABANDONED:
    default:
        break;
    }
    return thrd_error;
}

int mtx_unlock(mtx_t *mtx) {
    if (ReleaseMutex(mtx))
    {
        return thrd_success;
    }
    return thrd_error;
    
}

void mtx_destroy(mtx_t *mtx) {
    CloseHandle(mtx);
}

int thrd_create(thrd_t *thr, thrd_start_t func, void *arg) {
    thr = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)func, arg, 0, NULL);
    if (thr != NULL)
    {
        return thrd_success;
    }
    return thrd_error;
    
}

int thrd_join(thrd_t thr, int *res ALLOC_UNUSED) {
    DWORD dwWaitResult;

    dwWaitResult = WaitForSingleObject(thr, INFINITE);

    switch (dwWaitResult)
    {
    case WAIT_OBJECT_0:
        return thrd_success;
    case WAIT_ABANDONED:
    default:
        break;
    }
    return thrd_error;
}
#endif