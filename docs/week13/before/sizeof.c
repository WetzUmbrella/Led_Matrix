/*
 * sizeof.c: host program for T-B4-01 (cr1-before baseline, commit 45e038c).
 *
 * The two structs below are copied verbatim from panel_sync/main/panel_sync.c
 * lines 52-69 at 45e038c; MAX_NODES is copied from line 50 of the same file.
 * The program prints sizeof(beacon_t) and sizeof(hello_t).
 *
 * Build and run on the host:
 *   gcc -std=c11 -Wall -Wextra -o sizeof docs/week13/before/sizeof.c && ./sizeof
 *
 * Target-compiler check (no hardware needed, see BENCH.md step S-2):
 *   xtensa-esp32s3-elf-gcc -c -o sizeof_xtensa.o docs/week13/before/sizeof.c
 *   xtensa-esp32s3-elf-nm -S -t d sizeof_xtensa.o | grep sizeof_
 * The two arrays at the end of this file are sized by sizeof(), so the
 * symbol sizes nm prints are the target compiler's sizeof values.
 */
#include <stdio.h>
#include <stdint.h>

/* panel_sync.c:50 */
#define MAX_NODES        8

/* ---- BEGIN VERBATIM panel_sync.c:52-69 ---- */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  version;
    uint32_t seq;
    uint8_t  cmd;
    uint32_t frame_no;
    uint8_t  layout_ver;
    uint8_t  count;
    uint8_t  cols;
    uint8_t  rows;
    uint32_t ids[MAX_NODES];
} beacon_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t id;
    uint8_t  board;
} hello_t;
/* ---- END VERBATIM panel_sync.c:52-69 ---- */

/* Sized by sizeof() so a cross compiler's object file records the target value. */
const unsigned char sizeof_beacon_t[sizeof(beacon_t)] = { 0 };
const unsigned char sizeof_hello_t[sizeof(hello_t)] = { 0 };

int main(void)
{
    printf("sizeof(beacon_t) = %zu\n", sizeof(beacon_t));
    printf("sizeof(hello_t) = %zu\n", sizeof(hello_t));
    return 0;
}
