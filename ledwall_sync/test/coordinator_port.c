/**
 * @file    coordinator_port.c
 * @brief   Host test port of coordinator_loop: fake clock and send recorder.
 *
 * Owner: Benjamin (Buddy 2). Implemented by Ahsanul (B4) for Benjamin.
 */
#include "coordinator_port.h"

#include <stddef.h>
#include <string.h>

static uint64_t                g_now_us = 0u;
static coordinator_port_send_t g_sends[COORDINATOR_PORT_MAX];
static uint32_t                g_count = 0u;

/**
 * @brief  Current fake time.
 * @return Microseconds.
 */
uint64_t coordinator_port_now_us (void)
{
    return g_now_us;
}

/**
 * @brief  Record one send; sends beyond the recorder size are not kept.
 * @param  addr   Destination address.
 * @param  port   Destination port.
 * @param  p_buf  Bytes.
 * @param  len    Length, bytes.
 * @return true.
 */
bool coordinator_port_send (uint32_t addr, uint16_t port, uint8_t const * p_buf,
                            uint32_t len)
{
    if (g_count < COORDINATOR_PORT_MAX)
    {
        coordinator_port_send_t * const p_send = &g_sends[g_count];
        uint32_t const                  copy =
            (len < COORDINATOR_PORT_BYTES) ? len : COORDINATOR_PORT_BYTES;

        p_send->t_us = g_now_us;
        p_send->addr = addr;
        p_send->port = port;
        p_send->len  = len;
        (void)memcpy(p_send->bytes, p_buf, copy); /* Returns bytes. */
        g_count++;
    }

    return true;
}

/**
 * @brief  Clear the recorder and set the clock.
 * @param  now_us  New time.
 */
void coordinator_port_reset (uint64_t now_us)
{
    g_now_us = now_us;
    g_count  = 0u;
}

/**
 * @brief  Set the clock.
 * @param  now_us  New time.
 */
void coordinator_port_set_now (uint64_t now_us)
{
    g_now_us = now_us;
}

/**
 * @brief  Number of recorded sends.
 * @return The count.
 */
uint32_t coordinator_port_count (void)
{
    return g_count;
}

/**
 * @brief  One recorded send.
 * @param  index  Index.
 * @return The send.
 */
coordinator_port_send_t const * coordinator_port_get (uint32_t index)
{
    return &g_sends[index];
}

/*** end of file ***/
