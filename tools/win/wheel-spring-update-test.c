/* wheel-spring-update-test.c: does the base keep its centring spring when the spring is
 * UPDATED while it runs (what RECOMP_WHEEL_FFB_SPEED does), or does the update stop it?
 *
 * It plays a spring on the wheel (gain ramped up over 1.5 s, strength never above 30% and
 * the same sign on both sides, like the game's), then goes through these phases, 10 s
 * each. At the start of each phase turn the wheel about a quarter turn and LET GO: with
 * a working spring it swings back to the centre, and the log shows how fast.
 *   A  15% spring as the game makes it
 *   B  updated in place to 30%
 *   C  updated in place to 5%
 *   D  updated in place back to 15%
 *   E  destroyed and created again at 15%
 * Every 0.25 s it prints the steering axis as a percent of the half travel (0 = centre).
 * A runaway guard stops everything if the wheel sits past 90% for 1.5 s. Ctrl-C ends it
 * (the gain goes to 0 first).
 *
 * Build in Git Bash after tools/win/fetch-deps.sh, then put SDL2.dll beside it:
 *   D=$HOME/.cache/bo3-windeps
 *   $D/llvm-mingw/bin/x86_64-w64-mingw32-clang -DSDL_MAIN_HANDLED \
 *       -I$D/SDL2/x86_64-w64-mingw32/include/SDL2 wheel-spring-update-test.c -o spring-test.exe \
 *       -L$D/SDL2/x86_64-w64-mingw32/lib -lSDL2
 */
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>

/* Everything printed goes to the console and to Desktop\spring-update-test.log. */
static FILE *g_log;
static void say(const char *fmt, ...)
{
    va_list a;

    va_start(a, fmt);
    vprintf(fmt, a);
    va_end(a);
    if (g_log) {
        va_start(a, fmt);
        vfprintf(g_log, fmt, a);
        va_end(a);
        fflush(g_log);
    }
}

#define VID 0x044F
#define PID 0xB696
#define RANGE 900.0
#define LOCK 360.0

static volatile int g_stop;
static void on_sig(int s) { (void)s; g_stop = 1; }

static SDL_Haptic *hap;
static SDL_Joystick *joy;
static int fx = -1;
static SDL_HapticEffect eff;

/* A 15% spring is a coefficient of 15% * range/lock of 32767 and a saturation of 15% of 65535. */
static void params(double pct, int16_t *coeff, uint16_t *sat)
{
    double k = pct / 100.0 * RANGE / LOCK;

    if (k > 1.0)
        k = 1.0;
    *coeff = (int16_t)(k * 32767.0 + 0.5);
    *sat = (uint16_t)(pct / 100.0 * 65535.0 + 0.5);
}

static void make(double pct)
{
    int16_t c;
    uint16_t s;

    params(pct, &c, &s);
    memset(&eff, 0, sizeof eff);
    eff.type = SDL_HAPTIC_SPRING;
    eff.condition.direction.type = SDL_HAPTIC_CARTESIAN;
    eff.condition.direction.dir[0] = 1;
    eff.condition.length = SDL_HAPTIC_INFINITY;
    eff.condition.right_sat[0] = eff.condition.left_sat[0] = s;
    eff.condition.right_coeff[0] = eff.condition.left_coeff[0] = c;
    fx = SDL_HapticNewEffect(hap, &eff);
    say("   create %.0f%% (coeff %d sat %u): id %d %s\n", pct, c, s, fx, fx < 0 ? SDL_GetError() : "ok");
    if (fx >= 0 && SDL_HapticRunEffect(hap, fx, 1) != 0)
        say("   run failed: %s\n", SDL_GetError());
}

