# Host build of the coordinator_loop tests (T-B2-02 to T-B2-05). No Pico SDK
# needed: coordinator_loop.c is built with -DCL_HOST_TEST and the test
# supplies the clock and the send function.
#   make        build and run test_coordinator
#   make asan   same with -fsanitize=address,undefined, run with address
#               randomisation off (see panel_sync/test/Makefile)
CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Werror -O2 -DCL_HOST_TEST -I. -I.. -I../../common/proto
SRC     = test_main.c test_coord.c coordinator_port.c ../coordinator_loop.c ../../common/proto/proto.c \
          test_join_backoff.c ../join_backoff.c \
          test_ack_outbox.c ../../common/proto/ack_outbox.c \
          test_game_rules.c ../game_rules.c \
          test_score_font.c ../score_font.c \
          test_coord_game.c
HDR     = test_main.h test_coord.h coordinator_port.h ../coordinator_loop.h ../../common/proto/proto.h \
          test_join_backoff.h ../join_backoff.h \
          test_ack_outbox.h ../../common/proto/ack_outbox.h \
          test_game_rules.h ../game_rules.h \
          test_score_font.h ../score_font.h \
          test_coord_game.h

.PHONY: run asan clean
run: test_coordinator
	./test_coordinator

test_coordinator: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC)

asan: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -o test_coordinator_asan $(SRC)
	setarch $$(uname -m) -R ./test_coordinator_asan

clean:
	rm -f test_coordinator test_coordinator_asan
