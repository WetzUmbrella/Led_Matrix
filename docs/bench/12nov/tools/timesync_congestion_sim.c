/* Bench evidence, 12 Nov 2026 (not part of any build): time sync through a
 * congestion period. Build from Led_Matrix with the timesync.c/.h variant
 * to compare, for example:
 *   gcc -std=c11 -O2 -pthread -DTIMESYNC_HOST_TEST -Ipanel_sync/main \
 *       -Icommon/proto -o sim docs/bench/12nov/tools/timesync_congestion_sim.c \
 *       panel_sync/main/timesync.c
 * Results are in docs/bench/12nov/step4.md, "Time sync on Matrix". */
/* Scratch: a congestion period where beacon delay ramps from 2 ms to RAMP
 * ms over 3 s and back, once, then normal. Prints the worst corrected-clock
 * error and how long it stays over 50 ms. */
#include <stdio.h>
#include <inttypes.h>
#include "timesync.h"

static int64_t delay_at (int i, int64_t ramp_us)
{
    if ((i < 100) || (i >= 160)) return 2000;
    int const k = i - 100;                       /* 0..59: 6 s congestion */
    int64_t const up = (k < 30) ? k : (59 - k);  /* up then down */
    return 2000 + (ramp_us * up) / 30;
}

int main (void)
{
    int64_t const ramps[] = {400000, 600000, 900000};
    for (int r = 0; r < 3; r++)
    {
        timesync_reader_t reader = {0};
        timesync_init();
        int64_t worst = 0; int over50 = 0;
        for (int i = 0; i < 1500; i++)
        {
            int64_t const t_p = 1000000 + (int64_t)i * 100000;
            int64_t const t_l = t_p + 5000000 + delay_at(i, ramps[r]);
            timesync_add_sample(t_p, t_l);
            for (int k = 0; k < 6; k++) { int64_t now = t_l + k * 16667; timesync_test_set_now_us(now); timesync_tick(now); }
            int64_t const now = t_l + 99999; timesync_test_set_now_us(now);
            int64_t err = timesync_now_corrected(&reader) - (now - 5000000); if (err < 0) err = -err;
            if (err > worst) worst = err;
            if (err > 50000) over50++;
        }
        printf("ramp to %4" PRId64 " ms: worst error %4" PRId64 " ms, over 50 ms for %5.1f s, reseeds %" PRIu32 "\n",
               ramps[r] / 1000, worst / 1000, over50 / 10.0, timesync_reseed_count());
    }
    return 0;
}
