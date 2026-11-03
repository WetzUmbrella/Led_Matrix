/**
 * @file    coordinator_port.h
 * @brief   Host test port of coordinator_loop: a fake clock and a recorder
 *          of every datagram the coordinator sends (CL_HOST_TEST build).
 *
 * Owner: Benjamin (Buddy 2). Implemented by Ahsanul (B4) for Benjamin.
 */
#ifndef COORDINATOR_PORT_H
#define COORDINATOR_PORT_H

#include <stdbool.h>
#include <stdint.h>

/** Bytes kept per recorded send (the largest Pico send is 13 B). */
#define COORDINATOR_PORT_BYTES 16u

/** Recorded sends: enough for 60 s of beacons, STATUS and commands. */
#define COORDINATOR_PORT_MAX 4096u

/** One recorded send. */
typedef struct
{
    uint64_t t_us;
    uint32_t addr;
    uint16_t port;
    uint32_t len;
    uint8_t  bytes[COORDINATOR_PORT_BYTES];
} coordinator_port_send_t;

/**
 * @brief  Current fake time (called by coordinator_loop.c).
 * @return Microseconds.
 */
uint64_t coordinator_port_now_us (void);

/**
 * @brief  Record one send (called by coordinator_loop.c).
 * @param  addr   Destination address.
 * @param  port   Destination port.
 * @param  p_buf  Bytes.
 * @param  len    Length, bytes.
 * @return true: the fake network never fails.
 */
bool coordinator_port_send (uint32_t addr, uint16_t port, uint8_t const * p_buf,
                            uint32_t len);

/**
 * @brief  Clear the recorder and set the clock.
 * @param  now_us  New time, microseconds.
 */
void coordinator_port_reset (uint64_t now_us);

/**
 * @brief  Set the clock.
 * @param  now_us  New time, microseconds.
 */
void coordinator_port_set_now (uint64_t now_us);

/**
 * @brief  Number of recorded sends.
 * @return The count.
 */
uint32_t coordinator_port_count (void);

/**
 * @brief  One recorded send.
 * @param  index  0 to coordinator_port_count() - 1.
 * @return The send (index is not checked).
 */
coordinator_port_send_t const * coordinator_port_get (uint32_t index);

#endif /* COORDINATOR_PORT_H */

/*** end of file ***/
