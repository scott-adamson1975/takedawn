/*
 * The car snapshot's maths (src/game/carstate_math.h): conventions, filters and
 * their restarts on made-up motion, and (BO3_CARSTATE_REPLAY=<prefix>) a replay of a
 * BO3_TELEMETRY recording through the same code, printed for a look.
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "carstate_math.h"

static int g_checks, g_fail;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { g_fail++; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define NEAR(a, b, tol) (fabsf((a) - (b)) <= (tol))

#define DT (1.0f / 60.0f)

static bo3_cs_raw base(float t)
{
    bo3_cs_raw r;

    memset(&r, 0, sizeof r);
    r.time = t;
    r.phase = 2;
    return r;
}

/* n steps of straight motion along z from t0 at v0, accelerating by acc. */
static void run_straight(bo3_cs_filter *f, bo3_cs_out *o, int n, float t0, float v0, float acc, float *z)
{
    for (int i = 0; i < n; i++) {
        float t = t0 + (float)i * DT, v = v0 + acc * (float)i * DT;
        bo3_cs_raw r = base(t);

        r.speed = v;
        *z += v * DT;
        r.pos[2] = *z;
        bo3_cs_step(f, &r, o);
    }
}

static void test_straight(void)
{
    bo3_cs_filter f;
    bo3_cs_out o;
    float z = 0;

    memset(&f, 0, sizeof f);
    run_straight(&f, &o, 120, 1.0f, 30.0f, 0.0f, &z);
    CHECK(o.accel_ok, "steady drive reports accelerations");
    CHECK(NEAR(o.speed, 30.0f, 1e-4f), "speed %g", o.speed);
    CHECK(NEAR(o.surge, 0, 0.01f) && NEAR(o.sway, 0, 0.01f), "steady: surge %g sway %g", o.surge, o.sway);
    CHECK(NEAR(o.vel_up, 0, 0.01f) && NEAR(o.accel_up, 0, 0.05f), "level: vup %g aup %g", o.vel_up, o.accel_up);
}

static void test_surge(void)
{
    bo3_cs_filter f;
    bo3_cs_out o;
    float z = 0;

    memset(&f, 0, sizeof f);
    run_straight(&f, &o, 240, 1.0f, 10.0f, 5.0f, &z);
    CHECK(NEAR(o.surge, 5.0f, 0.05f), "accelerating at 5: surge %g", o.surge);
    memset(&f, 0, sizeof f);
    z = 0;
    run_straight(&f, &o, 240, 1.0f, 40.0f, -12.0f, &z);
    CHECK(NEAR(o.surge, -12.0f, 0.1f), "braking at 12: surge %g", o.surge);
}

static void test_sway_and_rates(void)
{
    bo3_cs_filter f;
    bo3_cs_out o;
    bo3_cs_raw r;

    memset(&f, 0, sizeof f);
    for (int i = 0; i < 200; i++) {
        r = base(1.0f + (float)i * DT);
        r.speed = 20.0f;
        r.fwd[0] = sinf(-0.5f * (float)i * DT);   /* the heading falls at 0.5 rad/s: turning right */
        r.fwd[1] = cosf(-0.5f * (float)i * DT);
        r.pitch = 0.1f / BO3_CS_STEP_HZ;        /* stored positive: nose down */
        r.roll = 0.2f / BO3_CS_STEP_HZ;         /* stored positive: right side down */
        r.pos[2] = 20.0f * (float)i * DT;
        bo3_cs_step(&f, &r, &o);
    }
    CHECK(NEAR(o.yaw_rate, 0.5f, 0.01f), "yaw rate %g", o.yaw_rate);
    CHECK(NEAR(o.pitch_rate, -0.1f, 1e-4f), "pitch rate: nose down reads negative: %g", o.pitch_rate);
    CHECK(NEAR(o.roll_rate, 0.2f, 1e-4f), "roll rate: stored positive is the right side going down: %g", o.roll_rate);
    CHECK(NEAR(o.sway, 10.0f, 0.05f), "a right turn at 20 m/s and 0.5 rad/s sways right 10: %g", o.sway);
    for (int i = 200; i < 400; i++) {
        r.time = 1.0f + (float)i * DT;
        r.fwd[0] = sinf(0.5f * (float)(i - 200) * DT - 0.5f * 200 * DT);   /* and now rising: turning left */
        r.fwd[1] = cosf(0.5f * (float)(i - 200) * DT - 0.5f * 200 * DT);
        r.pos[2] = 20.0f * (float)i * DT;
        bo3_cs_step(&f, &r, &o);
    }
    CHECK(NEAR(o.sway, -10.0f, 0.05f), "a left turn sways left: %g", o.sway);
}

