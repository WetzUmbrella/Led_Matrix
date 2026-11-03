/**
 * @file    main.c
 * @brief   Pico W coordinator entry point (T-B2-02): stdio, the cyw43
 *          driver in poll mode, then coordinator_loop_run(), which never
 *          returns (plan section 0.1: "main.c reduced to stdio_init_all(),
 *          cyw43_arch_init() and coordinator_loop_run()").
 *
 * Owner: Benjamin (Buddy 2). Implemented by Ahsanul (B4) for Benjamin.
 * The prototype's own access point (USE_ROUTER 0) and its DHCP server are
 * removed: the Pico always joins the dedicated router (VA-2.1; the Pico
 * access point is out of scope, OUT-3).
 *
 * DEVIATION D-2 (module-wide): Pico SDK names, and int main (void), which
 * the C standard fixes.
 */
#include "main.h"

#include <stdio.h>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "coordinator_loop.h"

/**
 * @brief  Start stdio and the cyw43 driver, then run the coordinator.
 * @return Never returns while the driver started; 1 when it failed.
 */
int main (void) /* DEVIATION D-2: the C standard fixes int main (void). */
{
    int status = 1; /* DEVIATION D-2: main returns int. */

    stdio_init_all();

    if (0 == cyw43_arch_init())
    {
        coordinator_loop_run();
        status = 0;
    }
    else
    {
        printf("COORDINATOR FAULT: WiFi chip init failed\n");
    }

    return status;
}

/*** end of file ***/
