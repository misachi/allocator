CC := gcc
BUILD_ARGS := -g -O2 \
	-fsanitize=address \
	-Werror -Wall
TEST_BUILD_ARGS := -ggdb \
	-Werror -Wall

test1:
	${CC} ${TEST_BUILD_ARGS} test_alloc.c alloc.c -o test.o

# gcc -g -O2 -fsanitize=address -Werror -Wall test_alloc.c alloc.c -o test

clean:
	rm -f *.o *.gch *.exe