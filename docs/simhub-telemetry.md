# Telemetry to SimHub

The game can send the player's car to [SimHub](https://www.simhubdash.com/) over UDP, for dashboards, LED rev lights, bass shakers and motion rigs. Off by default.

## How it is sent

SimHub has no generic UDP input for a game of its own, so the game speaks a format SimHub already reads: Forza Motorsport 7's "Data Out" in its **car dash** form (311 bytes, little-endian, 60 packets a second). In SimHub you choose the game **Forza Motorsport 7**; nothing else is installed.

Set in the environment before starting the game (a launcher, for example):

| Variable | Meaning |
|---|---|
| `BO3_SIMHUB_UDP=5300` | On. A port alone sends to this computer (127.0.0.1); `192.168.1.20:5300` sends to another one. Use the port SimHub listens on for Forza (its game settings show it). |
| `BO3_SIMHUB_RPM=0` | Send rpm 0 and gear 1 instead of the made-up values (below). |
| `BO3_SIMHUB_LOG=1` | Log a line a second while sending (`[SIMHUB]` in the log). |
| `BO3_SIMHUB_FLIP=pry` | Turn the sign of the pitch (`p`), roll (`r`) or yaw (`y`) angle round, if SimHub shows one backwards. |
| `BO3_SIMHUB_GRAVITY=1` | Add 9.81 to the vertical acceleration, if SimHub expects gravity in it (a car at rest should read 0 heave, not a fall). |

Packets go out only while a race runs (the snapshot is fresh); one "race off" packet when it stops, then nothing.

## What is in it

From the shared car snapshot (`src/game/carstate.c`, the same one the wheel's force feedback reads): the orientation (pitch, roll and yaw angles, in radians, from the car's matrix: pitch positive nose up, roll positive right side down, yaw the heading), speed, the accelerations (forward, sideways, up), the turn rates, wheel spin, wheel slip, throttle, brake, steering, world position, distance driven, race time, and the impacts (as the surface rumble, decaying over about a quarter of a second, for the bass shakers).

**Made up:** rpm and gear. Burnout 3 has no gearbox and the game's car has no rpm that was found, so both come from the speed: five bands over 0 to 70 m/s with the rpm rising from 3000 to 8000 through each, 1000 at a stop. Rev lights and engine shakers then do something sensible. `BO3_SIMHUB_RPM=0` turns it off.

**Not sent (zero or fixed):** suspension travel (mid-travel), rumble strips, puddles, tyre temperatures, boost, fuel, lap data and race position, power and torque. The car identity is fixed (ordinal 1, class 0, all-wheel drive, 8 cylinders).

## Unverified

The angles' and accelerations' signs follow the snapshot's own conventions; check pitch and roll in SimHub's motion preview (braking pitches the nose down, a right turn rolls right) and use `BO3_SIMHUB_FLIP` if one is backwards. Written from the documented packet layout and a check of the offsets (a 311-byte packet with each field where the format puts it), not yet run against SimHub: the axis signs of the accelerations, the wheel order of the spin (assumed FL, FR, RL, RR), and whether SimHub's Forza profile accepts the fixed car identity. Report what a dashboard or shaker shows against the car.

## Where the code is

`src/game/simhub_udp.c` (the sender, one thread) and `src/game/simhub.h`. Hooks: one `#include` and one call in `main.c` (next to `bo3_wheel_feed_start`), two lines in `src/game/CMakeLists.txt` (the file, and Winsock on Windows).
