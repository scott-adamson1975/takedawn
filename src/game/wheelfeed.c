/*
 * wheelfeed.c - the racing wheel's spring told what the car is doing: the
 * toolkit's wheel glue (src/input/wheel_sdl.c) asks, each poll, whether a race
 * is running and how fast the car goes, and scales the spring by it
 * (RECOMP_WHEEL_FFB_SPEED, RECOMP_WHEEL_FFB_RACE_ONLY; docs/controls.md). The
 * numbers are carstate.c's snapshot; the toolkit knows nothing of Burnout.
 *
 * "Racing" is a snapshot no older than FEED_AGE_MS: the HUD's update, which
 * samples the car, stops in a pause, a menu or the results, so the spring
 * fades out there when RECOMP_WHEEL_FFB_RACE_ONLY is on.
 */
#include <math.h>
#include <stdlib.h>
#include <stdint.h>

#include "platform/recomp_env.h"
#include "wheel_pad.h"

#include "carstate.h"
#include "controls.h"

#define FEED_AGE_MS 300u

static int car_feed(double *speed)
{
    bo3_carstate cs;

    if (!bo3_carstate_get(&cs, FEED_AGE_MS))
        return 0;
    *speed = fabs((double)cs.speed);
    return 1;
}

/* How much of the spring the front wheels have: 0 in the air, and less in a drift, so that the wheel goes light as the
 * front slips (BO3_WHEEL_AIR and BO3_WHEEL_DRIFT: the percent of the spring left, defaults 0 and 15; 100 leaves it). The angle
 * between where the car points and where it travels: nothing under 12 degrees, the whole drift value from 35. */
static double grip_feed(double *kick_side, double *kick)
{
    static double air = -1.0, drift = -1.0;
    static uint32_t last_impact;
    bo3_carstate cs;
    double g = 1.0, a;

    if (air < 0.0) {
        const char *e = getenv("BO3_WHEEL_AIR"), *d = getenv("BO3_WHEEL_DRIFT");

        air = e ? atof(e) / 100.0 : 0.0;
        drift = d ? atof(d) / 100.0 : 0.15;
        air = air < 0.0 ? 0.0 : air > 1.0 ? 1.0 : air;
        drift = drift < 0.0 ? 0.0 : drift > 1.0 ? 1.0 : drift;
    }
    if (!bo3_carstate_get(&cs, FEED_AGE_MS))
        return 1.0;
    if (cs.impact_count != last_impact) {
        /* A hit (once): the side it pushes the car to and its strength. The strength is an event size, not a measurement (a
         * run of records of one hit, peaks of 10 000 to 24 000 in a recording), so it is compressed and capped; a hit
         * along the car (head on or from behind) has little to push the wheel sideways with. */
        last_impact = cs.impact_count;
        if (cs.impact_strength > 0.0f) {
            double len = sqrt((double)cs.impact_dir[0] * cs.impact_dir[0] + (double)cs.impact_dir[1] * cs.impact_dir[1]
                              + (double)cs.impact_dir[2] * cs.impact_dir[2]);
            double x = len > 0.01 ? (double)cs.impact_dir[0] / len : 0.0;
            double k = (double)cs.impact_strength / 20000.0;

            k = k > 1.0 ? 1.0 : pow(k, 0.6);
            *kick = k * (0.25 + 0.75 * fabs(x));
            *kick_side = x < 0.0 ? -1.0 : 1.0;          /* the car's x: right; the direction points into the car */
        }
    }
    if (cs.airborne)
        g = air;
    a = (double)cs.slip_angle * 57.29578;
    if (a > 12.0 && a < 120.0) {                /* past 120 degrees it is reversing, not drifting */
        double t = (a - 12.0) / (35.0 - 12.0);

        t = t > 1.0 ? 1.0 : t;
        if (1.0 - t * (1.0 - drift) < g)
            g = 1.0 - t * (1.0 - drift);
    }
    return g;
}

void bo3_wheel_feed_start(void)
{
    if (recomp_env_double(RENV_INPUT_WHEEL_FFB_SPEED, 0.0) <= 0 && !recomp_env_on(RENV_INPUT_WHEEL_FFB_RACE_ONLY))
        return;
    bo3_carstate_request();                 /* before main.c's drive_log_start */
    xbox_InputWheelSetCarSource(car_feed);
    xbox_InputWheelSetGripSource(grip_feed);
}
