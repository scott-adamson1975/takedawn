# Force feedback for a racing wheel: feasibility and proposal

Status: **investigation done; option A's centring spring is implemented** (see
[controls.md](controls.md), "Force feedback: the centring spring"), untested on the
wheel at the time of writing. Everything else below is still a proposal. Written 2026-10-29.

Tested on the T818 (2026-10-29): the spring pulls toward the centre once its
coefficient sign was fixed; strength 5 was far too weak, **15 is the working
value** (higher is untried). The spring is the same at every speed and in menus
on purpose: scaling it with the car's speed, and the rest of the true force
feedback (cornering force, impacts, road texture), wait for the shared car-state
reader and for finding the game's physics fields (phases 0 and 3 below). Related:
[controls.md](controls.md) ("Racing wheel and pedals") and the telemetry
investigation (motion platform), summarised under "Cross concerns with telemetry"
below because it has not been written up on its own yet.

## Goal

Make the T818 (a 10 Nm direct-drive base) push back like a car: self-centring, a
sense of grip and load in corners, road and kerb texture, and impacts. Today the
game sends the wheel no force at all.

## Verdict

Possible, and the device side is better than hoped. The hard part is not the
wheel, it is that **the game has no force-feedback concept**, so every force has
to be synthesised from the car's state. That is the same data the telemetry work
needs, so the two should share one source. The first useful steps need no game
data at all (a centring spring and damping), and are low risk at low gain.

## What the device offers (measured)

`tools/win/wheel-haptic-probe.c` (read-only, plays no effect) on this PC, SDL 2.32.10:

- Wheel base `044F:B696` "Thrustmaster Advanced Mode Racer": **haptic = yes**,
  one force axis (steering), up to 128 effects.
- Effect types supported: constant, sine, triangle, saw up and down, ramp,
  **spring, damper, inertia, friction**, custom, gain, status, pause.
- Not supported: left/right rumble, and **autocenter is not reported**, so
  centring has to be a spring effect.
- The pedals (`044F:B6FC`) have no haptics.
- `044F:B660` is a **"T500 RS Gear Shift"**, a shifter (2 axes, 10 buttons).
  Earlier notes called it a phantom device; it is the shifter, with a button
  held at rest. Burnout 3 has no gears, so nothing maps to it.

Spring, damper, friction and inertia run **inside the base**, so they cost no
latency from this program. Constant force and periodic effects are updated from
the PC.

## What the game and toolkit already do

- The title can only ask for **rumble**: left and right motor speeds, 16 bits
  each, in the Xbox pad's USB output report (`src/usb/usb_gamepad.c`, "the
  output report: rumble"). They go to `xbox_InputSetState`, through a rumble
  curve (`src/input/rumble_curve.h`: gate, strength, a 90 ms release) and then
  to a backend. The wheel is not a rumble target today.
- The game's rumble carries a quiet **engine hum of about 1 % of full**, which
  the curve's gate treats as silence ([android.md](android.md)). Impacts and the
  like are louder. Which game events produce which speeds has **not been
  measured**.
- Safety plumbing already exists for rumble and should be reused for force:
  motors zeroed at exit, by a signal, or on a crash (`input_shutdown`,
  `xbox_InputPanicStop`, `src/input/input_backend.h`), silenced when the window
  loses focus (`xbox_InputSetBackground`) and while the overlay is open
  (`xbox_InputSetBlocked`).

## Options, cheapest first

| Option | What it gives | Needs game data? |
|---|---|---|
| A. Spring + damper + friction in the base | A wheel that centres and has weight; nothing is limp | No |
| B. Software-lock stop | A spring "wall" at the software lock, so a 360 degree lock feels like a real stop (see [controls.md](controls.md): the base itself stays at 900) | No |
| C. Forward the title's rumble | Vibration on collisions and similar, as a sine or periodic effect | Only the rumble the title already sends |
| D. Synthesised steering force | Self-aligning torque from cornering: lighter when sliding, heavier with speed | Yes: speed, lateral acceleration, yaw rate, steering input |
| E. Impacts and road texture | One-shot jolts on collisions, kerb and surface rumble | Yes: collision events, surface |

Burnout 3 is an arcade game with no tyre model that has been found, so D is an
approximation, not a replica of what the car "feels".

## Where the code would go

A split that keeps each repo to what it knows:

- **Toolkit** (`src/input`): the device layer. A pure header for force maths
  (scaling, slew limits, gain, clipping), tested without a device, and an SDL
  glue file that opens the haptic interface and creates and updates the effects.
  It would offer a small API: centring strength, damping, a lock wall, a constant
  force, a vibration, an impact. It must not know anything about Burnout.
