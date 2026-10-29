# Host build of the common/proto tests (T-B2-01, every message row). No SDK needed.
#   make        build and run test_proto
#   make asan   same with -fsanitize=address,undefined, run with address
#               randomisation off (see panel_sync/test/Makefile)
CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Werror -O2 -I..
SRC     = test_main.c test_proto.c test_messages.c ../proto.c
HDR     = test_main.h test_proto.h test_messages.h ../proto.h

.PHONY: run asan clean
run: test_proto
	./test_proto

test_proto: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC)

asan: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -o test_proto_asan $(SRC)
	setarch $$(uname -m) -R ./test_proto_asan

clean:
	rm -f test_proto test_proto_asan
