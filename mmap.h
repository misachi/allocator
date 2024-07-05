#ifndef _MMAP_H
#define _MMAP_H

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#if defined(__linux__)
#include <sys/mman.h>
#elif defined(_WIN32)

#include <io.h>
#include <windows.h>

#define MAP_FILE 0
#define MAP_SHARED 1
#define MAP_PRIVATE 2
#define MAP_FAILED ((void *)-1)
#define MAP_ANONYMOUS 4
#define MAP_ANON MAP_ANONYMOUS

#define PROT_READ 0
#define PROT_WRITE 4

typedef int64_t win_off_t;

// This is not a complete implementation equivalent for mmap on linux
// It only works for shared anonymously mapped memory. If something beyond this is required, I'm sure
// there are other better libraries that can be used
void *mmap(void *addr, size_t len, int prot, int flags, int fd, win_off_t off)
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

int munmap(void *addr, size_t len)
{
    BOOL ret = UnmapViewOfFile((LPCVOID)addr);
    if (ret == 0)
    {
        return -1;
    }
    return 0;
}

#endif

#endif // _MMAP_H