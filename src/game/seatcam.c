/*
 * seatcam.c - BO3_SEATCAM=<right>,<up>,<ahead>: the race is drawn from a fixed seat in the
 * player's car instead of the title's camera (a test for a VR driving view; docs/vr-openxr-proposal.md,
 * "Camera views"). Off unless set. "1" takes the default seat: on the centre line, 0.66 m above the
 * car's origin (where the title's own low front camera sits), 0.15 m ahead of it.
 *
 * How: the stage's render (sub_0001A6A0) begins by building the camera's frame and the view matrix from the
 * game's camera object (sub_001C8A60; the race's view is at 0x4D6200, the camera object at its + 0x68, laid out
 * like a car's RwMatrix: three axes at 0x10 apart and the position at + 0x30; hirefresh.c). The "uw_stage_render"
 * mark is that function's entry, so bo3_seatcam_apply() writes the seat there, and bo3_seatcam_restore() puts
 * the camera object back at the next flip (the title's camera follows its own state from step to step; our
 * values must not leak into it). A mark runs only its first handler, so main.c calls both.
 *
 * The seat is the car's own axes: the camera turns, pitches and rolls with the body. The car's matrix has the
 * LEFT axis first (row 0), then up, then forward (docs/wheel-force-feedback-proposal.md); right = -row 0.
 *
 * Applied only to a race in play (phase 2) with the car driving (+0x3D0 is 4) and the title's camera within
 * 8 m of the car (a chase or front view, not a crash camera or a replay). BO3_SEATCAM_LOG=1 prints the camera
 * object and the car's axes a few times, to check the layout above.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <math.h>

#include "controls.h"

extern ptrdiff_t g_xbox_mem_offset;

#define SC_PTR(va)  ((float *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define SC_M32(va)  (*(const uint32_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))

#define SC_CAR1   0x0073A1D0u
#define SC_VIEW   0x004D6200u
#define SC_MAX_D  8.0f

static int s_on, s_log;
static float s_seat[3] = { 0.0f, 0.66f, 0.15f };      /* right, up, ahead */
static struct {
    int applied;
    uint32_t va;
    float orig[16], wrote[16];
} s_cam;

void bo3_seatcam_start(void)
{
    const char *e = getenv("BO3_SEATCAM");

    if (!e || !*e || *e == '0')
        return;
    s_on = 1;
    s_log = getenv("BO3_SEATCAM_LOG") != NULL;
    if (strchr(e, ',')) {
        float a, b, c;

        if (sscanf(e, "%f,%f,%f", &a, &b, &c) == 3) {
            s_seat[0] = a;
            s_seat[1] = b;
            s_seat[2] = c;
        }
    }
    fprintf(stderr, "[SEATCAM] on: the race is drawn from the car, right %.2f up %.2f ahead %.2f m (BO3_SEATCAM)\n",
            s_seat[0], s_seat[1], s_seat[2]);
}

void bo3_seatcam_restore(void)
{
    if (!s_cam.applied)
        return;
    s_cam.applied = 0;
    if (!memcmp(SC_PTR(s_cam.va), s_cam.wrote, sizeof s_cam.wrote))    /* still ours: nothing else wrote it */
        memcpy(SC_PTR(s_cam.va), s_cam.orig, sizeof s_cam.orig);
}

void bo3_seatcam_apply(void)
{
    static int shown;
    uint32_t cp;
    float *c, *car, d[3], dist, n[16];
    int i;

    if (!s_on || !g_xbox_mem_offset)
        return;
    bo3_seatcam_restore();                                  /* a flip we missed */
    car = SC_PTR(SC_CAR1 + 0x10u);
    if (SC_M32(SC_CAR1 + 0x134Cu) != 2 || SC_M32(SC_CAR1 + 0x3D0u) != 4)
        return;
    cp = SC_M32(SC_VIEW + 0x68u);
    if (cp < 0x10000u || cp >= 0x08000000u)
        return;
    c = SC_PTR(cp);
    for (i = 0; i < 3; i++)
        d[i] = c[12 + i] - car[12 + i];
    dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (s_log && shown < 6 && (dist < SC_MAX_D)) {
        shown++;
        fprintf(stderr, "[SEATCAM] camera object %08x, dist %.2f:\n", cp, dist);
        for (i = 0; i < 4; i++)
            fprintf(stderr, "  cam row %d  %8.4f %8.4f %8.4f %8.4f   car row %d  %8.4f %8.4f %8.4f %8.4f\n", i,
                    c[4 * i], c[4 * i + 1], c[4 * i + 2], c[4 * i + 3], i, car[4 * i], car[4 * i + 1],
                    car[4 * i + 2], car[4 * i + 3]);
    }
    if (dist >= SC_MAX_D)
        return;
    memcpy(n, c, sizeof n);
    for (i = 0; i < 3; i++) {
        n[i] = car[i];                                      /* the axes: the car's */
        n[4 + i] = car[4 + i];
        n[8 + i] = car[8 + i];
        /* position: right (= -row 0) * x + up * y + forward * z, from the car's origin */
        n[12 + i] = car[12 + i] - s_seat[0] * car[i] + s_seat[1] * car[4 + i] + s_seat[2] * car[8 + i];
    }
    memcpy(s_cam.orig, c, sizeof s_cam.orig);
    memcpy(s_cam.wrote, n, sizeof s_cam.wrote);
    memcpy(c, n, sizeof n);
    s_cam.va = cp;
    s_cam.applied = 1;
}
