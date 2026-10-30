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

void bo3_wheel_feed_start(void)
{
    if (recomp_env_double(RENV_INPUT_WHEEL_FFB_SPEED, 0.0) <= 0 && !recomp_env_on(RENV_INPUT_WHEEL_FFB_RACE_ONLY))
        return;
    bo3_carstate_request();                 /* before main.c's drive_log_start */
    xbox_InputWheelSetCarSource(car_feed);
}