- **Takedawn** (`src/game`): the physics side. It reads the car, computes the
  numbers, and calls that API. This is also where "is the player driving" is
  decided (`ctl_driving`).

## Safety (this is a 10 Nm direct-drive base)

- Start with a low default gain and ramp forces up and down; never step a force.
- Limit how fast a force may change (slew rate) and clip, with a hard maximum.
- Force only while the title is taking driving input. Fade to zero in menus,
  pause, results, loading, the overlay, and when the window is not in front.
- Decide explicitly what happens in crash cameras and aftertouch, where the
  car's motion is violent and the player is not steering a normal car.
- Reuse the existing exit, signal and crash paths so a force never outlives the
  process. Check what the Thrustmaster driver does with an effect left running.
- Keep a documented way to switch it off (a setting and an environment variable).

## Technical risks

- SDL's haptic on Windows goes through DirectInput. It worked in the probe, but
  playing and updating effects has not been tried here. Whether it conflicts with
  the Thrustmaster Control Panel's own gain and damping, or with the joystick
  being read at the same time, is unknown.
- Thread: effect calls from the title's thread versus the window thread.
- Update rate. The game steps at 60 Hz; a constant force updated at 60 Hz may feel
  steppy. Hardware-resident spring and damper avoid this.
- The T818's quirks (torque scaling, effect limits) are unknown beyond the probe.
- Feel can only be judged by the owner, by driving.

## Cross concerns with telemetry (motion platform via SimHub)

The telemetry investigation found that the same car state feeds both, so one
sampler should serve both outputs (a UDP feed for SimHub, and forces for the wheel).

What both need, and where it already exists (addresses are from existing docs
and code, see [high-refresh.md](high-refresh.md), [softbody.md](softbody.md),
[autopilot.c](../src/game/autopilot.c), [main.c](../src/game/main.c)):

| Data | Where | Used by |
|---|---|---|
| Player 1's car object | `0x73A1D0` | both |
| Speed, m/s (magnitude) | `car + 0xBC` | both |
| World matrix (orientation, position) | `car + 0x10` | both: pitch/roll, yaw rate, accelerations |
| Wheel matrices | `car + 0x90 + 0x40 * wheel` | both: surface and suspension cues |
| Collision events (point, direction, strength) | `sub_0012FA40` | both: impacts, motion jolts |
| Driving / paused / crash gating | `ctl_driving()` in `controls.c` | both |
| Throttle, brake, steering inputs | pad records at `0x4AE724` | force synthesis |

Shared concerns:

1. **One sampler, read at the end of each game step.** The high-refresh mode
   blends the car's matrix during drawing, so reading it mid-draw gives wrong
   values. Both consumers should read a snapshot published after the step.
2. **Unknown fields help both.** Velocity vector, angular velocity, lateral slip,
   suspension travel, an airborne flag and surface type have not been found. Finding
   them (reverse-engineering the physics body) would improve both outputs; deriving
   them by differencing the matrix is the fallback, and is noisy.
3. **Discontinuities.** Resets and respawns after a crash look like teleports and
   would produce huge spikes in both a derived acceleration and a force. Filter them.
4. **Violent events.** Takedowns can roll the car through full turns. A motion
   platform and a direct-drive wheel both need clamps and a decision about crash
   cameras and aftertouch. The safety rules above and the platform's travel limits
   should be designed together.
5. **Time dilation.** Aftertouch and some crash phases run the world slowed (see
   [crash60.md](crash60.md): a divisor of 5). Accelerations and forces derived from
   steps will shrink with it; decide whether that is wanted for each output.
6. **Steps, not frames,** and **player 1 only** (split screen is out of scope for both).
7. **Settings and threading.** One place to publish the snapshot (lock-free read),
   one set of settings, one gate for "driving", rather than two copies.
8. **Software lock and the wheel.** The lock wall (option B) is a force-feedback
   feature, but it touches the steering settings in [controls.md](controls.md).

## Suggested phases

0. **Measure.** *Started: `BO3_TELEMETRY=<prefix>` (`src/game/telemetry.c`) records
   player 1's car every frame of a race to a `.csv` and a raw car dump `.bin`, and
   `tools/telemetry/find_fields.py` searches the dump for the unknown physics fields
   (velocity, acceleration, angular velocity, flags). Its self-test passes on planted
   fields; no real recording has been analysed yet. `bin/run-telemetry.cmd` runs it.*
   With `RECOMP_TRACE=input_diag`, also log the title's rumble speeds in a
   race and a crash, next to the car state per step (the speed log is the model).
   Use the built-in autopilot for repeatable runs. This feeds both investigations.
1. **Device layer prototype.** Centring spring, damper and the lock wall at low gain,
   no game data. Proves DirectInput effects work on this base and gives a feel
   baseline.
