/*
 * seatcam.c - BO3_SEATCAM=<right>,<up>,<ahead>: the race is drawn from a fixed seat in the
 * player's car instead of the title's camera (a test for a VR driving view; docs/vr-openxr-proposal.md,
 * "Camera views"). Off unless set. "1" takes the default seat: on the centre line, 0.66 m above the
 * car's origin (where the title's own low front camera sits), 0.15 m ahead of it.
 *
 * How: the stage's render (sub_0001A6A0) begins by building the camera's frame and the view matrix from the
 * game's camera object (sub_001C8A60; the race's view is at 0x4D6200, the camera object at its + 0x68: + 0x20 the
 * orientation as a quaternion (x, y, z, w), + 0x30 the position; the first attempt wrote matrix rows there and
 * the world was gone; hirefresh.c). The "uw_stage_render"
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
 *
 * Looking around (a stand-in for the headset's tracking, to see what the stage looks like from the seat
 * sideways): the pad's RIGHT stick, which the title ignores in a race, or on Windows the keys J and L (left and
 * right) and I and K (up and down). It turns the camera about the seat point, up to 150 degrees sideways and
 * 60 up or down, easing back to the front when let go.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <math.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <stdarg.h>

#include "controls.h"

extern ptrdiff_t g_xbox_mem_offset;

#define SC_PTR(va)  ((float *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define SC_M32(va)  (*(const uint32_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))

#define SC_CAR1   0x0073A1D0u
#define SC_VIEW   0x004D6200u
#define SC_MAX_D  8.0f
#define SC_PADS   0x004AE20Cu         /* the pad manager's four records (main.c's drive_log) */
#define SC_LOOK_YAW   (150.0f * 3.14159265f / 180.0f)
#define SC_LOOK_PITCH (60.0f * 3.14159265f / 180.0f)

static int s_on, s_log;
static FILE *s_lf;                      /* BO3_SEATCAM_LOG=<file>: the diagnostics go here (the game's own stderr is not in the log file) */

static void slog(const char *fmt, ...)
{
    va_list a;

    if (!s_log)
        return;
    va_start(a, fmt);
    vfprintf(s_lf ? s_lf : stderr, fmt, a);
    va_end(a);
    if (s_lf)
        fflush(s_lf);
}
static float s_seat[3] = { 0.0f, 0.66f, 0.15f };      /* right, up, ahead */
static struct {
    int applied;
    uint32_t va;
    float orig[16], wrote[16];
} s_cam;

static float s_yaw, s_pitch;            /* the look now, radians: right and up positive */

/* The per-car seat (below): declared here for the start-up code. */
static float s_cur[3];                  /* the seat now: right, up, ahead */
static char s_file[600] = "seatcam.txt";
static void tab_load(void);

/* The near clip plane. The title's is 0.5 m (docs/draw-distance.md), so with the seat in the cabin the roof and
 * the window pillars, closer than that, are cut away. BO3_SEATCAM_NEAR=<metres> (default 0.1) replaces it while the
 * seat is drawn. Where it lives is not documented: the RwCamera (view + 0x58) is searched, once, for the pair
 * 0.5 then 10000.0 (the near and far planes sit side by side in a RwCamera), and what was found is logged. */
static float s_near = 0.1f;
static int s_near_off = -2;             /* the float index of the near plane; -2 not looked for, -1 not found */
static uint32_t s_near_va;
static float s_near_orig;
static int s_near_applied;