static void update(double pct)
{
    int16_t c;
    uint16_t s;
    int r;

    params(pct, &c, &s);
    eff.condition.right_sat[0] = eff.condition.left_sat[0] = s;
    eff.condition.right_coeff[0] = eff.condition.left_coeff[0] = c;
    r = SDL_HapticUpdateEffect(hap, fx, &eff);
    say("   update to %.0f%% (coeff %d sat %u): %s%s%s; status %d\n", pct, c, s, r == 0 ? "ok" : "FAILED ",
           r == 0 ? "" : SDL_GetError(), "", SDL_HapticGetEffectStatus(hap, fx));
}

static double axis_pct(void)
{
    SDL_JoystickUpdate();
    return SDL_JoystickGetAxis(joy, 0) / 32768.0 * 100.0;
}

int main(void)
{
    int i, n, ph, found = -1, at_end_ms = 0;
    double g = 0;
    Uint32 t0;
    static const char *names[] = { "A  15% spring as the game makes it", "B  updated in place to 30%",
                                   "C  updated in place to 5%", "D  updated in place back to 15%",
                                   "E  destroyed and created again at 15%" };
    static const double pcts[] = { 15, 30, 5, 15, 15 };

    setvbuf(stdout, NULL, _IONBF, 0);       /* print as it goes, also into a pipe (the launcher tees it) */
    {
        const char *u = getenv("USERPROFILE");
        char path[600];

        snprintf(path, sizeof path, "%s/Desktop/spring-update-test.log", u ? u : ".");
        g_log = fopen(path, "w");
    }
    SDL_SetMainReady();
    signal(SIGINT, on_sig);
    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC) != 0) {
        say("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    n = SDL_NumJoysticks();
    for (i = 0; i < n; i++)
        if (SDL_JoystickGetDeviceVendor(i) == VID && SDL_JoystickGetDeviceProduct(i) == PID)
            found = i;
    if (found < 0) {
        say("The wheel base %04X:%04X is not connected.\n", VID, PID);
        return 1;
    }
    joy = SDL_JoystickOpen(found);
    hap = joy ? SDL_HapticOpenFromJoystick(joy) : NULL;
    if (!hap) {
        say("No haptic interface: %s\n", SDL_GetError());
        return 1;
    }
    say("Opened %s. Strength never above 30%%. Ctrl-C stops. Let go of the wheel when told.\n\n",
           SDL_JoystickName(joy));
    SDL_HapticSetGain(hap, 0);
    make(15);
    for (ph = 0; ph < 5 && !g_stop; ph++) {
        say("\n== PHASE ");
        say("%s", names[ph]);
        say("\n");
        if (ph == 1 || ph == 2 || ph == 3)
            update(pcts[ph]);
        if (ph == 4) {
            SDL_HapticDestroyEffect(hap, fx);
            make(pcts[ph]);
        }
        say("   >>> turn the wheel about a quarter turn and LET GO <<<\n");
        t0 = SDL_GetTicks();
        while (!g_stop && SDL_GetTicks() - t0 < 10000) {
            double p;
            Uint32 now = SDL_GetTicks();

            if (g < 1.0) {                          /* the first ramp-up, 1.5 s */
                g += 0.25 / 1.5;
                if (g > 1.0)
                    g = 1.0;
                SDL_HapticSetGain(hap, (int)(g * 100.0 + 0.5));
            }
            p = axis_pct();
            say("   t=%5.2fs  axis %+6.1f%%\n", (now - t0) / 1000.0, p);
            if (p > 90 || p < -90) {
                at_end_ms += 250;
                if (at_end_ms >= 1500) {
                    say("   runaway guard: the wheel sat at an end stop. Stopping.\n");
                    g_stop = 1;
                }
            } else {
                at_end_ms = 0;
            }
            SDL_Delay(250);
        }
    }
    SDL_HapticSetGain(hap, 0);
    SDL_HapticStopAll(hap);
    if (fx >= 0)
        SDL_HapticDestroyEffect(hap, fx);
    SDL_HapticClose(hap);
    SDL_JoystickClose(joy);
    say("\nDone. Force off.\n");
    SDL_Quit();
    return 0;
}