2. **Forward the title's rumble** as a vibration effect.
3. **Shared car-state sampler** (the first phase of the telemetry work), then
   **synthesised steering force** from it.
4. **Impacts and road texture,** then settings (overlay page, gain, curves).

## Not verified

- No effect was ever played; only the capabilities were read.
- Which in-game events produce which rumble speeds.
- The T818's behaviour under rapid effect updates, and its interaction with the
  Thrustmaster driver and Control Panel.
- The car-state addresses come from existing docs; none was re-checked in a race.
- Nothing was felt on the wheel.

## Where to resume

1. Finish tuning the wheel's buttons, lock and curve ([controls.md](controls.md)).
2. Decide whether to start with phase 0 (shared with telemetry) or phase 1.
3. Toolkit work goes in `C:\source\xboxrecomp-takedawn` (branch `takedawn`).

## Findings: the car's physics fields (first real recording, 2026-10-29)

One 68 s race recorded with `bin/run-telemetry.cmd` (4528 frames, player 1's car at
`0x73A1D0`), searched with `tools/telemetry/windowed_fields.py` (position differenced
over 4 frames, then every slot of the car object correlated against it). Offsets are
from the car object.

| Offset | What it holds | Evidence |
|---|---|---|
| `+0x10` | world matrix, position at `+0x30` of it (car `+0x40`; plausible world metres) | position moves ~17-50 m/s |
| `+0x64` | **speed, m/s, a magnitude**: it stays at or above 0 while the car reverses (the matrix shows backing up to 25 m/s). The sign comes from the wheel spin (below), which is negative in reverse (mean about -8 rad/s, against +170 forward) | r = 0.94 against the car-frame forward velocity, fitted scale 1.005 |
| `+0x25D0`, `+0x25D8` | **pitch and roll angular velocity**, radians per step (x60 for rad/s). Pitch: positive is nose down. Roll: positive is the **right side going down** (matrix row 0 is the car's LEFT, see below). | r = -0.93 (pitch) against the rates from the matrix; roll follows row 0's side rising at r = 0.60 |
| `+0x25D4` | **not a yaw rate** (the first recording suggested it, r = 0.90, but only crash spins drove that): it stays near 0 through ordinary cornering. Yaw rate is derived from the matrix's heading instead, positive turning right, checked against the steering (r = +0.56). | |
| `+0x98` / `+0xD8` | the front wheels' matrix element that follows the steering angle (opposite signs per side): a steering-angle source for force feedback, not used yet | r = 0.67 against yaw rate |
| `+0x578`, `+0x57C`, `+0x580`, `+0x584` | probably **wheel spin**, one slot per wheel (rad/s): scale 0.35 m, the radius of a wheel | r = 0.98 against forward speed (0x578 only 0.87); spin runs about 15% above speed in corners |
| `+0xBC` | **not** the speed on this object (reads 0); it was the speed on the HUD's own object at `0x667E90` | |
| `+0x90 + 0x40 * w` | wheel matrices: **car-local** offsets, but the title swaps in world positions while it draws | |

Not found: lateral (sideways) velocity as a stored field (derive it from the matrix),
suspension travel, an airborne flag. Slots that take few values (flag candidates) are
listed by `find_fields.py`; none is identified yet. Lateral and vertical acceleration
can be derived from `+0x64`, the yaw rate and the matrix.

What this gives the two outputs: SimHub's surge (from `+0x64`'s change), sway (speed times
yaw rate), pitch and roll rates (`+0x25D0`, `+0x25D8`), yaw rate, and wheel spin
against speed (a slip hint) are all stored by the game, not differenced. Not yet
verified: the four wheel slots' meaning, units across a takedown or a reset, and
what happens in aftertouch.

## The shared car state (built, 2026-10-29)

`src/game/carstate.c` is the one reader both outputs use (nothing consumes it yet
except the telemetry recorder):

