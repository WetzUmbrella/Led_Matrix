# Host build of the timesync core tests (T-B4-02). No ESP-IDF needed.
#   make        build and run test_timesync
#   make asan   same with -fsanitize=address,undefined. The binary runs with
#               address randomisation off (setarch -R): with high-entropy
#               randomisation (WSL2 kernel 6.6, some CI runners) ASan can
#               crash at start-up with AddressSanitizer:DEADLYSIGNAL before
#               any test runs.
CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Werror -O2 -DTIMESYNC_HOST_TEST -I../main
SRC     = test_timesync.c ../main/timesync.c

.PHONY: run asan clean
run: test_timesync
	./test_timesync

test_timesync: $(SRC) ../main/timesync.h
	$(CC) $(CFLAGS) -o $@ $(SRC)

asan: $(SRC)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -o test_timesync_asan $(SRC)
	setarch $$(uname -m) -R ./test_timesync_asan

clean:
	rm -f test_timesync test_timesync_asan
