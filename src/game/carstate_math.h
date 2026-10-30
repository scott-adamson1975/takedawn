/*
 * carstate_math.h - the car's state as the motion platform and the wheel's force
 * feedback want it, from what the title keeps of player 1's car
 * (docs/wheel-force-feedback-proposal.md, "Findings"). Pure: no guest, no host
 * calls; tests/carstate checks it, and replays a BO3_TELEMETRY recording when
 * BO3_CARSTATE_REPLAY names one.
 *
 * What the title stores (offsets in the CB3PlayerCar, 0x73A1D0 for player 1):
 *   +0x64              the speed, m/s, a MAGNITUDE (it stays >= 0 while the car reverses at 25 m/s);
 *                      the wheel spin's sign gives the direction (about -8 rad/s reversing, +170 forward)
 *   +0x25D0, +0x25D8   angular velocity about the car's axes, radians per step: pitch
 *                      (positive: nose DOWN, r = -0.93 against the matrix) and roll
 *                      (positive: right side DOWN: matrix row 0 is the car's LEFT, r = -0.67
 *                      against the sideways acceleration; the stored roll follows row 0's side
 *                      rising, r = 0.60). +0x25D4 is not the yaw rate: it
 *                      stays near 0 through ordinary cornering (only crash spins move it)
 *   +0x30, +0x38       the matrix's forward axis (x, z): the yaw rate is its heading's change
 *   +0x578..+0x584     probably wheel spin, rad/s, one slot a wheel
 *   +0x40              the position (world metres, the matrix's + 0x30)
 *
 * What this derives, with the conventions its users get (right-handed reads of
 * a driver's body: positive is forward, right, up, nose up, right turn):
 *   surge     forward acceleration, m/s^2, from the change in speed
 *   sway      acceleration toward the car's right, m/s^2: speed * yaw rate (the
 *             centripetal part; a car turning right accelerates right, the
 *             driver is pushed left)
 *   vel_up    the car's vertical velocity (world, m/s), accel_up its change
 *   yaw_rate  rad/s, positive turning right (checked against the steering: r = +0.56)
 *   pitch_rate  rad/s, positive nose up; roll_rate  rad/s, positive right side down. In a
 *   grip corner the body leans the real way: the outside down (a right turn: left side down)
 *
 * Derived values come from low-pass filters, so a frame the title did not
 * move the car on (about one in ten) does not show as a spike. A gap in
 * time, a jump in position (a respawn, a reset) or a change of phase starts
 * the filters again: no acceleration is reported for that sample.
 */
#ifndef BO3_CARSTATE_MATH_H
#define BO3_CARSTATE_MATH_H

#include <math.h>
#include <stdint.h>
#include <string.h>

#define BO3_CS_STEP_HZ      60.0f    /* the title's physics steps a second: the stored rates are per step */
#define BO3_CS_TAU_SURGE    0.05f    /* filter time constants, seconds */
#define BO3_CS_TAU_SWAY     0.03f
#define BO3_CS_TAU_VUP      0.03f
#define BO3_CS_TAU_YAW      0.03f
#define BO3_CS_MAX_YAW      12.0f    /* rad/s: a tumbling car's heading is not a yaw rate */
#define BO3_CS_TAU_AUP      0.08f
#define BO3_CS_MAX_GAP      0.10f    /* a longer gap in the race time restarts the filters */
#define BO3_CS_MAX_SPEED    150.0f   /* a position change faster than this, m/s, is a jump */
#define BO3_CS_WHEEL_R      0.35f    /* m: a wheel's spin * this is the ground speed it would give */
#define BO3_CS_GRAVITY      9.81f
#define BO3_CS_REVERSE_SPIN (-1.5f)  /* rad/s: the wheels' mean spin below this is a car going backwards */

/* What the title holds of the car at one sample. */
typedef struct bo3_cs_raw {
    float time;             /* the car's race time, s */
    uint32_t phase;         /* 1 before the GO, 2 in play, 3 finished */
    float speed;            /* +0x64 */
    float pos[3];           /* world position */
    float fwd[2];           /* the matrix's forward axis, x and z */
    float pitch, roll;      /* +0x25D0, +0x25D8: per step, as stored */
    float spin[4];          /* +0x578..+0x584 */
} bo3_cs_raw;

/* What comes out. accel_ok is 0 while the filters are starting: surge, sway
 * and accel_up read 0 then. */