static void near_apply(void)
{
    uint32_t rc = SC_M32(SC_VIEW + 0x58u);
    float *f;
    int k;

    if (rc < 0x10000u || rc >= 0x08000000u)
        return;
    f = SC_PTR(rc);
    if (s_near_off == -2) {
        s_near_off = -1;
        for (k = 2; k < 0x120 / 4; k++)
            if (f[k] == 0.5f && f[k + 1] == 10000.0f) {
                s_near_off = k;
                break;
            }
        if (s_near_off >= 0) {
            slog("[SEATCAM] the camera %08x has its near plane at +0x%X (0.5) and the far plane after it (10000)\n", rc,
                 s_near_off * 4);
        } else {
            slog("[SEATCAM] the camera %08x: no 0.5 / 10000.0 pair found in its first 0x120 bytes; floats:\n", rc);
            for (k = 0; k < 0x120 / 4; k += 4)
                slog("  +0x%03X %12.4f %12.4f %12.4f %12.4f\n", k * 4, f[k], f[k + 1], f[k + 2], f[k + 3]);
        }
    }
    if (s_near_off < 0 || !(s_near > 0.0f) || f[s_near_off] == s_near)
        return;
    s_near_va = rc + (uint32_t)s_near_off * 4u;
    s_near_orig = f[s_near_off];
    f[s_near_off] = s_near;
    s_near_applied = 1;
}

static void near_restore(void)
{
    if (!s_near_applied)
        return;
    s_near_applied = 0;
    if (*SC_PTR(s_near_va) == s_near)
        *SC_PTR(s_near_va) = s_near_orig;
}

/* The look asked for, each -1..1: the right stick, or the keys. */
static void look_input(float *x, float *y)
{
    uint32_t k;
    const uint8_t *st;
    int rx, ry;

    *x = *y = 0.0f;
    for (k = 0; k < 3 && SC_M32(SC_PADS + k * 0x20) != 2; k++)
        ;
    st = (const uint8_t *)((uintptr_t)g_xbox_mem_offset + SC_PADS + k * 0x20 + 8);
    rx = (int)(int16_t)(st[18] | st[19] << 8);
    ry = (int)(int16_t)(st[20] | st[21] << 8);
    if (rx > 8000 || rx < -8000)
        *x = (float)rx / 32767.0f;
    if (ry > 8000 || ry < -8000)
        *y = (float)ry / 32767.0f;
#ifdef _WIN32
    if (GetAsyncKeyState('L') & 0x8000) *x = 1.0f;
    if (GetAsyncKeyState('J') & 0x8000) *x = -1.0f;
    if (GetAsyncKeyState('I') & 0x8000) *y = 1.0f;
    if (GetAsyncKeyState('K') & 0x8000) *y = -1.0f;
#endif
}

void bo3_seatcam_start(void)
{
    const char *e = getenv("BO3_SEATCAM");

    /* Off when unset, empty or exactly "0": a seat such as "0,0.66,0.15" starts with a 0 (no sideways offset). */
    if (!e || !*e || !strcmp(e, "0"))
        return;
    s_on = 1;
    {
        const char *l = getenv("BO3_SEATCAM_LOG");

        s_log = l != NULL && *l;
        if (s_log && strlen(l) > 1)
            s_lf = fopen(l, "w");
    }
    if (strchr(e, ',')) {
        float a, b, c;

        if (sscanf(e, "%f,%f,%f", &a, &b, &c) == 3) {
            s_seat[0] = a;
            s_seat[1] = b;
            s_seat[2] = c;
        }
    }
    {
        const char *nn = getenv("BO3_SEATCAM_NEAR");

        if (nn && *nn)
            s_near = (float)atof(nn);
    }
    {
        const char *sf = getenv("BO3_SEATCAM_FILE");

        if (sf && *sf)
            snprintf(s_file, sizeof s_file, "%s", sf);
    }
    memcpy(s_cur, s_seat, sizeof s_cur);
    tab_load();
    slog("[SEATCAM] near plane while seated: %.3f m (0 = the title's)\n", s_near);
    fprintf(stderr, "[SEATCAM] on: the race is drawn from the car, right %.2f up %.2f ahead %.2f m (BO3_SEATCAM)\n",
            s_seat[0], s_seat[1], s_seat[2]);
    slog("[SEATCAM] on: right %.2f up %.2f ahead %.2f m\n", s_seat[0], s_seat[1], s_seat[2]);
}

