/* wheel-haptic-probe.c: read-only. Lists the joysticks and, for a Thrustmaster
 * wheel, its SDL haptic (force feedback) capabilities. It plays no effect.
 *
 * Build in Git Bash after tools/win/fetch-deps.sh, then put SDL2.dll beside it:
 *   D=$HOME/.cache/bo3-windeps
 *   $D/llvm-mingw/bin/x86_64-w64-mingw32-clang -DSDL_MAIN_HANDLED \
 *       -I$D/SDL2/x86_64-w64-mingw32/include/SDL2 wheel-haptic-probe.c -o probe.exe \
 *       -L$D/SDL2/x86_64-w64-mingw32/lib -lSDL2
 */
#include <SDL.h>
#include <stdio.h>

static void feat(unsigned f)
{
    static const struct { unsigned bit; const char *n; } t[] = {
        { SDL_HAPTIC_CONSTANT, "constant" }, { SDL_HAPTIC_SINE, "sine" }, { SDL_HAPTIC_LEFTRIGHT, "leftright" },
        { SDL_HAPTIC_TRIANGLE, "triangle" }, { SDL_HAPTIC_SAWTOOTHUP, "sawup" }, { SDL_HAPTIC_SAWTOOTHDOWN, "sawdown" },
        { SDL_HAPTIC_RAMP, "ramp" }, { SDL_HAPTIC_SPRING, "spring" }, { SDL_HAPTIC_DAMPER, "damper" },
        { SDL_HAPTIC_INERTIA, "inertia" }, { SDL_HAPTIC_FRICTION, "friction" }, { SDL_HAPTIC_CUSTOM, "custom" },
        { SDL_HAPTIC_GAIN, "gain" }, { SDL_HAPTIC_AUTOCENTER, "autocenter" }, { SDL_HAPTIC_STATUS, "status" },
        { SDL_HAPTIC_PAUSE, "pause" },
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++)
        printf(" %s%s", t[i].n, (f & t[i].bit) ? "=yes" : "=no");
    printf("\n");
}

int main(void)
{
    int n, i;
    SDL_version v;

    SDL_GetVersion(&v);
    printf("SDL %d.%d.%d\n", v.major, v.minor, v.patch);
    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC) != 0) { printf("init failed: %s\n", SDL_GetError()); return 1; }
    n = SDL_NumJoysticks();
    printf("%d joysticks\n", n);
    for (i = 0; i < n; i++) {
        SDL_Joystick *j = SDL_JoystickOpen(i);
        unsigned vid = SDL_JoystickGetDeviceVendor(i), pid = SDL_JoystickGetDeviceProduct(i);
        int hap = j ? SDL_JoystickIsHaptic(j) : -1;
        printf("[%d] %04X:%04X \"%s\" axes=%d buttons=%d hats=%d haptic=%d\n", i, vid, pid,
               SDL_JoystickNameForIndex(i), j ? SDL_JoystickNumAxes(j) : 0, j ? SDL_JoystickNumButtons(j) : 0,
               j ? SDL_JoystickNumHats(j) : 0, hap);
        if (j && hap == 1 && vid == 0x044F) {
            SDL_Haptic *h = SDL_HapticOpenFromJoystick(j);
            if (!h) { printf("    haptic open failed: %s\n", SDL_GetError()); }
            else {
                printf("    effects=%d playing=%d axes=%d\n", SDL_HapticNumEffects(h),
                       SDL_HapticNumEffectsPlaying(h), SDL_HapticNumAxes(h));
                printf("   "); feat(SDL_HapticQuery(h));
                SDL_HapticClose(h);
            }
        }
        if (j) SDL_JoystickClose(j);
    }
    SDL_Quit();
    return 0;
}
