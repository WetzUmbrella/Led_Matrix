/**
 * @file    module.h
 * @brief   One-line purpose of the module (replace).
 *
 * Owner: <name> (Buddy <n>). Plan task: T-Bn-nn.
 * Template from docs/templates (Barr-C:2018, see docs/coding_standard.md).
 * Rename "module" and "MODULE" to the real module name in every line.
 */
#ifndef MODULE_H
#define MODULE_H

#include <stdbool.h>
#include <stdint.h>

/** Example constant: say where the number comes from (PostTri row). */
#define MODULE_LIMIT 10u

/** Example public type: named via typedef, module prefix, ends in _t. */
typedef struct
{
    uint32_t count;  /**< What the field holds, with units. */
    bool     b_full; /**< Boolean names start with b_. */
} module_state_t;

/**
 * @brief  Reset the module state.
 * @param  p_state  State to reset; must not be NULL.
 */
void module_init (module_state_t * p_state);

/**
 * @brief  Add one to the count, saturating at MODULE_LIMIT.
 * @param  p_state  State to update; must not be NULL.
 * @return true if the count is now at MODULE_LIMIT.
 */
bool module_step (module_state_t * p_state);

#endif /* MODULE_H */

/*** end of file ***/