void bo3_seatcam_restore(void)
{
    if (s_on) {
        /* At the flip, after the picture's draw: did the title switch the car's draw off during the render?
         * (At the render's start the byte read 1 in both of its views.) Logged on a change. */
        static uint32_t last = 0xFFFFFFFFu;
        uint32_t now = (uint32_t)*(const uint8_t *)((uintptr_t)g_xbox_mem_offset + SC_CAR1 + 0x18F8u)
                       | (uint32_t)*(const uint8_t *)((uintptr_t)g_xbox_mem_offset + 0x004D61CEu) << 8
                       | (uint32_t)*(const uint8_t *)((uintptr_t)g_xbox_mem_offset + 0x004D6CA8u) << 16;

        if (now != last) {
            last = now;
            slog("[SEATCAM] at the flip: car draws %u, vehicles flag %u, 0x4D6CA8 %u\n", now & 0xFFu, (now >> 8) & 0xFFu,
                 (now >> 16) & 0xFFu);
        }
    }
    near_restore();
    if (!s_cam.applied)
        return;
    s_cam.applied = 0;
    if (!memcmp(SC_PTR(s_cam.va), s_cam.wrote, sizeof s_cam.wrote))    /* still ours: nothing else wrote it */
        memcpy(SC_PTR(s_cam.va), s_cam.orig, sizeof s_cam.orig);
}

/* ---- the seat for each car -----------------------------------------------
 *
 * Cars differ: a small coupe sits lower and further back than a big saloon, and a US car has its wheel on the left
 * where a UK one has it on the right. The seat is kept per car model and set by hand, in the game: the numpad moves
 * it (4 and 6 left and right, 8 and 2 ahead and back, 9 and 3 up and down, about 0.4 m a second), 5 puts it back to
 * the default, 0 saves it for this car. A car is known by the size field of its model, the .bgv (car + 0x50, its
 * + 8: unique to a model, the same every run). The table is a text file, one car a line: "0x<key> <right> <up> <ahead>",
 * BO3_SEATCAM_FILE=<path> (default seatcam.txt in the game's folder). A car with no line has the default seat. */
#define SC_MAXCARS 128
static struct { uint32_t key; float v[3]; } s_tab[SC_MAXCARS];
static int s_ntab;
static uint32_t s_curkey;               /* the car it is for; 0 none yet */

static void tab_load(void)
{
    FILE *f = fopen(s_file, "r");
    char line[200];

    if (!f)
        return;
    while (fgets(line, sizeof line, f) && s_ntab < SC_MAXCARS) {
        unsigned k;
        float a, b, c;

        if (sscanf(line, "%x %f %f %f", &k, &a, &b, &c) == 4) {
            s_tab[s_ntab].key = k;
            s_tab[s_ntab].v[0] = a;
            s_tab[s_ntab].v[1] = b;
            s_tab[s_ntab].v[2] = c;
            s_ntab++;
        }
    }
    fclose(f);
    slog("[SEATCAM] %d car seats read from %s\n", s_ntab, s_file);
}

static void tab_save(uint32_t key)
{
    FILE *f;
    int i;

    for (i = 0; i < s_ntab && s_tab[i].key != key; i++)
        ;
    if (i == s_ntab) {
        if (s_ntab >= SC_MAXCARS)
            return;
        s_ntab++;
    }
    s_tab[i].key = key;
    memcpy(s_tab[i].v, s_cur, sizeof s_cur);
    f = fopen(s_file, "w");
    if (!f) {
        slog("[SEATCAM] cannot write %s\n", s_file);
        return;
    }
    for (i = 0; i < s_ntab; i++)
        fprintf(f, "0x%08X %.3f %.3f %.3f\n", s_tab[i].key, s_tab[i].v[0], s_tab[i].v[1], s_tab[i].v[2]);
    fclose(f);
    slog("[SEATCAM] seat for car 0x%08X saved to %s: right %.3f up %.3f ahead %.3f\n", key, s_file, s_cur[0], s_cur[1],
         s_cur[2]);
}

