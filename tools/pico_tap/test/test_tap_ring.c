/**
 * @file    test_tap_ring.c
 * @brief   Host tests of the pico_tap edge ring. Build and run:
 *          make -C tools/pico_tap/test
 *
 * Owner: Ahsanul (Buddy 4). Bench tool, not shipped firmware.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "../tap_ring.h"

#define TEST_COUNT 4u
#define WRAP_START 0xFFFFFFF0u

static tap_ring_t g_ring;
static uint32_t   g_failures = 0u;

static void check (bool b_condition, int32_t line);
static bool test_order (void);
static bool test_full_drops (void);
static bool test_index_wrap (void);
static bool test_empty (void);

/**
 * @brief  Run the four ring tests.
 * @return 0 when every test passed.
 */
int main (void) /* DEVIATION D-2: the C standard fixes int main (void). */
{
    uint32_t passed = 0u;

    passed += test_order() ? 1u : 0u;
    passed += test_full_drops() ? 1u : 0u;
    passed += test_index_wrap() ? 1u : 0u;
    passed += test_empty() ? 1u : 0u;
    printf("tap_ring: Pass = %" PRIu32 " of %u\n", passed, TEST_COUNT);

    /* DEVIATION D-2: the exit status is the int that main returns. */
    return (TEST_COUNT == passed) ? 0 : 1;
}

/**
 * @brief  Record one check; print the line when it fails.
 * @param  b_condition  The checked condition.
 * @param  line         __LINE__ of the check.
 */
static void check (bool b_condition, int32_t line)
{
    if (!b_condition)
    {
        g_failures++;
        printf("    FAIL at line %" PRId32 "\n", line);
    }
}

/**
 * @brief  Events come out in the order they went in, with their values.
 * @return true when every check passed.
 */
static bool test_order (void)
{
    uint32_t const before = g_failures;
    tap_event_t    event;

    tap_ring_init(&g_ring);
    check(tap_ring_push(&g_ring, 100u, 1u), __LINE__);
    check(tap_ring_push(&g_ring, 200u, 3u), __LINE__);
    check(tap_ring_pop(&g_ring, &event), __LINE__);
    check((100u == event.t_us) && (1u == event.mask), __LINE__);
    check(tap_ring_pop(&g_ring, &event), __LINE__);
    check((200u == event.t_us) && (3u == event.mask), __LINE__);

    return (before == g_failures);
}

/**
 * @brief  A full ring refuses and counts the extra event, keeps the stored
 *         ones, and accepts again after a pop.
 * @return true when every check passed.
 */
static bool test_full_drops (void)
{
    uint32_t const before = g_failures;
    tap_event_t    event;

    tap_ring_init(&g_ring);

    for (uint32_t idx = 0u; idx < TAP_RING_CAPACITY; idx++)
    {
        check(tap_ring_push(&g_ring, idx, 0u), __LINE__);
    }

    check(!tap_ring_push(&g_ring, 9999u, 0u), __LINE__);
    check(1u == g_ring.dropped, __LINE__);
    check(tap_ring_pop(&g_ring, &event), __LINE__);
    check(0u == event.t_us, __LINE__);
    check(tap_ring_push(&g_ring, 7777u, 0u), __LINE__);

    return (before == g_failures);
}

/**
 * @brief  The free-running head and tail wrap at 2^32 without losing or
 *         repeating an event.
 * @return true when every check passed.
 */
static bool test_index_wrap (void)
{
    uint32_t const before = g_failures;
    tap_event_t    event;

    tap_ring_init(&g_ring);
    g_ring.head = WRAP_START;
    g_ring.tail = WRAP_START;

    for (uint32_t idx = 0u; idx < 32u; idx++)
    {
        check(tap_ring_push(&g_ring, idx, idx), __LINE__);
    }

    for (uint32_t idx = 0u; idx < 32u; idx++)
    {
        check(tap_ring_pop(&g_ring, &event), __LINE__);
        check((idx == event.t_us) && (idx == event.mask), __LINE__);
    }

    check(!tap_ring_pop(&g_ring, &event), __LINE__);

    return (before == g_failures);
}

/**
 * @brief  An empty ring returns nothing and drops nothing.
 * @return true when every check passed.
 */
static bool test_empty (void)
{
    uint32_t const before = g_failures;
    tap_event_t    event;

    tap_ring_init(&g_ring);
    check(!tap_ring_pop(&g_ring, &event), __LINE__);
    check(0u == g_ring.dropped, __LINE__);

    return (before == g_failures);
}

/*** end of file ***/
