# This fork: racing wheel, telemetry and VR

This is a fork of [xenophobentx/takedawn](https://github.com/xenophobentx/takedawn) that carries work not yet ready for
the original project: racing wheel support (a Thrustmaster T818 base and pedals), car telemetry, and a VR mode
(OpenXR, tested on a Quest 3 through SteamVR on Windows). It is built on a matching fork of the toolkit,
[xboxrecomp](https://github.com/scott-adamson1975/xboxrecomp). Both live on the branch `thrustmasterwheel`. The original
`main` (here) and `takedawn` (toolkit) branches follow the upstream projects.

What is in the branch, and where it is described:

| Work | Where |
|---|---|
| Wheel input, pedals, steering dead zone, force feedback spring, telemetry feed | [wheel-force-feedback-proposal.md](wheel-force-feedback-proposal.md), [controls.md](controls.md) |
| VR: seat camera, side-by-side stereo, OpenXR output, head tracking | [vr-openxr-proposal.md](vr-openxr-proposal.md) |

## Getting it

The game finds the toolkit as a sibling folder named `xboxrecomp`, so clone both side by side:

```sh
git clone -b thrustmasterwheel https://github.com/scott-adamson1975/xboxrecomp
git clone -b thrustmasterwheel https://github.com/scott-adamson1975/takedawn
```

To follow the originals as well, add them as `upstream` in each (`git remote add upstream <url>`) and rebase
`thrustmasterwheel` onto `upstream/main` (game) or `upstream/takedawn` (toolkit).

## Building (Windows, from Git Bash)

The Windows build is the cross build of [building.md](building.md), run from Git Bash:

```sh
cd takedawn
tools/win/fetch-deps.sh      # once: llvm-mingw, SDL2, Vulkan headers, glslang and the OpenXR headers, in ~/.cache/bo3-windeps
tools/win/build-mingw.sh     # writes bin/burnout3.exe (and SDL2.dll) beside it
```

`build-mingw.sh` passes the OpenXR headers to the toolkit (`XBOXRECOMP_OPENXR_INCLUDE_DIR`); without them the build
still works and `--vr` reports that the build has no OpenXR support. The OpenXR loader (`openxr_loader.dll`) and the
runtime (SteamVR) are the user's; nothing from them is shipped.

If the toolkit is not at `../xboxrecomp`, set `XBOXRECOMP_DIR`.

## Running the VR mode

Start SteamVR and put the headset on, then run `burnout3.exe --vr` from the game folder (the one with `default.xbe`).
The game waits up to 300 seconds for SteamVR and the headset (`RECOMP_VR_WAIT=<seconds>`). The launchers used while
developing are plain `.cmd` files in `bin/` (not in the repository); the settings they carry are:

| Setting | Why |
|---|---|
| `RECOMP_GFX_HDR=0` | the headset path copies the 8-bit picture raw |
| `RECOMP_RENDER_SCALE=6` | the picture drawn for each eye is half of a frame this size |
| `RECOMP_GFX_ANISO=16`, `RECOMP_GFX_AA=smaa` | filtering and anti-aliasing (the headset gets the post-processed frame) |
| `BO3_SEATCAM_NEAR=0.12` | the near plane; smaller shows more of the car (0.25 cut off the interior), larger removes z-fighting in the distance |
| `BO3_SEATCAM_FILE=%APPDATA%\Takedawn\seatcam.txt` | the per-car seat points |

Recentre the view with Tab (keyboard) or the pad's right stick pressed in; hold it for about 1.5 seconds to store where
your head is as the car's seat. Other settings (`RECOMP_VR_SWAP`, `RECOMP_STEREO_HUD_SCALE`, `RECOMP_STEREO_HUD_ROUND`,
`RECOMP_STEREO_HUD_BOTTOM`, `BO3_VR_CULL`, `RECOMP_STEREO_CAR_GLASS`) are described where they are read, in
`src/game/seatcam.c`, `oxr.c` and `nv2a_pb_vulkan.c`.

## The Wheel pages in the settings

TAKEDAWN SETTINGS (F1, or Back and Start) has two wheel pages.

**Wheel**: first the bindings, then the settings. A binding row (Steering axis, Throttle pedal, Brake pedal, D-pad, and the
buttons for A, B, Start, X, Y and the recentre) is set by pressing it (Enter, or A on a pad) and then using the control: turn
the wheel, press the pedal, push the D-pad, press the button. It waits ten seconds; Esc or B cancels, Backspace or Y clears the
binding. A pedal that rests high is bound as inverted by itself. After a binding is made the page asks for a restart. Below
them: the wheel's rotation and game lock, steering sensitivity, linearity and deadzones, each pedal's sensitivity and
linearity, and the force feedback (spring strength, how it follows the speed, the rest level, off outside races, off in the air
and light in a drift, rumble, engine vibration, hit weight).

**Wheel test**: every joystick that is there (a game pad is left out), its axes as bars, the buttons down and the hat, as you
use them; and under them what the game is given from the wheel after the lock, sensitivity and linearity (steering, throttle,
brake), so the curves can be judged by eye.

Everything is the `RECOMP_WHEEL_*` variables, kept as the keys `wheel.*` (`wheel.bind_steer` and so on for the bindings) in
`settings.toml`; a variable you set in a launcher still wins and shows its row locked (`bin\run-wheel-settings.cmd` sets none,
`bin\run-wheel-settings-bound.cmd` sets only the bindings). They are read when the game starts, so a change applies after a
restart.

Where the code is, kept apart from the shared files so that following the upstream stays easy (the shared files carry one
`#include` or a few one-line hooks for it): in the toolkit's `src/overlay/`, `xbox_settings_wheel_ids.inc`,
`xbox_settings_wheel_set.inc`, `xbox_settings_wheel_text.inc` (the settings), `overlay_wheel_meta.inc` (their labels and hints),
`overlay_wheel.h`, `overlay_wheel.inc` and `overlay_wheel_row.inc` (the two pages); in `src/input/`, `wheel_probe.h` and
`wheel_probe_sdl.c` (the joysticks and the capture, which the overlay reaches through a table of functions so it need not link
the SDL glue).
