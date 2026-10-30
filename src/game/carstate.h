/*
 * carstate.h - player 1's car as one shared snapshot: speed, angular rates,
 * accelerations, wheel spin, the driver's inputs and the latest impact. The
 * single source for the motion platform feed (SimHub) and the wheel's force
 * feedback, so both read the same numbers (docs/wheel-force-feedback-proposal.md).
 * The maths is in carstate_math.h (pure, tested).
 *
 * It is sampled once a game step, from main.c's drive_log mark (the HUD
 * speedometer's update, so only while a race's HUD runs), after the title
 * has stepped the car and before the high refresh rate mode blends the
 * matrix for drawing. Impacts come from the title's collision entry
 * (sub_0012FA40, the "sb_impact" mark), shared with the soft-body damage.
 *
 * Reading: bo3_carstate_get() copies a consistent snapshot, from any thread
 * and without a lock. A consumer asks first (bo3_carstate_request(), before
 * main.c's drive_log_start) so that the sampler is on at all: nothing runs
 * for a build nobody reads it in.
 */
#ifndef BO3_CARSTATE_H
#define BO3_CARSTATE_H

#include <stdint.h>

typedef struct bo3_carstate {
    uint32_t flip;              /* the title's flip count at the sample */
    float race_time;            /* s */
    uint32_t phase;             /* 1 before the GO, 2 in play, 3 finished */
    float speed;                /* m/s (reversing is unverified: it has not been seen negative) */
    float yaw_rate, pitch_rate, roll_rate;     /* rad/s: turning right, nose up, right side down */
    float surge, sway;          /* m/s^2: forward, toward the car's right */
    float vel_up, accel_up;     /* world vertical, m/s and m/s^2; gravity is not in it (add 9.81 for what a body feels) */
    float slip;                 /* wheel spin past the speed, a fraction; see carstate_math.h */
    float spin[4];              /* wheel spin, rad/s */
    float steer;                /* -1 left .. 1 right, the title's pad read */
    float throttle, brake;      /* 0..1 */
    float pos[3];               /* world, m */
    int accel_ok;               /* the derived accelerations are real this sample */
    /* The latest impacts (the title's collision entry, player 1's car only):
     * impact_count rises with each one; impact_strength is the strongest
     * since the previous sample (0 when none); impact_dir is its direction
     * in the car's frame (x right, y up, z forward; pointing into the car). */
    uint32_t impact_count;
    float impact_strength;
    float impact_dir[3];
} bo3_carstate;

/* A consumer wants the snapshot. Before drive_log_start. */
void bo3_carstate_request(void);
int  bo3_carstate_wanted(void);

/* After softbody's start: registers the impact mark if wanted (a mark runs
 * only its first handler: softbody's handler calls bo3_carstate_impact too). */
void bo3_carstate_start(uint32_t (*flips)(void));

/* From main.c's drive_log mark, on the guest's thread. */
void bo3_carstate_sample(void);

/* From the collision mark, with the entry's ebp. */
void bo3_carstate_impact(uint32_t ebp);

/* A consistent copy of the latest snapshot. Returns 0 (and zeroes *out) when
 * there is none, or none within max_age_ms (0: any age): the race is not
 * running, or the HUD's update has stopped (a pause, a menu). */
int bo3_carstate_get(bo3_carstate *out, uint32_t max_age_ms);

#endif /* BO3_CARSTATE_H */
