#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

// Generally you would define your own explicit list of lwIP options
// (see https://www.nongnu.org/lwip/2_1_x/group__lwip__opts.html)
//
// This example uses a common include to avoid repetition
// The lwIP heap the coordinator's sends are allocated from: 16 KB instead of
// the SDK examples' 4 KB, so a burst of TAPs from every tile cannot run it
// dry (bench 19 Nov 2026; the RP2040 has 264 KB of RAM).
#define MEM_SIZE 16000

// UDP pcbs: the coordinator opens one send and three receive pcbs (tiles,
// PC, and the tiles' TELEMETRY broadcast for the wall turn, D-17), besides
// DHCP's and DNS's. The default of 4 left port 4215 unopened (bench 24 Nov
// 2026: "cannot open port 4215").
#define MEMP_NUM_UDP_PCB 6

#include "lwipopts_examples_common.h"

// Enable some httpd features
#define LWIP_HTTPD_CGI 1
#define LWIP_HTTPD_SSI 1
#define LWIP_HTTPD_SSI_MULTIPART 1
#define LWIP_HTTPD_SUPPORT_POST 0
#define LWIP_HTTPD_SSI_INCLUDE_TAG 0

// Generated file containing html data
#define HTTPD_FSDATA_FILE "pico_fsdata.inc"

#endif