/* The key of the player's car: its model's size field, or 0. */
static uint32_t car_key(void)
{
    uint32_t bgv = SC_M32(SC_CAR1 + 0x50u);

    if (bgv < 0x80000000u || bgv >= 0x84000000u || SC_M32(bgv) != 0x17u)
        return 0;
    return SC_M32(bgv + 8u);
}

/* Called once a render while seated: the seat for this car, and the numpad's nudges. */
static void seat_update(void)
{
    uint32_t key = car_key();
    int i;

    if (key != s_curkey || (s_curkey == 0 && s_cur[1] == 0.0f)) {
        s_curkey = key;
        memcpy(s_cur, s_seat, sizeof s_cur);
        for (i = 0; i < s_ntab; i++)
            if (s_tab[i].key == key && key) {
                memcpy(s_cur, s_tab[i].v, sizeof s_cur);
                break;
            }
        slog("[SEATCAM] car model 0x%08X: seat right %.3f up %.3f ahead %.3f (%s)\n", key, s_cur[0], s_cur[1], s_cur[2],
             i < s_ntab && key ? "from the file" : "the default");
    }
#ifdef _WIN32
    {
        static int saved_down, reset_down;
        const float step = 0.4f / 60.0f;                    /* 0.4 m a second at 60 renders a second */
        int moved = 0;

        if (GetAsyncKeyState(VK_NUMPAD6) & 0x8000) { s_cur[0] += step; moved = 1; }   /* right */
        if (GetAsyncKeyState(VK_NUMPAD4) & 0x8000) { s_cur[0] -= step; moved = 1; }   /* left */
        if (GetAsyncKeyState(VK_NUMPAD8) & 0x8000) { s_cur[2] += step; moved = 1; }   /* ahead */
        if (GetAsyncKeyState(VK_NUMPAD2) & 0x8000) { s_cur[2] -= step; moved = 1; }   /* back */
        if (GetAsyncKeyState(VK_NUMPAD9) & 0x8000) { s_cur[1] += step; moved = 1; }   /* up */
        if (GetAsyncKeyState(VK_NUMPAD3) & 0x8000) { s_cur[1] -= step; moved = 1; }   /* down */
        if (moved) {
            static unsigned t;

            if ((t++ % 30) == 0)
                slog("[SEATCAM] seat now right %.3f up %.3f ahead %.3f\n", s_cur[0], s_cur[1], s_cur[2]);
        }
        if (GetAsyncKeyState(VK_NUMPAD5) & 0x8000) {
            if (!reset_down) {
                reset_down = 1;
                for (i = 0; i < s_ntab && s_tab[i].key != s_curkey; i++)
                    ;
                memcpy(s_cur, i < s_ntab && s_curkey ? s_tab[i].v : s_seat, sizeof s_cur);
                slog("[SEATCAM] seat reset to %s: right %.3f up %.3f ahead %.3f\n",
                     i < s_ntab && s_curkey ? "the saved one" : "the default", s_cur[0], s_cur[1], s_cur[2]);
            }
        } else {
            reset_down = 0;
        }
        if (GetAsyncKeyState(VK_NUMPAD0) & 0x8000) {
            if (!saved_down) {
                saved_down = 1;
                if (s_curkey)
                    tab_save(s_curkey);
                else
                    slog("[SEATCAM] this car has no model key: not saved\n");
            }
        } else {
            saved_down = 0;
        }
    }
#endif
}

