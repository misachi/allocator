CC := gcc
PREFIX := /usr/local
INCLUDEDIR := $(PREFIX)/include
LINK_TYPE := -shared
BUILD_ARGS := -O2 \
	-Werror -Wall -pedantic
DEBUG_BUILD := -g \
	-Werror -Wall -pedantic
TEST_BUILD_ARGS := -ggdb \
	-Werror -Wall
LIBDIR := $(PREFIX)/lib


ifeq ($(OS),Windows_NT)
TEST_OUT := test
else
DESTDIR := `pwd`
TEST_OUT := test.out
BUILD_ARGS += -fPIC
DEBUG_BUILD += -fsanitize=address -fPIC
endif

VALGRIND_CMD := valgrind -s --track-origins=yes --leak-check=yes --leak-check=full --show-leak-kinds=all

ifneq ($(OS),Windows_NT)
alloc.so: alloc.o mmap.o
	@mkdir -p $(DESTDIR)/build
	$(CC) $(BUILD_ARGS) $(LINK_TYPE) alloc.o mmap.o -o alloc.so
	@mv $(DESTDIR)/alloc.so $(DESTDIR)/build
else
alloc.so: alloc.o mmap.o
	$(CC) $(BUILD_ARGS) $(LINK_TYPE) alloc.o mmap.o -o alloc.so
endif

debug: alloc.o mmap.o
	@mkdir -p $(DESTDIR)/build
	$(CC) $(DEBUG_BUILD) $(LINK_TYPE) alloc.o mmap.o -o alloc.so.0
	@mv $(DESTDIR)/alloc.so.0 $(DESTDIR)/build

ifneq ($(OS),Windows_NT)
test1:
	$(CC) $(TEST_BUILD_ARGS) -fsanitize=address test_alloc.c alloc.c mmap.c -o $(TEST_OUT)
	@mkdir -p $(DESTDIR)/build/bin
	@cp $(TEST_OUT) $(DESTDIR)/build/bin
	@$(DESTDIR)/build/bin/$(TEST_OUT)
	@rm $(TEST_OUT)
else
test1:
	$(CC) $(TEST_BUILD_ARGS) test_alloc.c alloc.c mmap.c -o $(TEST_OUT)
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
	@chmod 644 $(INCLUDEDIR)/alloc.h

uninstall:
	@rm -f $(LIBDIR)/liballoc.so
	@rm -f $(INCLUDEDIR)/alloc.h

endif

ifeq ($(OS),Windows_NT)
clean:
	del *.o *.exe
else
clean:
	@rm -f *.o *.gch *.exe *.so *.so.* *.out
endif