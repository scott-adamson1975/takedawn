/*
 * telemetry.c - BO3_TELEMETRY=<prefix>: player 1's car state, once a frame of
 * a race, for finding what a motion platform (SimHub) and a wheel's force
 * feedback need (docs/wheel-force-feedback-proposal.md).
 *
 * It is a recording tool, not a feature: it only reads the title's memory.
 *
 * Where: the "drive_log" mark (config/patches.json, sub_00059BD0, the HUD
 * speedometer's update, once a frame while a race's HUD runs, the HUD in
 * ebx). main.c's drive_log() has the car ([[hud + 0x2C]] = the player's
 * CB3PlayerCar) and calls bo3_telemetry_sample(). A mark runs only its first
 * handler, so this is called from there rather than being a mark of its own.
 * Without the high refresh rate setting a frame is a step; with it, the car's
 * matrix is blended between steps while the title draws, so record without it.
 *
 * Two files:
 *   <prefix>.csv  one line a frame: what is already known of the car (race
 *                 time, speed, the world matrix and the four wheels', the
 *                 pad as the title read it).
 *   <prefix>.bin  the raw bytes of the car object each frame, for tools/
 *                 telemetry/find_fields.py to look for what is not known (a
 *                 velocity vector, angular velocity, suspension, slip, an
 *                 airborne flag): the fields whose values follow what the
 *                 matrix does. BO3_TELEMETRY_DUMP=0 leaves it out.
 *
 * .bin layout (little endian): "BO3T", u32 version (1), u32 bytes per frame's
 * car dump; then per frame: u32 flip, u32 car address, the dump.
 *
 * The car object (CB3PlayerCar, 0x73A1D0 for player 1) as far as the docs
 * know it: +0x10 the world matrix (an RwMatrix: right, up, at, each 16 bytes,
 * the position at +0x30), +0x90 + 0x40 * w the wheels' matrices, +0xBC the
 * speed (m/s, a magnitude), +0x10DC the race time (s), +0x134C the phase
 * (1 before the GO, 2 in play, 3 finished).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "controls.h"
#include "carstate.h"

extern ptrdiff_t g_xbox_mem_offset;

#define M8(va)  (*(const uint8_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define M16(va) (*(const int16_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define M32(va) (*(const uint32_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define MF(va)  (*(const float *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))

#define TEL_CAR1     0x0073A1D0u    /* player 1's CB3PlayerCar (world 0x60EA00 + 0x12B7D0) */
#define TEL_MATRIX   0x10u         /* car + : the world matrix */
#define TEL_WHEELS   0x90u          /* car + : four matrices of 0x40 */
#define TEL_SPEED    0xBCu          /* car + : m/s */
#define TEL_TIME     0x10DCu        /* car + : the race time, s */
#define TEL_PHASE    0x134Cu        /* car + : 1 before the GO, 2 in play, 3 finished */
#define TEL_DUMP     0x2900u        /* bytes of the car object kept a frame (it reaches 0x27D8+) */
#define TEL_PADS     0x004AE20Cu    /* the pad manager's four records (as main.c's drive_log) */

static FILE *s_csv, *s_bin, *s_imp;
static int s_frames, s_warned;
static uint32_t (*s_flips)(void);

/* The matrix at addr: right, up, at (each three floats at 0x10 apart) and the position (+0x30). */
static void put_matrix(FILE *f, uint32_t m)
{
    fprintf(f, ",%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.4f,%.4f,%.4f",
            MF(m), MF(m + 4), MF(m + 8), MF(m + 0x10), MF(m + 0x14), MF(m + 0x18),
            MF(m + 0x20), MF(m + 0x24), MF(m + 0x28), MF(m + 0x30), MF(m + 0x34), MF(m + 0x38));
}