typedef struct bo3_cs_out {
    float speed;
    float yaw_rate, pitch_rate, roll_rate;     /* rad/s */
    float surge, sway, vel_up, accel_up;       /* m/s^2, m/s */
    float spin[4];
    float slip;                                /* mean wheel spin * radius against speed, as a fraction of it: 0 gripping, > 0 driven past it */
    int accel_ok;
} bo3_cs_out;

typedef struct bo3_cs_filter {
    int have;
    float time, speed, pos[3];
    float vup_raw;          /* the last unfiltered vertical velocity */
    int have_vup;
    float heading;          /* atan2(fwd x, fwd z) at the last sample */
    float surge, sway, vup, aup, yaw;
} bo3_cs_filter;

static inline float bo3_cs_ema(float prev, float x, float dt, float tau)
{
    float a = 1.0f - expf(-dt / tau);

    return prev + a * (x - prev);
}

/* One sample in: the filters advance, *o is filled. Returns accel_ok. */
static inline int bo3_cs_step(bo3_cs_filter *f, const bo3_cs_raw *r, bo3_cs_out *o)
{
    float sp = r->speed, dt = r->time - f->time, vup, dx, dy, dz, jump, h = atan2f(r->fwd[0], r->fwd[1]), dh, yaw_raw = 0;
    int i, ok;

    memset(o, 0, sizeof *o);
    if ((r->spin[0] + r->spin[1] + r->spin[2] + r->spin[3]) * 0.25f < BO3_CS_REVERSE_SPIN)
        sp = -sp;                               /* the title's speed is a magnitude */
    o->speed = sp;
    o->pitch_rate = -r->pitch * BO3_CS_STEP_HZ;
    o->roll_rate = r->roll * BO3_CS_STEP_HZ;
    for (i = 0; i < 4; i++)
        o->spin[i] = r->spin[i];
    if (sp > 0.5f || sp < -0.5f) {
        float s = fabsf(r->spin[0] + r->spin[1] + r->spin[2] + r->spin[3]) * 0.25f * BO3_CS_WHEEL_R;

        o->slip = (s - (sp < 0 ? -sp : sp)) / (sp < 0 ? -sp : sp);
    }
    ok = f->have && r->phase == 2 && dt > 1e-4f && dt <= BO3_CS_MAX_GAP;
    if (ok) {
        dx = r->pos[0] - f->pos[0];
        dy = r->pos[1] - f->pos[1];
        dz = r->pos[2] - f->pos[2];
        jump = sqrtf(dx * dx + dy * dy + dz * dz) / dt;
        if (!(jump <= BO3_CS_MAX_SPEED))
            ok = 0;
    }
    if (!ok) {
        /* (Re)start from this sample. */
        f->surge = f->sway = f->vup = f->aup = f->yaw = 0;
        f->have_vup = 0;
    } else {
        vup = dy / dt;
        dh = h - f->heading;                    /* the heading falls as the car turns right */
        while (dh > 3.14159265f) dh -= 6.2831853f;
        while (dh < -3.14159265f) dh += 6.2831853f;
        yaw_raw = -dh / dt;
        if (yaw_raw > BO3_CS_MAX_YAW) yaw_raw = BO3_CS_MAX_YAW;
        if (yaw_raw < -BO3_CS_MAX_YAW) yaw_raw = -BO3_CS_MAX_YAW;
        f->yaw = bo3_cs_ema(f->yaw, yaw_raw, dt, BO3_CS_TAU_YAW);
        f->surge = bo3_cs_ema(f->surge, (sp - f->speed) / dt, dt, BO3_CS_TAU_SURGE);
        f->sway = bo3_cs_ema(f->sway, sp * f->yaw, dt, BO3_CS_TAU_SWAY);
        f->vup = bo3_cs_ema(f->vup, vup, dt, BO3_CS_TAU_VUP);
        if (f->have_vup)
            f->aup = bo3_cs_ema(f->aup, (vup - f->vup_raw) / dt, dt, BO3_CS_TAU_AUP);
        f->vup_raw = vup;
        f->have_vup = 1;
    }
    f->have = 1;
    f->heading = h;
    f->time = r->time;
    f->speed = sp;
    memcpy(f->pos, r->pos, sizeof f->pos);
    if (ok) {
        o->surge = f->surge;
        o->sway = f->sway;
        o->yaw_rate = f->yaw;
        o->vel_up = f->vup;
        o->accel_up = f->aup;
    }
    o->accel_ok = ok;
    return ok;
}

#endif /* BO3_CARSTATE_MATH_H */
