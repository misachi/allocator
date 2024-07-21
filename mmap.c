#include "mmap.h"

#if defined(_WIN32)

#define ALLOC_UNUSED __attribute__((unused))
// This is not a complete implementation equivalent for mmap on linux
// It only works for shared anonymously mapped memory. If something beyond this is required, I'm sure
// there are other better libraries that can be used
void *mmap(void *addr ALLOC_UNUSED, size_t len, int prot, int flags, int fd, win_off_t off)
{
    HANDLE hMapFile, hValue;
    PVOID mmap;
    DWORD dwFileOffsetLow = (DWORD)(off & 0xffffffff);
    DWORD dwFileOffsetHigh = (DWORD)((off >> 32) & 0xffffffff);

    // Read-write
    prot = prot == (PROT_READ | PROT_WRITE) ? PAGE_READWRITE : PAGE_NOACCESS;

    // Anonymous and shared
    hValue = flags == (MAP_ANONYMOUS | MAP_SHARED) && fd < 0 ? INVALID_HANDLE_VALUE : (HANDLE)_get_osfhandle(fd);

    hMapFile = CreateFileMapping(hValue, NULL, prot, 0, len, NULL);
    if (!hMapFile)
    {
        return MAP_FAILED;
    }

    mmap = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, dwFileOffsetHigh, dwFileOffsetLow, len);
    if (!mmap)
    {
        CloseHandle(hMapFile);
        return MAP_FAILED;
    }

    CloseHandle(hMapFile);
    return mmap;
}

int munmap(void *addr, size_t len ALLOC_UNUSED)
{
    BOOL ret = UnmapViewOfFile((LPCVOID)addr);
    if (ret == 0)
    {
        return -1;
    }
    return 0;
}
#endif