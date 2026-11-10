/* wheel-effects-test.c: PLAYS force feedback effects on a Thrustmaster base, one after another, each announced, to find out
 * which kinds the base really renders through SDL's haptic interface (the game's hits, jolts and engine use a constant force
 * and a sine; the spring is known to work). Each runs for about 1.5 seconds at a moderate strength with a pause between,
 * and the program ends by itself. Hold the wheel lightly. The game must not be running (the base takes one program at a time).
 *
 * Build in Git Bash after tools/win/fetch-deps.sh, then put SDL2.dll beside it:
 *   D=$HOME/.cache/bo3-windeps
 *   $D/llvm-mingw/bin/x86_64-w64-mingw32-clang -DSDL_MAIN_HANDLED \
 *       -I$D/SDL2/x86_64-w64-mingw32/include/SDL2 wheel-effects-test.c -o effects-test.exe \
 *       -L$D/SDL2/x86_64-w64-mingw32/lib -lSDL2
 */
#include <SDL.h>
#include <stdio.h>
#include <string.h>

static SDL_Haptic *hap;

static void play(const char *what, SDL_HapticEffect *e, int ms)
{
    int id = SDL_HapticNewEffect(hap, e);

    printf("  %-58s ", what);
    fflush(stdout);
    if (id < 0) {
        printf("refused: %s\n", SDL_GetError());
        return;
    }
    if (SDL_HapticRunEffect(hap, id, 1) != 0) {
        printf("run failed: %s\n", SDL_GetError());
        SDL_HapticDestroyEffect(hap, id);
        return;
    }
    printf("playing...\n");
    SDL_Delay((Uint32)ms);
    SDL_HapticStopEffect(hap, id);
    SDL_HapticDestroyEffect(hap, id);
    SDL_Delay(1500);
}

static SDL_HapticEffect constant(int level, int polar)
{
    SDL_HapticEffect e;

    memset(&e, 0, sizeof e);
    e.type = SDL_HAPTIC_CONSTANT;
    if (polar) {
        e.constant.direction.type = SDL_HAPTIC_POLAR;
        e.constant.direction.dir[0] = level > 0 ? 9000 : 27000;     /* 3 o'clock / 9 o'clock, hundredths of a degree */
        e.constant.level = (Sint16)(level > 0 ? level : -level);
    } else {
        e.constant.direction.type = SDL_HAPTIC_CARTESIAN;
        e.constant.direction.dir[0] = 1;
        e.constant.level = (Sint16)level;
    }
    e.constant.length = 1500;
    return e;
}

static SDL_HapticEffect periodic(Uint16 type, Uint16 period, int mag)
{
    SDL_HapticEffect e;

    memset(&e, 0, sizeof e);
    e.type = type;
    e.periodic.direction.type = SDL_HAPTIC_CARTESIAN;
    e.periodic.direction.dir[0] = 1;
    e.periodic.period = period;
    e.periodic.magnitude = (Sint16)mag;
    e.periodic.length = 1500;
    return e;
}

int main(void)
{
    int n, i, found = -1;
    SDL_HapticEffect e;

    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC) != 0) {
        printf("init failed: %s\n", SDL_GetError());
        return 1;
    }
    n = SDL_NumJoysticks();
    for (i = 0; i < n; i++)
        if (SDL_JoystickGetDeviceVendor(i) == 0x044F && SDL_JoystickGetDeviceProduct(i) == 0xB696)
            found = i;
    if (found < 0) {
        printf("no Thrustmaster base (044F:B696) found among %d joysticks\n", n);
        return 1;
    }
    {
        SDL_Joystick *j = SDL_JoystickOpen(found);

        hap = j ? SDL_HapticOpenFromJoystick(j) : NULL;
        if (!hap) {
            printf("cannot open the haptic interface: %s\n", SDL_GetError());
            return 1;
        }
    }
    printf("The base will now play effects, one every 3 seconds, about 1.5 s each, at moderate strength.\n"
           "Hold the wheel lightly and note which ones you FEEL. Starting in 5 seconds...\n");
    SDL_HapticSetGain(hap, 100);
    SDL_Delay(5000);

    e = constant(12000, 0);   play("1  constant force, cartesian, to one side (level +12000)", &e, 1500);
    e = constant(-12000, 0);  play("2  constant force, cartesian, to the other side (-12000)", &e, 1500);
    e = constant(12000, 1);   play("3  constant force, polar 90 degrees (3 o'clock)", &e, 1500);
    e = constant(-12000, 1);  play("4  constant force, polar 270 degrees (9 o'clock)", &e, 1500);
    e = constant(20000, 0);   play("5  constant force, cartesian, stronger (+20000)", &e, 1500);
    e = periodic(SDL_HAPTIC_SINE, 40, 12000);   play("6  sine, 25 Hz, magnitude 12000", &e, 1500);
    e = periodic(SDL_HAPTIC_SINE, 40, 22000);   play("7  sine, 25 Hz, magnitude 22000", &e, 1500);
    e = periodic(SDL_HAPTIC_SINE, 100, 16000);  play("8  sine, 10 Hz, magnitude 16000", &e, 1500);
    e = periodic(SDL_HAPTIC_TRIANGLE, 60, 16000); play("9  triangle, about 17 Hz, magnitude 16000", &e, 1500);
    memset(&e, 0, sizeof e);
    e.type = SDL_HAPTIC_LEFTRIGHT;
    e.leftright.length = 1500;
    e.leftright.large_magnitude = 30000;
    e.leftright.small_magnitude = 30000;
    play("10 leftright (a rumble pad's motors), 30000 both", &e, 1500);

    SDL_HapticSetGain(hap, 0);
    SDL_HapticStopAll(hap);
    SDL_HapticClose(hap);
    SDL_Quit();
    printf("Done. Tell me which numbers you felt, and for 1 to 5 which way the wheel was pushed.\n");
    return 0;
}
