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

void *mmap(void *addr, size_t len, int prot, int flags, int fd, win_off_t off);

int munmap(void *addr, size_t len);

#endif

#endif // _MMAP_H