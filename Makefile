CC := gcc
PREFIX := /usr/local
INCLUDEDIR := $(PREFIX)/include
LINK_TYPE := -shared
BUILD_ARGS := -g -O2 \
	-Werror -Wall -pedantic ${LINK_TYPE}
TEST_BUILD_ARGS := -ggdb \
	-Werror -Wall
LIBDIR := ${PREFIX}/lib

ifeq ($(OS),Windows_NT)
TEST_OUT := test
else
TEST_OUT := test.o
BUILD_ARGS += -fsanitize=address -fPIC
endif

VALGRIND_CMD := valgrind -s --track-origins=yes --leak-check=yes --leak-check=full --show-leak-kinds=all

alloc.so:
	${CC} ${BUILD_ARGS} test_alloc.c alloc.c mmap.c -o alloc.so

test1:
	${CC} ${TEST_BUILD_ARGS} test_alloc.c alloc.c mmap.c -o ${TEST_OUT}


ifneq ($(OS),Windows_NT)
memcheck: test1
	${VALGRIND_CMD} ./${TEST_OUT}

install: alloc.so
	mkdir -p ${LIBDIR}
	mkdir -p ${INCLUDEDIR}
	cp alloc.so ${LIBDIR}/alloc.so
	chmod 755 ${LIBDIR}/alloc.so
	cp alloc.h ${INCLUDEDIR}/alloc.h
	chmod 644 ${INCLUDEDIR}/alloc.h

uninstall:
	rm ${LIBDIR}/alloc.so
	rm ${INCLUDEDIR}/alloc.h

endif

ifeq ($(OS),Windows_NT)
clean:
	del *.o *.exe
else
clean:
	rm -f *.o *.gch *.exe
endif