# Host build of the pico_tap edge ring tests. No Pico SDK needed.
#   make        build and run
#   make asan   same with -fsanitize=address,undefined
CC     = gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -O2
SRC    = test_tap_ring.c ../tap_ring.c

.PHONY: run asan clean

run: test_tap_ring
	./test_tap_ring

test_tap_ring: $(SRC) ../tap_ring.h
	$(CC) $(CFLAGS) -o $@ $(SRC)

asan: $(SRC)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -o test_tap_ring_asan $(SRC)
	setarch $$(uname -m) -R ./test_tap_ring_asan

clean:
	rm -f test_tap_ring test_tap_ring_asan
