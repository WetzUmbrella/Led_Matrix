/**
 * @file    main.c
 * @brief   pico_tap: a spare Pico W as a timestamping edge recorder for the
 *          skew tests (bench plan 5 Nov 2026, section 2.2).
 *
 * Owner: Ahsanul (Buddy 4). Bench tool, not shipped firmware.
 *
 * Wiring: tile test pins (GPIO4 on each tile) to GP2 (channel 0), GP3
 * (channel 1), GP4 (channel 2), GP5 (channel 3) and GP6 (channel 4); every
 * tile GND joined to a Pico GND. Inputs are 3.3 V only, like the tiles.
 * Unused inputs are pulled down, so they read 0.
 *
 * Output over USB serial, one line per edge on any channel:
 *   T,<t_us>,<mask>
 * t_us is time_us_32() at the interrupt (wraps every 71.6 min; the PC
 * script unwraps it); mask bit i is the level of channel i just after the
 * edge. Comment lines start with '#': a banner every 5 s and a statistics
 * line with events= and dropped=. pc/tools/pico_tap_capture.py turns the
 * stream into the Saleae-format CSV that pc/tools/skew_analyse.py reads.
 *
 * Accuracy: every channel is timestamped by the same interrupt from the
 * same timer, so the interrupt latency (a few microseconds) is common to
 * both edges of a pair and the skew error is about 1 to 2 us. Two edges on
 * different channels within that time can share one event; each event
 * reads all levels, so no change is lost while the ring has room.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "tap_ring.h"

/** First input pin (channel 0). */
#define TAP_FIRST_PIN 2u

/** Number of channels: GP2 to GP6. */
#define TAP_CHANNELS 5u

/** Levels of the channels within gpio_get_all() after the shift. */
#define TAP_CHANNEL_MASK ((1u << TAP_CHANNELS) - 1u)

/** Banner and statistics period. */
#define TAP_STATS_PERIOD_US 5000000u

static tap_ring_t g_ring;

static void tap_irq (uint gpio, uint32_t events);
static void tap_print_stats (uint32_t printed);

/**
 * @brief  Set up the inputs and the interrupt, then print every edge.
 * @return Never returns.
 */
int main (void) /* DEVIATION D-2: the C standard fixes int main (void). */
{
    uint32_t printed  = 0u;
    uint32_t next_us  = 0u;
    bool     b_irq_on = false;

    (void)stdio_init_all(); /* USB serial; the result is not needed. */
    tap_ring_init(&g_ring);

    for (uint32_t chan = 0u; chan < TAP_CHANNELS; chan++)
    {
        uint const pin = (uint)(TAP_FIRST_PIN + chan); /* GP2 to GP6. */

        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_down(pin);

        if (!b_irq_on)
        {
            /* The first pin installs the shared callback. */
            gpio_set_irq_enabled_with_callback(
                pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &tap_irq);
            b_irq_on = true;
        }
        else
        {
            gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
                                 true);
        }
    }

    next_us = time_us_32() + TAP_STATS_PERIOD_US;

    for (;;)
    {
        tap_event_t event;

        while (tap_ring_pop(&g_ring, &event))
        {
            printf("T,%" PRIu32 ",%" PRIu32 "\n", event.t_us, event.mask);
            printed++;
        }

        /* Wrap-aware: the difference is read as signed. */
        if ((int32_t)(time_us_32() - next_us) >= 0)
        {
            tap_print_stats(printed);
            next_us += TAP_STATS_PERIOD_US;
        }
    }
}

/**
 * @brief  GPIO interrupt for any channel: timestamp first, then read every
 *         level, then store the event.
 * @param  gpio    The pin that fired (not needed: all levels are read).
 * @param  events  The edge kinds (not needed).
 */
static void tap_irq (uint gpio, uint32_t events)
{
    uint32_t const t_us = time_us_32();
    uint32_t const mask = (gpio_get_all() >> TAP_FIRST_PIN) & TAP_CHANNEL_MASK;

    (void)gpio;   /* Every level is read below the shift. */
    (void)events; /* Both edges are recorded the same way. */
    (void)tap_ring_push(&g_ring, t_us, mask); /* A drop is counted inside. */
}

/**
 * @brief  Print the banner and the statistics comment lines.
 * @param  printed  Events printed since boot.
 */
static void tap_print_stats (uint32_t printed)
{
    printf("# pico_tap channels=%u pins=GP2-GP6 format=T,t_us,mask\n",
           TAP_CHANNELS);
    printf("# stats events=%" PRIu32 " dropped=%" PRIu32 "\n", printed,
           g_ring.dropped);
}

/*** end of file ***/
