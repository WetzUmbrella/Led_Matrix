/**
 * @file    module.c
 * @brief   One-line purpose of the module (replace).
 *
 * Owner: <name> (Buddy <n>). Plan task: T-Bn-nn.
 * Template from docs/templates (Barr-C:2018, see docs/coding_standard.md).
 * Section order follows Barr-C 4.3.b: comment block, includes, types and
 * constants, static data, private prototypes, public functions, private
 * functions.
 */
#include "module.h"

#include <stddef.h>

/** Example private constant. */
#define MODULE_START 0u

/** Example static data: file-scope names start with g_. */
static uint32_t g_step_count = 0u;

static bool module_is_full (module_state_t const * p_state);

/**
 * @brief  Reset the module state.
 * @param  p_state  State to reset; must not be NULL.
 */
void module_init (module_state_t * p_state)
{
    p_state->count  = MODULE_START;
    p_state->b_full = false;
}

/**
 * @brief  Add one to the count, saturating at MODULE_LIMIT.
 * @param  p_state  State to update; must not be NULL.
 * @return true if the count is now at MODULE_LIMIT.
 */
bool module_step (module_state_t * p_state)
{
    if (!module_is_full(p_state))
    {
        p_state->count++;
    }

    g_step_count++;
    p_state->b_full = module_is_full(p_state);

    return p_state->b_full;
}

/**
 * @brief  Whether the count has reached MODULE_LIMIT.
 * @param  p_state  State to test; must not be NULL.
 * @return true at or above the limit.
 */
static bool module_is_full (module_state_t const * p_state)
{
    return (p_state->count >= MODULE_LIMIT);
}

/*** end of file ***/