static void test_vertical(void)
{
    bo3_cs_filter f;
    bo3_cs_out o;
    bo3_cs_raw r;
    float y = 10.0f, v = 0;

    memset(&f, 0, sizeof f);
    for (int i = 0; i < 240; i++) {         /* a car in the air: free fall */
        r = base(1.0f + (float)i * DT);
        r.speed = 25.0f;
        r.pos[2] = 25.0f * (float)i * DT;
        r.pos[1] = y;
        v -= BO3_CS_GRAVITY * DT;
        y += v * DT;
        bo3_cs_step(&f, &r, &o);
    }
    CHECK(NEAR(o.accel_up, -BO3_CS_GRAVITY, 0.2f), "free fall: accel_up %g", o.accel_up);
    CHECK(o.vel_up < -3, "falling: vel_up %g", o.vel_up);
}

static void test_restarts(void)
{
    bo3_cs_filter f;
    bo3_cs_out o;
    bo3_cs_raw r;
    float z = 0;

    memset(&f, 0, sizeof f);
    run_straight(&f, &o, 60, 1.0f, 30.0f, 3.0f, &z);
    CHECK(o.accel_ok && o.surge > 2, "running: surge %g", o.surge);

    r = base(2.5f);                          /* a respawn: 500 m away in one step */
    r.speed = 0;
    r.pos[2] = z + 500.0f;
    bo3_cs_step(&f, &r, &o);
    CHECK(!o.accel_ok && o.surge == 0 && o.sway == 0 && o.accel_up == 0, "a jump gives no accelerations");
    r.time += DT;
    bo3_cs_step(&f, &r, &o);
    CHECK(o.accel_ok && NEAR(o.surge, 0, 0.01f), "after the jump it carries on from the new place: surge %g", o.surge);

    r.time += 0.5f;                          /* the HUD stopped updating (a pause) */
    r.speed = 30.0f;
    bo3_cs_step(&f, &r, &o);
    CHECK(!o.accel_ok, "a gap restarts the filters");
    r.time += DT;
    r.phase = 1;                              /* before the GO */
    bo3_cs_step(&f, &r, &o);
    CHECK(!o.accel_ok, "no accelerations outside phase 2");
    r.time += DT;
    r.phase = 2;
    bo3_cs_step(&f, &r, &o);
    CHECK(o.accel_ok, "the first sample back in play follows a phase-1 sample: it is real");

    r.time = 1.0f;                            /* the clock going back (a retry) */
    bo3_cs_step(&f, &r, &o);
    CHECK(!o.accel_ok, "time going backwards restarts the filters");
}

static void test_reverse(void)
{
    bo3_cs_filter f;
    bo3_cs_out o;
    bo3_cs_raw r = base(1.0f);
    float z = 0;

    memset(&f, 0, sizeof f);
    for (int i = 0; i < 120; i++) {                 /* backing up at 8 m/s: the title's speed stays positive */
        r = base(1.0f + (float)i * DT);
        r.speed = 8.0f;
        for (int w = 0; w < 4; w++)
            r.spin[w] = -8.0f / BO3_CS_WHEEL_R;
        z -= 8.0f * DT;
        r.pos[2] = z;
        bo3_cs_step(&f, &r, &o);
    }
    CHECK(NEAR(o.speed, -8.0f, 1e-4f), "reversing reads negative: %g", o.speed);
    CHECK(NEAR(o.surge, 0, 0.05f), "steady reverse: surge %g", o.surge);
    CHECK(NEAR(o.slip, 0, 1e-3f), "wheels at ground speed in reverse: slip %g", o.slip);
    for (int w = 0; w < 4; w++)
        r.spin[w] = 0.3f;                           /* a stop, wheels barely turning */
    r.speed = 0.2f;
    r.time += DT;
    bo3_cs_step(&f, &r, &o);
    CHECK(o.speed > 0, "a near stop is not called reversing: %g", o.speed);
}

