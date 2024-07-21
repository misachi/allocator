CC := gcc
PREFIX := /usr/local
INCLUDEDIR := $(PREFIX)/include
LINK_TYPE := -shared
BUILD_ARGS := -O3 \
	-Werror -Wall -pedantic
DEBUG_BUILD := -g \
	-Werror -Wall -pedantic -pthread
TEST_BUILD_ARGS := -ggdb \
	-Werror -Wall -fstrict-aliasing -Wstrict-aliasing -fsanitize=thread -fno-sanitize-recover=all -pthread
LIBDIR := $(PREFIX)/lib


ifeq ($(OS),Windows_NT)
TEST_OUT := test
BENCH_OUT := bench
TEST_BUILD_ARGS := -ggdb \
	-Werror -Wall -fstrict-aliasing -Wstrict-aliasing
else
DESTDIR := `pwd`
BENCH_OUT := bench.out
TEST_OUT := test.out
BUILD_ARGS += -fPIC
DEBUG_BUILD += -fsanitize=address -fPIC
endif

VALGRIND_CMD := valgrind -s --track-origins=yes --leak-check=yes --leak-check=full --show-leak-kinds=all

ifneq ($(OS),Windows_NT)
alloc.so: alloc.o mmap.o threading.o
	@mkdir -p $(DESTDIR)/build
	$(CC) $(BUILD_ARGS) $(LINK_TYPE) -pthread alloc.o mmap.o threading.o -o alloc.so
	@mv $(DESTDIR)/alloc.so $(DESTDIR)/build
else
alloc.so: alloc.o mmap.o threading.o
	$(CC) $(BUILD_ARGS) $(LINK_TYPE) alloc.o mmap.o threading.o -o alloc.so
endif

debug: alloc.o mmap.o threading.o
	@mkdir -p $(DESTDIR)/build
	$(CC) $(DEBUG_BUILD) $(LINK_TYPE) alloc.o mmap.o threading.o -o alloc.so.0
	@mv $(DESTDIR)/alloc.so.0 $(DESTDIR)/build

ifneq ($(OS),Windows_NT)
test1:
	$(CC) $(TEST_BUILD_ARGS) test_alloc.c alloc.c mmap.c threading.c -o $(TEST_OUT)
	@mkdir -p $(DESTDIR)/build/bin
	@cp $(TEST_OUT) $(DESTDIR)/build/bin
	@$(DESTDIR)/build/bin/$(TEST_OUT)
	@rm $(TEST_OUT)

bench:
	$(CC) -g -O3 -Wall -Werror -Wextra -pthread bench_alloc.c alloc.c mmap.c threading.c -o $(BENCH_OUT)
	@mkdir -p $(DESTDIR)/build/bin
	@cp $(BENCH_OUT) $(DESTDIR)/build/bin
	@$(DESTDIR)/build/bin/$(BENCH_OUT)
	@rm $(BENCH_OUT)
else
test1:
	$(CC) $(TEST_BUILD_ARGS) test_alloc.c alloc.c mmap.c threading.c -o $(TEST_OUT)
bench:
	$(CC) -g -O3 -Wall -Werror -Wextra bench_alloc.c alloc.c mmap.c threading.c -o $(BENCH_OUT)
endif

%.o: %.c
	$(CC) -fPIC -c '$<' -o '$@'


ifneq ($(OS),Windows_NT)
memcheck:
	$(CC) $(TEST_BUILD_ARGS) test_alloc.c alloc.c mmap.c -o $(TEST_OUT)
	@mkdir -p $(DESTDIR)/build/bin
	@cp $(TEST_OUT) $(DESTDIR)/build/bin
	@$(VALGRIND_CMD) $(DESTDIR)/build/bin/$(TEST_OUT)
	@rm $(TEST_OUT)

install: alloc.so
	@mkdir -p $(LIBDIR)
	@mkdir -p $(INCLUDEDIR)
	@cp $(DESTDIR)/build/alloc.so $(LIBDIR)/liballoc.so
	@chmod 755 $(LIBDIR)/liballoc.so
	@cp alloc.h $(INCLUDEDIR)/alloc.h
	@cp threading.h $(INCLUDEDIR)/threading.h
	@chmod 644 $(INCLUDEDIR)/alloc.h
	@chmod 644 $(INCLUDEDIR)/threading.h

uninstall:
	@rm -f $(LIBDIR)/liballoc.so
	@rm -f $(INCLUDEDIR)/alloc.h
	@rm -f $(INCLUDEDIR)/threading.h

endif

ifeq ($(OS),Windows_NT)
clean:
	del *.o *.gch *.exe *.so *.so.* *.out
else
clean:
	@rm -f *.o *.gch *.exe *.so *.so.* *.out
endif