/*
 * carstate.c - player 1's car as one shared snapshot (carstate.h). The maths
 * is carstate_math.h; this is the guest side: the reads, the impact mark and
 * the lock-free publication.
 *
 * The car (CB3PlayerCar, 0x73A1D0 for player 1: autopilot.c) is read at fixed
 * addresses, the pad as main.c's drive_log reads it (the first open port's
 * XINPUT_STATE, four records of 0x20 at 0x4AE20C, the state at + 8). A sample
 * counts only while the car's phase is 1, 2 or 3 (a race). The HUD's own
 * pointer to "the car" ([hud + 0x2C]) is not used: in a race it pointed at
 * 0x667E90, an object with no race time and a scaled matrix.
 *
 * Publication is a seqlock: the sampler (the guest's thread) bumps the
 * sequence to odd, writes, bumps it to even; a reader retries while the
 * sequence is odd or changed. The sampler is the only writer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>

#include "platform/recomp_patch.h"
#include "recomp_types.h"

#include "carstate.h"
#include "carstate_math.h"
#include "controls.h"

extern RECOMP_TLS uint32_t g_ebp;
extern ptrdiff_t g_xbox_mem_offset;

#define CS_M32(va) (*(const uint32_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define CS_MF(va)  (*(const float *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))

#define CS_CAR1     0x0073A1D0u     /* player 1's CB3PlayerCar */
#define CS_MATRIX   0x10u           /* the world matrix; the position is its + 0x30 */
#define CS_SPEED    0x64u
#define CS_ANG      0x25D0u         /* pitch, (yaw: unused), roll: three floats */
#define CS_SPIN     0x578u          /* four floats */
#define CS_TIME     0x10DCu
#define CS_PHASE    0x134Cu
#define CS_PADS     0x004AE20Cu

static int s_want;
static uint32_t (*s_flips)(void);
static bo3_cs_filter s_filter;
static bo3_carstate s_snap;         /* the published copy */
static volatile uint32_t s_seq;
static uint64_t s_snap_ms;          /* host clock at the last sample */

/* Impacts since the last sample (the collision mark and the sampler share the guest's thread). */
static uint32_t s_imp_count;
static float s_imp_max, s_imp_dir[3];

static uint64_t host_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

void bo3_carstate_request(void) { s_want = 1; }
int  bo3_carstate_wanted(void)  { return s_want; }

void bo3_carstate_impact(uint32_t ebp)
{
    uint32_t body, dirp;
    float strength, R[9], wd[3], ld[3];
    int i;

    if (!s_want || !g_xbox_mem_offset)
        return;
    body = CS_M32(ebp + 0x10);
    dirp = CS_M32(ebp + 0x28);
    if (body != CS_CAR1 + CS_MATRIX || dirp < 0x10000u || dirp >= 0x08000000u)
        return;
    strength = CS_MF(ebp + 0x2C);
    if (!(strength > 0))
        return;
    for (i = 0; i < 9; i++)
        R[i] = CS_MF(body + 4u * (uint32_t)(4 * (i / 3) + i % 3));
    for (i = 0; i < 3; i++)
        wd[i] = CS_MF(dirp + 4u * (uint32_t)i);
    for (i = 0; i < 3; i++)
        ld[i] = R[3 * i] * wd[0] + R[3 * i + 1] * wd[1] + R[3 * i + 2] * wd[2];
    s_imp_count++;
    if (strength > s_imp_max) {
        s_imp_max = strength;
        memcpy(s_imp_dir, ld, sizeof ld);
    }
    bo3_telemetry_impact(strength, ld);
}

static void cs_impact_mark(void)
{
    bo3_carstate_impact(g_ebp);
}

void bo3_carstate_start(uint32_t (*flips)(void))
{
    s_flips = flips;
    if (s_want)
        recomp_patch_on_mark("sb_impact", cs_impact_mark);
}

static float s_prev_pos[3], s_prev_time = -1.0f;