static void test_slip(void)
{
    bo3_cs_filter f;
    bo3_cs_out o;
    bo3_cs_raw r = base(1.0f);

    memset(&f, 0, sizeof f);
    r.speed = 20.0f;
    for (int i = 0; i < 4; i++)
        r.spin[i] = 20.0f / BO3_CS_WHEEL_R;
    bo3_cs_step(&f, &r, &o);
    CHECK(NEAR(o.slip, 0, 1e-3f), "wheels at ground speed: slip %g", o.slip);
    for (int i = 0; i < 4; i++)
        r.spin[i] = 23.0f / BO3_CS_WHEEL_R;
    r.time += DT;
    bo3_cs_step(&f, &r, &o);
    CHECK(NEAR(o.slip, 0.15f, 1e-3f), "wheels 15 percent over: slip %g", o.slip);
    r.speed = 0;
    r.time += DT;
    bo3_cs_step(&f, &r, &o);
    CHECK(o.slip == 0, "at a standstill slip is left at 0: %g", o.slip);
}

/* The recording: .bin (see telemetry.c): "BO3T", version, bytes per dump; per frame flip, car, dump. */
static void replay(const char *prefix)
{
    char path[1024];
    FILE *f;
    uint32_t hdr[3], head[2], dump;
    unsigned char *buf;
    bo3_cs_filter flt;
    bo3_cs_out o;
    bo3_cs_raw r;
    int n = 0, ok = 0;
    double mx_surge = 0, mn_surge = 0, mx_sway = 0, mn_sway = 0, mx_up = 0, mn_up = 0, sum = 0;

    snprintf(path, sizeof path, "%s.bin", prefix);
    f = fopen(path, "rb");
    if (!f || fread(hdr, sizeof hdr, 1, f) != 1 || hdr[0] != 0x54334F42u) {
        printf("replay: cannot read %s\n", path);
        if (f)
            fclose(f);
        return;
    }
    dump = hdr[2];
    buf = malloc(dump);
    memset(&flt, 0, sizeof flt);
    while (fread(head, sizeof head, 1, f) == 1 && fread(buf, dump, 1, f) == 1) {
#define F(off) (*(const float *)(buf + (off)))
        r.time = F(0x10DC);
        r.phase = *(const uint32_t *)(buf + 0x134C);
        r.speed = F(0x64);
        for (int i = 0; i < 3; i++)
            r.pos[i] = F(0x40 + 4 * i);
        r.pitch = F(0x25D0);
        r.fwd[0] = F(0x30);
        r.fwd[1] = F(0x38);
        r.roll = F(0x25D8);
        for (int i = 0; i < 4; i++)
            r.spin[i] = F(0x578 + 4 * i);
#undef F
        bo3_cs_step(&flt, &r, &o);
        n++;
        if (!o.accel_ok)
            continue;
        ok++;
        sum += fabs(o.surge);
        if (o.surge > mx_surge) mx_surge = o.surge;
        if (o.surge < mn_surge) mn_surge = o.surge;
        if (o.sway > mx_sway) mx_sway = o.sway;
        if (o.sway < mn_sway) mn_sway = o.sway;
        if (o.accel_up > mx_up) mx_up = o.accel_up;
        if (o.accel_up < mn_up) mn_up = o.accel_up;
    }
    printf("replay %s: %d samples, %d with accelerations, %d restarts\n", prefix, n, ok, n - ok);
    printf("  surge  %.1f .. %.1f m/s^2 (mean |.| %.2f)\n", mn_surge, mx_surge, ok ? sum / ok : 0.0);
    printf("  sway   %.1f .. %.1f m/s^2\n", mn_sway, mx_sway);
    printf("  accel_up %.1f .. %.1f m/s^2\n", mn_up, mx_up);
    CHECK(n > 0 && ok > n / 2, "most of a real race has accelerations (%d of %d)", ok, n);
    free(buf);
    fclose(f);
}

int main(void)
{
    const char *rp = getenv("BO3_CARSTATE_REPLAY");

    test_straight();
    test_surge();
    test_sway_and_rates();
    test_vertical();
    test_restarts();
    test_reverse();
    test_slip();
    if (rp && *rp)
        replay(rp);
    printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail != 0;
}