void bo3_seatcam_apply(void)
{
    static int shown, why_last = -1;
    static unsigned applied_n, calls_n;
    uint32_t cp;
    int why = 0;
    float *c, *car, d[3], dist, n[16];
    int i;

    if (!s_on || !g_xbox_mem_offset)
        return;
    bo3_seatcam_restore();                                  /* a flip we missed */
    car = SC_PTR(SC_CAR1 + 0x10u);
    calls_n++;
    cp = SC_M32(SC_VIEW + 0x68u);
    {
        /* Why does the car's body vanish? The title's car draw (sub_0018DE00) returns at once when the byte at
         * car + 0x18F8 is 0 or the "vehicles" render flag (0x4D61CE) is 0. Logged when either changes. */
        static uint32_t last = 0xFFFFFFFFu;
        uint32_t now = (uint32_t)*(const uint8_t *)((uintptr_t)g_xbox_mem_offset + SC_CAR1 + 0x18F8u)
                       | (uint32_t)*(const uint8_t *)((uintptr_t)g_xbox_mem_offset + 0x004D61CEu) << 8
                       | (uint32_t)*(const uint8_t *)((uintptr_t)g_xbox_mem_offset + SC_CAR1 + 0x18FAu) << 16
                       | (SC_M32(SC_CAR1 + 0x3D0u) & 0xFFu) << 24;

        if (now != last) {
            last = now;
            slog("[SEATCAM] render #%u: car draws %u, vehicles flag %u, +0x18FA %u, wreck state %u, phase %u\n", calls_n,
                 now & 0xFFu, (now >> 8) & 0xFFu, (now >> 16) & 0xFFu, now >> 24, SC_M32(SC_CAR1 + 0x134Cu));
        }
    }
    if (SC_M32(SC_CAR1 + 0x134Cu) != 2)
        why = 1;
    else if (SC_M32(SC_CAR1 + 0x3D0u) != 4)
        why = 2;
    else if (cp < 0x10000u || cp >= 0x08000000u)
        why = 3;
    if (why) {
        if (why != why_last) {
            why_last = why;
            slog("[SEATCAM] stage render #%u: not applied: %s\n", calls_n,
                 why == 1 ? "the race is not in play (phase)" : why == 2 ? "the car is not driving (+0x3D0)"
                                                                         : "no camera object at the view");
        }
        return;
    }
    c = SC_PTR(cp);
    for (i = 0; i < 3; i++)
        d[i] = c[12 + i] - car[12 + i];
    dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (s_log && shown < 6 && (dist < SC_MAX_D)) {
        shown++;
        slog("[SEATCAM] camera object %08x, dist %.2f:\n", cp, dist);
        for (i = 0; i < 4; i++)
            slog("  cam row %d  %8.4f %8.4f %8.4f %8.4f   car row %d  %8.4f %8.4f %8.4f %8.4f\n", i,
                    c[4 * i], c[4 * i + 1], c[4 * i + 2], c[4 * i + 3], i, car[4 * i], car[4 * i + 1],
                    car[4 * i + 2], car[4 * i + 3]);
    }
    if (dist >= SC_MAX_D) {
        if (why_last != 4) {
            why_last = 4;
            slog("[SEATCAM] stage render #%u: not applied: the title's camera is %.1f m from the car (a crash camera or a replay)\n",
                 calls_n, dist);
        }
        return;
    }
    if (why_last != 0) {
        why_last = 0;
        slog("[SEATCAM] stage render #%u: applied (the title's camera was %.2f m from the car)\n", calls_n, dist);
    }
    applied_n++;
    if (applied_n % 600 == 0)
        slog("[SEATCAM] %u renders drawn from the seat so far\n", applied_n);
    seat_update();
    memcpy(n, c, sizeof n);
    {
        /* The look: turn the car's axes about the seat. Written in the axes themselves (left, up, forward) so
         * that it does not depend on the handedness of the title's space: yaw to the right moves the forward
         * axis toward the car's right (= -left); pitch up moves it toward up. */
        float lx, ly, ty, tp, cy, sy, cp2, sp2, L[3], U[3], F[3], L2[3], F2[3], U2[3];

        look_input(&lx, &ly);
        ty = lx * SC_LOOK_YAW;
        tp = ly * SC_LOOK_PITCH;
        s_yaw += (ty - s_yaw) * 0.15f;                       /* eases, a frame at a time */
        s_pitch += (tp - s_pitch) * 0.15f;
        cy = cosf(s_yaw);
        sy = sinf(s_yaw);
        cp2 = cosf(s_pitch);
        sp2 = sinf(s_pitch);
        for (i = 0; i < 3; i++) {
            L[i] = car[i];
            U[i] = car[4 + i];
            F[i] = car[8 + i];
            F2[i] = cy * F[i] - sy * L[i];                   /* yaw about up */
            L2[i] = cy * L[i] + sy * F[i];
        }
        for (i = 0; i < 3; i++) {
            float f3 = cp2 * F2[i] + sp2 * U[i];             /* pitch about the (yawed) right axis */
            U2[i] = cp2 * U[i] - sp2 * F2[i];
            F2[i] = f3;
        }
        /* The camera object is not a matrix (its first log dump: row 0 zero, row 1 starting 90.0, row 2 a unit
         * quaternion, row 3 the position): + 0x20 holds the orientation as a quaternion (x, y, z, w) and + 0x30
         * the position. The quaternion is the standard one of the rotation whose COLUMNS are the camera's x, y
         * and z axes in the world: the car's left, up and forward (checked on the first dump: the chase camera's
         * (0.020, -0.811, 0.028, 0.585) against the car's (-0.001, -0.810, -0.001, 0.586), the chase camera
         * tilted a little down). The rest of rows 0 to 2 (the field of view and the like) is left as the title set it. */
        {
            float m00 = L2[0], m10 = L2[1], m20 = L2[2];     /* column 0: left */
            float m01 = U2[0], m11 = U2[1], m21 = U2[2];     /* column 1: up */
            float m02 = F2[0], m12 = F2[1], m22 = F2[2];     /* column 2: forward */
            float tr = m00 + m11 + m22, qx, qy, qz, qw, sc;

            if (tr > 0.0f) {
                sc = sqrtf(tr + 1.0f) * 2.0f;
                qw = 0.25f * sc;
                qx = (m21 - m12) / sc;
                qy = (m02 - m20) / sc;
                qz = (m10 - m01) / sc;
            } else if (m00 > m11 && m00 > m22) {
                sc = sqrtf(1.0f + m00 - m11 - m22) * 2.0f;
                qw = (m21 - m12) / sc;
                qx = 0.25f * sc;
                qy = (m01 + m10) / sc;
                qz = (m02 + m20) / sc;
            } else if (m11 > m22) {
                sc = sqrtf(1.0f + m11 - m00 - m22) * 2.0f;
                qw = (m02 - m20) / sc;
                qx = (m01 + m10) / sc;
                qy = 0.25f * sc;
                qz = (m12 + m21) / sc;
            } else {
                sc = sqrtf(1.0f + m22 - m00 - m11) * 2.0f;
                qw = (m10 - m01) / sc;
                qx = (m02 + m20) / sc;
                qy = (m12 + m21) / sc;
                qz = 0.25f * sc;
            }
            if (qw < 0.0f) {
                qx = -qx;
                qy = -qy;
                qz = -qz;
                qw = -qw;
            }
            n[8] = qx;
            n[9] = qy;
            n[10] = qz;
            n[11] = qw;
        }
    }
    for (i = 0; i < 3; i++) {
        /* position: right (= -row 0) * x + up * y + forward * z, from the car's origin */
        n[12 + i] = car[12 + i] - s_cur[0] * car[i] + s_cur[1] * car[4 + i] + s_cur[2] * car[8 + i];
    }
    memcpy(s_cam.orig, c, sizeof s_cam.orig);
    memcpy(s_cam.wrote, n, sizeof s_cam.wrote);
    memcpy(c, n, sizeof n);
    s_cam.va = cp;
    s_cam.applied = 1;
    near_apply();
}