- **Sampled** once a game step from the `drive_log` mark (the HUD speedometer's update,
  so only while a race's HUD runs), from player 1's car at the fixed `0x73A1D0` while its
  phase is 1 to 3. The HUD's own "car" pointer is not used (it pointed at `0x667E90`).
- **Impacts** come from the collision entry (`sub_0012FA40`, the `sb_impact` mark), player 1's
  car only: strength and direction in the car's frame. A mark runs only its first handler, so
  `softbody.c`'s handler calls `bo3_carstate_impact` too, and `bo3_carstate_start` (called after
  softbody's) registers the mark itself when soft-body damage is off.
- **Published** as a `bo3_carstate` snapshot through a seqlock: `bo3_carstate_get(&out, max_age_ms)`
  from any thread, no lock; it returns 0 when there is no race or the HUD's update has
  stopped (a pause, a menu), which is the "fade the forces out" signal. A consumer calls
  `bo3_carstate_request()` before start-up, so nothing runs for a build that reads it nowhere.
- **Maths** in `carstate_math.h` (pure, `tests/carstate`, 23 checks): speed, rates in rad/s (turning
  right, nose up, right side down), surge (change of speed), sway (speed times yaw rate), vertical
  velocity and acceleration, a wheel-slip fraction. Filtered (time constants 30 to 80 ms) so a frame
  without movement is no spike; a gap in time, a position jump (respawn) or a phase change restarts
  them (`accel_ok` is 0 for that sample).

Replayed on the 68 s recording (`BO3_CARSTATE_REPLAY=<prefix> bo3_carstate_test`): the middle half
of the race's forward acceleration lies between -3 and +6 m/s^2, braking reaches -15, and wall hits
reach hundreds (35 to 10 m/s in 8 frames). **Consumers must clamp**: a platform or a wheel should
never be fed a raw crash.

`BO3_TELEMETRY` now also writes the derived values beside each frame (`cs_*` columns) and
`<prefix>-impacts.csv` (flip, strength, direction in the car's frame), so a second recording shows
both the filters and the collision events. Not yet seen: what an impact's strength is in units, how
scrapes look (they may call it every frame), whether other cars' hits on player 1 come through, and
the car's behaviour in aftertouch.

### Second recording, with a pad (`bin/run-telemetry-pad.cmd`, 103 s, 6535 frames)

Run through the live path (not just replayed): all frames in play, derived columns filled.
It corrected the yaw slot (above). Impacts: 114 records in one cluster (a long scrape or
a pile-up), strengths from 27 to 11112 (median 179), directions in the car's frame, with
the speed falling from 73 to 65 m/s over the next 10 frames. The strength has no known
unit; treat it as a relative size and compress it (a log or a cap) before it drives a
force. Wall hits and scrapes arrive as a run of calls, one or more a frame.

Still open: roll's sign (the body leaning in a turn gave weak, mixed correlations),
reversing, and whether the title's speed lags the wheels' (wheel spin tracked the matrix's
speed more closely, r 0.97 against 0.93).

### Third recording, with a pad (137 s, 10432 frames): reversing, grip corners, crashes

- **Reversing:** the title's speed (`+0x64`) never goes negative; the wheel spin does (mean
  about -8 rad/s reversing, 65% of reversing frames with all four negative; forward about +170).
  `carstate_math.h` now reports a signed speed: the title's magnitude with the sign of the
  wheels' mean spin (below -1.5 rad/s). Wheel spin can spike (a reading of +126 was seen
  while reversing), so a few frames of a wrong sign are possible.
- **Which side is matrix row 0:** the car's **left**. The real sideways acceleration in turns
  correlates -0.67 with the acceleration along row 0, and the body leans the realistic way in grip
  corners (right turns: row 0's side down, mean row-0 vertical -0.019; left turns +0.021). With it
  the roll sign is settled: stored roll positive = right side down. Yaw (from the heading) follows
  the steering at r = 0.73 in grip corners; pitch at r = -0.935 against the matrix.
- **Impacts:** 410 records in 5 clusters (each a run of 5 to 145 calls). The strength does **not**
  scale with the speed lost: the clusters' peak strengths are 10 000 to 24 000 for speed losses of
  21 to 31 m/s (r = 0.16). Treat an impact as an event with a compressed, capped size rather than as
  a measurement; for how hard the car stopped use the change of speed (surge), which the snapshot
  already has.
- Not recorded yet: a takedown, boost.

## Built: the spring follows the car (2026-10-29)

Phase 3 started with the two simplest pieces, both opt-in and both only lowering the
existing spring (see [controls.md](controls.md), "The spring follows the car"):
`RECOMP_WHEEL_FFB_SPEED` (strength by speed, down to `RECOMP_WHEEL_FFB_REST` at a standstill)
and `RECOMP_WHEEL_FFB_RACE_ONLY` (off outside a race). The toolkit has a generic car feed
(`xbox_InputWheelSetCarSource`, a callback returning "racing" and the speed); the game's
`wheelfeed.c` fills it from the car snapshot. The maths is in `wheel_pad.h` (`xbox_wheel_level_*`,
27 new checks in the toolkit's `input_wheel` test); the spring effect is updated in place on the
base (`SDL_HapticUpdateEffect`), only for a change of about 1 percent.

**Not tested on the wheel**: whether the T818 takes an in-place update of a running spring
without a click, how the speed scaling feels (the 40 m/s reference and the 30 percent rest are
guesses), and whether the pause is long enough to fade (a race counts for 0.3 s after the HUD's
last update). Cornering load and impact kicks are next, and need the feel tested first.
