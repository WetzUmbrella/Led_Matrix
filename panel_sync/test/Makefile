# Host build of the timesync core tests (T-B4-02) and the fault word tests
# (T-B3-06). No ESP-IDF needed.
#   make        build and run test_timesync
#   make asan   same with -fsanitize=address,undefined. The binary runs with
#               address randomisation off (setarch -R): with high-entropy
#               randomisation (WSL2 kernel 6.6, some CI runners) ASan can
#               crash at start-up with AddressSanitizer:DEADLYSIGNAL before
#               any test runs.
CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Werror -O2 -pthread -DTIMESYNC_HOST_TEST -I../main -I../../common/proto
SRC     = test_main.c test_timesync.c ../main/timesync.c \
          test_fault_word.c ../main/fault_word.c \
          test_playback.c ../main/playback_control.c \
          test_telemetry_book.c ../main/telemetry_book.c \
          test_asset_rx.c ../main/asset_rx.c \
          test_coord_lock.c ../main/coord_lock.c \
          test_display.c ../main/display_frame.c ../main/orient_filter.c \
          test_tap_detect.c ../main/tap_detect.c \
          test_game_tile.c ../main/game_tile.c ../../common/proto/proto.c

.PHONY: run asan clean
run: test_timesync
	./test_timesync

test_timesync: $(SRC) test_main.h test_timesync.h ../main/timesync.h \
               test_fault_word.h ../main/fault_word.h \
               test_playback.h ../main/playback_control.h \
               test_telemetry_book.h ../main/telemetry_book.h \
               test_asset_rx.h ../main/asset_rx.h \
               test_coord_lock.h ../main/coord_lock.h \
               test_display.h ../main/display_frame.h ../main/orient_filter.h \
               test_tap_detect.h ../main/tap_detect.h \
               test_game_tile.h ../main/game_tile.h
	$(CC) $(CFLAGS) -o $@ $(SRC)

asan: $(SRC)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -o test_timesync_asan $(SRC)
	setarch $$(uname -m) -R ./test_timesync_asan

clean:
	rm -f test_timesync test_timesync_asan
