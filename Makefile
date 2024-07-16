CC := gcc
PREFIX := /usr/local
INCLUDEDIR := $(PREFIX)/include
LINK_TYPE := -shared
BUILD_ARGS := -g -O2 \
	-Werror -Wall -pedantic
DEBUG_BUILD := -g \
	-Werror -Wall -pedantic
TEST_BUILD_ARGS := -ggdb \
	-Werror -Wall -fsanitize=address
LIBDIR := ${PREFIX}/lib
DESTDIR := `pwd`

ifeq ($(OS),Windows_NT)
TEST_OUT := test
else
TEST_OUT := test.out
BUILD_ARGS += -fPIC
DEBUG_BUILD += -fsanitize=address
endif

VALGRIND_CMD := valgrind -s --track-origins=yes --leak-check=yes --leak-check=full --show-leak-kinds=all

ifneq ($(OS),Windows_NT)
alloc.so: alloc.o mmap.o
	mkdir -p $(DESTDIR)/bin
	${CC} ${BUILD_ARGS} ${LINK_TYPE} alloc.o mmap.o -o alloc.so
	mv $(DESTDIR)/alloc.so $(DESTDIR)/bin
else
alloc.so: alloc.o mmap.o
	${CC} ${BUILD_ARGS} ${LINK_TYPE} alloc.o mmap.o -o alloc.so
endif

debug:
	${CC} ${DEBUG_BUILD} test_alloc.c alloc.c mmap.c -o alloc.so.0

ifneq ($(OS),Windows_NT)
test1:
	${CC} ${TEST_BUILD_ARGS} test_alloc.c alloc.c mmap.c -o ${TEST_OUT}
	mkdir -p $(DESTDIR)/bin
	cp ${TEST_OUT} $(DESTDIR)/bin
	$(DESTDIR)/bin/test.out
else
test1:
	${CC} ${TEST_BUILD_ARGS} test_alloc.c alloc.c mmap.c -o ${TEST_OUT}
endif

%.o: %.c
	${CC} -fPIC -c '$<' -o '$@'


ifneq ($(OS),Windows_NT)
memcheck: test1
	${VALGRIND_CMD} ./${TEST_OUT}

install: alloc.so
	mkdir -p ${LIBDIR}
	mkdir -p ${INCLUDEDIR}
	cp $(DESTDIR)/bin/alloc.so ${LIBDIR}/liballoc.so
	chmod 755 ${LIBDIR}/liballoc.so
	cp alloc.h ${INCLUDEDIR}/alloc.h
	chmod 644 ${INCLUDEDIR}/alloc.h

uninstall:
	rm -f ${LIBDIR}/liballoc.so
	rm -f ${INCLUDEDIR}/alloc.h

endif

ifeq ($(OS),Windows_NT)
clean:
	del *.o *.exe
else
clean:
	rm -f *.o *.gch *.exe *.so *.so.* *.out
endif