void bo3_carstate_sample(void)
{
    const uint32_t car = CS_CAR1;
    bo3_cs_raw r;
    bo3_cs_out o;
    bo3_carstate n;
    const uint8_t *st;
    uint32_t k;
    int i;

    if (!s_want || !g_xbox_mem_offset)
        return;
    r.phase = CS_M32(car + CS_PHASE);
    if (r.phase < 1 || r.phase > 3)
        return;
    r.time = CS_MF(car + CS_TIME);
    r.speed = CS_MF(car + CS_SPEED);
    for (i = 0; i < 3; i++)
        r.pos[i] = CS_MF(car + CS_MATRIX + 0x30u + 4u * (uint32_t)i);
    r.pitch = CS_MF(car + CS_ANG);
    r.fwd[0] = CS_MF(car + CS_MATRIX + 0x20u);
    r.fwd[1] = CS_MF(car + CS_MATRIX + 0x28u);
    r.roll = CS_MF(car + CS_ANG + 8u);
    for (i = 0; i < 4; i++)
        r.spin[i] = CS_MF(car + CS_SPIN + 4u * (uint32_t)i);
    bo3_cs_step(&s_filter, &r, &o);

    memset(&n, 0, sizeof n);
    n.flip = s_flips ? s_flips() : 0;
    n.race_time = r.time;
    n.phase = r.phase;
    n.speed = o.speed;
    n.yaw_rate = o.yaw_rate;
    n.pitch_rate = o.pitch_rate;
    n.roll_rate = o.roll_rate;
    n.surge = o.surge;
    n.sway = o.sway;
    n.vel_up = o.vel_up;
    n.accel_up = o.accel_up;
    n.slip = o.slip;
    n.accel_ok = o.accel_ok;
    memcpy(n.spin, o.spin, sizeof n.spin);
    memcpy(n.pos, r.pos, sizeof n.pos);
    n.airborne = o.accel_ok && o.accel_up < -5.0f;
    {
        /* The slip angle: the car's forward (the matrix's z row, x and z) against the horizontal travel since the last sample. */
        float dt = r.time - s_prev_time, vx = r.pos[0] - s_prev_pos[0], vz = r.pos[2] - s_prev_pos[2];
        float fl = sqrtf(r.fwd[0] * r.fwd[0] + r.fwd[1] * r.fwd[1]), vl = sqrtf(vx * vx + vz * vz);

        if (s_prev_time >= 0.0f && dt > 0.001f && dt < 0.25f && fl > 0.1f && vl / dt > 6.0f) {
            float c = (vx * r.fwd[0] + vz * r.fwd[1]) / (vl * fl);

            n.slip_angle = acosf(c > 1.0f ? 1.0f : c < -1.0f ? -1.0f : c);
        }
        s_prev_time = r.time;
        memcpy(s_prev_pos, r.pos, sizeof s_prev_pos);
    }
    for (k = 0; k < 3 && CS_M32(CS_PADS + k * 0x20) != 2; k++)
        ;
    st = (const uint8_t *)((uintptr_t)g_xbox_mem_offset + CS_PADS + k * 0x20 + 8);
    n.steer = (float)(int16_t)(st[14] | st[15] << 8) / 32767.0f;
    n.throttle = (float)st[13] / 255.0f;
    n.brake = (float)st[12] / 255.0f;
    n.impact_count = s_imp_count;
    n.impact_strength = s_imp_max;
    memcpy(n.impact_dir, s_imp_dir, sizeof n.impact_dir);
    s_imp_max = 0;
    memset(s_imp_dir, 0, sizeof s_imp_dir);

    __atomic_store_n(&s_seq, s_seq + 1, __ATOMIC_RELEASE);      /* odd: being written */
    s_snap = n;
    s_snap_ms = host_ms();
    __atomic_store_n(&s_seq, s_seq + 1, __ATOMIC_RELEASE);
}

int bo3_carstate_get(bo3_carstate *out, uint32_t max_age_ms)
{
    uint32_t a, b;
    uint64_t ms;
    int ok;

    do {
        a = __atomic_load_n(&s_seq, __ATOMIC_ACQUIRE);
        *out = s_snap;
        ms = s_snap_ms;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        b = __atomic_load_n(&s_seq, __ATOMIC_RELAXED);
    } while ((a & 1u) || a != b);
    ok = a != 0 && (max_age_ms == 0 || host_ms() - ms <= max_age_ms);
    if (!ok)
        memset(out, 0, sizeof *out);
    return ok;
}
