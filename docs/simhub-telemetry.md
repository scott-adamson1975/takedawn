# Telemetry to SimHub

The game can send the player's car to [SimHub](https://www.simhubdash.com/) over UDP, for dashboards, LED rev lights, bass shakers and motion platforms. Off by default.

## How it is sent

SimHub has no generic UDP input for a game of its own, so the game speaks a format SimHub already reads: Forza Motorsport 7's "Data Out" in its **car dash** form (311 bytes, little-endian, 60 packets a second). In SimHub you choose the game **Forza Motorsport 7**; nothing else is installed.

## Settings

Open the settings (F1, or Back and Start on a pad) and go to the **SimHub** tab. The rows are saved in `settings.toml` and apply after a restart. A variable of the same name set in a launcher wins and locks its row.

| Row (settings.toml key) | Variable | Meaning |
|---|---|---|
| Send to SimHub (`simhub.enable`) | `RECOMP_SIMHUB=1` | On. Off sends nothing and runs nothing. |
| UDP port (`simhub.port`) | `RECOMP_SIMHUB_PORT` | The port the packets go to (default 8000, the port Forza Motorsport 7 sends its Data Out to). |
| (`simhub.host`, settings.toml only) | `RECOMP_SIMHUB_HOST` | The IPv4 address to send to; empty is this computer (127.0.0.1). Set it to send to another PC. Not on the page: there is no text entry there. |
| Made-up rpm and gear (`simhub.made_up_rpm`) | `RECOMP_SIMHUB_RPM=0` turns it off | See below. |
| Gravity in heave (`simhub.gravity`) | `RECOMP_SIMHUB_GRAVITY=1` | Adds 9.81 to the vertical acceleration, if SimHub expects gravity in it (a parked car should read no heave, not a fall). |
| Flip pitch / roll / yaw (`simhub.flip_pitch`, `_roll`, `_yaw`) | `RECOMP_SIMHUB_FLIP_PITCH`, `_ROLL`, `_YAW=1` | Turn that angle's sign round, if SimHub shows it backwards. Pitch (the angle and its rate) is already sent reversed from the car's own sign, because it showed backwards in SimHub; turning it on sends the car's own sign. |
| | `RECOMP_SIMHUB_LOG=1` | Log a line a second while sending (`[SIMHUB]` in the log). |

Packets go out only while a race runs (the snapshot is fresh); one "race off" packet when it stops, then nothing.

## What is in it

From the shared car snapshot (`src/game/carstate.c`, the same one the wheel's force feedback reads): the orientation (pitch, roll and yaw angles, in radians, from the car's matrix: pitch positive nose up, roll positive right side down, yaw the heading), speed, the accelerations (forward, sideways, up), the turn rates, wheel spin, wheel slip, throttle, brake, steering, world position, distance driven, race time, and the impacts (as the surface rumble, decaying over about a quarter of a second, for the bass shakers).

**Made up:** rpm and gear. Burnout 3 has no gearbox and the game's car has no rpm that was found, so both come from the speed: five bands over 0 to 70 m/s with the rpm rising from 3000 to 8000 through each, 1000 at a stop. Rev lights and engine shakers then do something sensible.

**Not sent (zero or fixed):** suspension travel (mid-travel), rumble strips, puddles, tyre temperatures, boost, fuel, lap data and race position, power and torque. The car identity is fixed (ordinal 1, class 0, all-wheel drive, 8 cylinders).

## Unverified

Written from the documented packet layout and a check of the offsets (a 311-byte packet with each field where the format puts it), not yet run against SimHub: the signs of the angles and accelerations (they follow the snapshot's own conventions: check pitch and roll in SimHub's motion preview, braking should pitch the nose down and a right turn lean the platform left, left side down, whether from the roll or from the sideways acceleration, and use the flip rows if one is backwards; the roll angle itself is only the car's own tilt, which is small in corners), the wheel order of the spin (assumed FL, FR, RL, RR), whether SimHub expects gravity in the heave, and whether SimHub's Forza profile accepts the fixed car identity. Report what a dashboard or shaker shows against the car.

## Where the code is

Game repo: `src/game/simhub_udp.c` (the sender, one thread) and `src/game/simhub.h`; hooks are one `#include` and one call in `main.c` (next to `bo3_wheel_feed_start`) and two lines in `src/game/CMakeLists.txt`. Toolkit: the page is `src/overlay/overlay_simhub.inc` (and `.h`), the settings are `xbox_settings_simhub_ids.inc`, `_set.inc`, `_text.inc` and `overlay_simhub_meta.inc`, the variables are the `RECOMP_SIMHUB_*` rows of `src/platform/recomp_env.h`.