void bo3_telemetry_start(uint32_t (*flips)(void))
{
    const char *p = getenv("BO3_TELEMETRY"), *d = getenv("BO3_TELEMETRY_DUMP");
    char path[1024];

    s_flips = flips;
    if (!p || !*p)
        return;
    snprintf(path, sizeof path, "%s.csv", p);
    s_csv = fopen(path, "w");
    if (!s_csv) {
        fprintf(stderr, "BO3_TELEMETRY: cannot write %s\n", path);
        return;
    }
    fprintf(s_csv, "flip,race_time,phase,speed");
    fprintf(s_csv, ",rx,ry,rz,ux,uy,uz,ax,ay,az,px,py,pz");
    for (int w = 0; w < 4; w++)
        fprintf(s_csv, ",w%drx,w%dry,w%drz,w%dux,w%duy,w%duz,w%dax,w%day,w%daz,w%dpx,w%dpy,w%dpz",
                w, w, w, w, w, w, w, w, w, w, w, w);
    fprintf(s_csv, ",pad_buttons,pad_lt,pad_rt,pad_lx,pad_ly,pad_a,pad_b,pad_x,pad_y");
    /* carstate.c's derived values for the same sample (carstate_math.h) */
    fprintf(s_csv, ",cs_ok,cs_speed,cs_yaw,cs_pitch,cs_roll,cs_surge,cs_sway,cs_vup,cs_aup,cs_slip,cs_impacts\n");
    bo3_carstate_request();                 /* the sampler and the impact mark are on */
    snprintf(path, sizeof path, "%s-impacts.csv", p);
    s_imp = fopen(path, "w");
    if (s_imp)
        fprintf(s_imp, "flip,strength,dir_x,dir_y,dir_z\n");
    if (!(d && *d == '0')) {
        const uint32_t hdr[3] = { 0x54334F42u /* "BO3T" */, 1u, TEL_DUMP };

        snprintf(path, sizeof path, "%s.bin", p);
        s_bin = fopen(path, "wb");
        if (s_bin)
            fwrite(hdr, sizeof hdr, 1, s_bin);
        else
            fprintf(stderr, "BO3_TELEMETRY: cannot write %s\n", path);
    }
    fprintf(stderr, "[TELEMETRY] recording to %s.csv%s\n", p, s_bin ? " and .bin" : "");
}

int bo3_telemetry_active(void)
{
    return s_csv != NULL;
}

/* One line a collision entry call against player 1's car (carstate.c). A scrape
 * calls it every frame. */
void bo3_telemetry_impact(float strength, const float dir[3])
{
    if (!s_imp)
        return;
    fprintf(s_imp, "%u,%.4f,%.3f,%.3f,%.3f\n", s_flips ? s_flips() : 0, strength, dir[0], dir[1], dir[2]);
    fflush(s_imp);
}

void bo3_telemetry_sample(uint32_t hud_car)
{
    /* The first recording sampled what the HUD points at ([hud + 0x2C] = 0x667E90 there): not a car
     * (matrix scaled 0.001, race time 0, phase 0). Player 1's CB3PlayerCar is at a fixed address
     * (autopilot.c); a frame is a race's only while its phase is 1, 2 or 3. */
    const uint32_t car = TEL_CAR1;
    uint32_t k, phase;
    const uint8_t *st;

    if (!s_csv || !g_xbox_mem_offset)
        return;
    if (hud_car != car && !s_warned) {
        s_warned = 1;
        fprintf(stderr, "[TELEMETRY] the HUD points at 0x%X, not player 1's car 0x%X: recording the latter\n",
                hud_car, car);
    }
    phase = M32(car + TEL_PHASE);
    if (phase < 1 || phase > 3)
        return;
    fprintf(s_csv, "%u,%.4f,%u,%.4f", s_flips ? s_flips() : 0, MF(car + TEL_TIME),
            M32(car + TEL_PHASE), MF(car + TEL_SPEED));
    put_matrix(s_csv, car + TEL_MATRIX);
    for (int w = 0; w < 4; w++)
        put_matrix(s_csv, car + TEL_WHEELS + 0x40u * (uint32_t)w);
    /* The first open pad port's XINPUT_STATE as the title read it (main.c drive_log). */
    for (k = 0; k < 3 && M32(TEL_PADS + k * 0x20) != 2; k++)
        ;
    st = (const uint8_t *)((uintptr_t)g_xbox_mem_offset + TEL_PADS + k * 0x20 + 8);
    fprintf(s_csv, ",%u,%u,%u,%d,%d,%u,%u,%u,%u",
            (unsigned)(st[4] | st[5] << 8), st[12], st[13],
            (int)(int16_t)(st[14] | st[15] << 8), (int)(int16_t)(st[16] | st[17] << 8),
            st[6], st[7], st[8], st[9]);
    {
        bo3_carstate cs;

        bo3_carstate_get(&cs, 0);           /* main.c's drive_log sampled it just before */
        fprintf(s_csv, ",%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%u\n", cs.accel_ok, cs.speed,
                cs.yaw_rate, cs.pitch_rate, cs.roll_rate, cs.surge, cs.sway, cs.vel_up, cs.accel_up,
                cs.slip, cs.impact_count);
    }
    if (s_bin) {
        const uint32_t head[2] = { s_flips ? s_flips() : 0, car };

        fwrite(head, sizeof head, 1, s_bin);
        fwrite((const void *)((uintptr_t)g_xbox_mem_offset + car), TEL_DUMP, 1, s_bin);
    }
    if (++s_frames % 60 == 0) {
        fflush(s_csv);
        if (s_bin)
            fflush(s_bin);
    }
}
