# Driving controls: Xbox layout or PS2 layout

`input.control_scheme` (overlay: Input > Driving controls; settings.toml;
`RECOMP_INPUT_CONTROL_SCHEME=xbox|ps2`) chooses the controls used **while
driving**. `xbox`, the default, is the game as shipped. `ps2` is the
PlayStation 2 version's layout. Menus are not changed by it: the front end,
the garage, the pause menu, a crash event's captions, the results and the
overlay read the pad as they always do. A change in the overlay holds from
the next frame.

## The two layouts

From the two NTSC-U manuals ("Complete Controls"), and for the Xbox column
also from the title itself (below).

| Action | Xbox (as shipped) | PS2 | PS2 layout on an Xbox-shaped pad |
|---|---|---|---|
| Accelerate | Right trigger | Cross, or right stick up | bottom face button (A), or right stick up |
| Brake / reverse (when stopped) | Left trigger | Square, or right stick down | left face button (X), or right stick down |
| Steering | Left stick | Left stick | left stick |
| Look back | X | L1 | left shoulder (LB) |
| Change view | Y | Triangle | top face button (Y) |
| Boost | A | R1 | right shoulder (RB) |
| Impact time (while crashing) | hold A | hold R1 | hold right shoulder (RB) |
| Aftertouch (while crashing) | Left stick | Left stick | left stick |
| Crashbreaker (crash events) | B | R2 | right trigger |
| Next music track | Black (RB on a modern pad) | L2 | left trigger |
| Pause | Start | Start | Start / Menu |
| (nothing) | White (LB), right stick | Circle | right face button (B) |

Neither version has a horn. The Xbox manual does not list the music track
button; in a race Black brings up the EA Trax banner with the next song (and the
log shows the music bank opened again); White opened nothing. The Xbox version does not read the right stick
for the pedals (a race with the stick held up or down only coasts), so in the
PS2 layout the host turns the stick's travel into the triggers.

Sources: Xbox manual, pages 2 and 4,
<https://archive.org/details/Burnout_3_Takedown_2004_Electronic_Arts>; PS2
manual, pages 3 and 7,
<https://archive.org/details/ps2_Burnout_3-_Takedown_USA>.

### Analog and digital

- On the original Xbox pad the triggers and all six face buttons are
  pressure-sensitive; on the DualShock 2 so are Cross and Square, which is
  how the PS2 version has an analog throttle on a button. A modern pad's face
  buttons and shoulders are switches. So in the PS2 layout the two face-button
  pedals are all or nothing (the title is given the trigger at 255), and the
  analog throttle and brake are on the right stick, as on the PS2: the first
  quarter of its travel reads as nothing, the rest runs from 1 to 255.
- The triggers stay analog on the pad but drive two switches here
  (crashbreaker, next track): pressed from a pull of more than 30 of 255, and
  the title is given the button at 255.
- What the PS2 layout gives up against the Xbox one: the analog pedals under
  the index fingers.

## The keyboard

The keyboard is a pad for player 1 (the toolkit's `src/input/keyboard.c`, on
by default on Mac, Windows and Linux; `RECOMP_KEYBOARD=0` turns it off, and on
Android it is off unless `RECOMP_KEYBOARD=1`). The map is fixed. It always has
the Xbox layout: while a key is held the PS2 layout is not applied, so these
are the Xbox buttons whatever Driving controls says. A controller works beside
it: buttons are ORed, a trigger is the larger of the two, and a held key sets
its stick axis.

| Key | Xbox pad |
|---|---|
| W or Up | Right trigger (accelerate) |
| S or Down | Left trigger (brake, reverse) |
| A or Left | Left stick left (steer) |
| D or Right | Left stick right (steer) |
| Up, Down, Left, Right | D-pad |
| Space | A (boost, select) |
| Backspace | B (Crashbreaker, back) |
| Shift | X (look back) |
| Ctrl | Y (change view) |
| Q | White |
| E | Black (next music track) |
| Enter | Start (pause, select) |
| Esc | Back; Start while driving (pause) |

Esc pauses a race, like in a PC game. While you drive (the same test as the
PS2 layout's, below) a press of Esc is Start for as long as it is held, so the
pause menu is not closed by the press that opened it. Everywhere else, the
pause menu included, Esc is Back as before. An Esc that was already down when
the race began (held through the countdown) is still a Back. Enter pauses as
well. Neither manual gives Back a use in a race, so nothing is lost, and a
controller's Back is not touched (Back and Start together still open the
settings on a pad, F1 on a keyboard).

The settings change pages with Q and E (like the bumpers), Tab and Shift+Tab, or
Page Up and Page Down, which a Mac keyboard does not have. Cmd, Ctrl or Alt
with Q or E is left to the system.

The arrows are the D-pad and also steer, accelerate and brake. The menus take
the D-pad and the stick as one direction, so an arrow is one step in a menu
(checked: one Right arrow moves Region by one place, and a
100 ms Down moves one line; a key held for about 300 ms repeats, as a held pad
does). F1, and Cmd and comma on a Mac, are the settings'; while they are open the
title sees no keys, and the keys held when they close stay unseen until let go.

### Keyboard and controller as two players

By default the keyboard and the first controller both drive player 1. With
Input > First controller at Player 2 (`input.first_pad = "player2"`,
`RECOMP_INPUT_FIRST_PAD=player2`) the keyboard is player 1 alone and the
first controller to press a button is player 2, so two people can play
split screen with one keyboard and one controller. Needs a toolkit with
`input.first_pad`.
The PS2 driving layout follows the same split: in the two-player stage that
controller goes by player 2's event, and in the one-player stage and Party Crash
it goes by player 1's, so a lone controller keeps the PS2 layout there.

## Racing wheel and pedals (Windows, Linux, Mac; SDL hosts)

A wheel base and its pedals are plain HID joysticks, not game controllers, so
they are bound by hand, one environment variable per control, as
`VID:PID:AXIS` (the USB ids in hex, then the axis index, in SDL's order). A
trailing `!` is for a pedal that reads high when released. The steering and the
pedals may be two devices. Setting any one of the three turns the wheel on;
they merge into player 1 like the keyboard does (a trigger the larger of the two).

| Variable | Control | Default |
|---|---|---|
| `RECOMP_WHEEL_STEER` | steering: the left stick's X | off |
| `RECOMP_WHEEL_THROTTLE` | throttle: the right trigger | off |
| `RECOMP_WHEEL_BRAKE` | brake and reverse: the left trigger | off |
| `RECOMP_WHEEL_RANGE` | degrees the base turns lock to lock | 900 |
| `RECOMP_WHEEL_LOCK` | degrees lock to lock the game gets, up to the range | 540 |
| `RECOMP_WHEEL_DEADZONE` | pedal deadzone, percent of travel | 4 |
| `RECOMP_WHEEL_STEER_DEAD` | steering deadzone, percent of the stick's travel after the lock (the wheel inside it reads centred) | 0 |
| `RECOMP_WHEEL_STEER_SKIP` | the band around the stick's centre the game ignores, percent; the wheel jumps over it | 0 |
| `RECOMP_WHEEL_LOG` | `1`: log the devices opened and the raw axes | off |

Example, a Thrustmaster base (`044F:B696`) with separate pedals (`044F:B6FC`)
whose axes are X (throttle) and Z (brake):

    set RECOMP_WHEEL_STEER=044F:B696:0
    set RECOMP_WHEEL_THROTTLE=044F:B6FC:0
    set RECOMP_WHEEL_BRAKE=044F:B6FC:2

The axis index is SDL's, which need not be the order Windows' game controller
panel shows. Run with `RECOMP_WHEEL_LOG=1`: it prints each device it opens
(name, axis count) and, once a second while a control moves, the raw values and
what the title is given. A wheel is always the Xbox layout, whatever Driving
controls says: its pedals are the triggers.

### Buttons and the hat

Each button is bound as `VID:PID:N`, N being the joystick's button index from 0
(the hat's index is usually 0). A button is the Xbox button the title acts on:

| Variable | Xbox button | What it does |
|---|---|---|
| `RECOMP_WHEEL_BTN_ACCEPT` | A | confirms in menus; boost in a race; impact time in a crash |
| `RECOMP_WHEEL_BTN_BACK` | B | back in menus; the Crashbreaker |
| `RECOMP_WHEEL_BTN_PAUSE` | Start | pauses a race |
| `RECOMP_WHEEL_BTN_LOOKBACK` | X | look back |
| `RECOMP_WHEEL_BTN_VIEW` | Y | change view |
| `RECOMP_WHEEL_HAT` | D-pad and left stick | menus; a crash's aftertouch |

The hat is both the D-pad and the left stick, as the keyboard's arrows are,
because the aftertouch after a crash is steered with the left stick and the
D-pad does nothing there. Its up and down are the stick's Y, which driving does
not use. Its left and right are the stick's X only while the wheel's own
steering is within about a quarter of full lock (a wheel held straight still reads a little off centre), so the hat never fights a wheel you are turning.

`tools/win/wheel-buttons.ps1` asks for each control in turn and prints the
lines to use. With `RECOMP_WHEEL_LOG=1` the game logs every button of an open
device going down or up by the index SDL gives it (and the hat), bound or not;
this is how to check a number against the physical button.

### The title's dead spot

The title ignores a band around the left stick's centre (a pad stick has play), so a
wheel with a linear mapping does nothing for its first degrees: about 20 degrees at a
360 degree lock, reported on a T818. `RECOMP_WHEEL_STEER_SKIP=<percent>` makes the wheel
jump over that band: past the centre, the stick starts at that percent and rises to full
at full lock, so the first degrees already steer. The band is a share of the stick's
travel: 20 degrees at a 360 degree lock (180 degrees each way) is 20/180, about 11, so
`RECOMP_WHEEL_STEER_SKIP=11` is the first thing to try; change it by feel (too much
and the car turns in with a jerk as soon as the wheel leaves the centre, too little and
a dead spot is left).

It needs `RECOMP_WHEEL_STEER_DEAD` too: a base that rests a little off centre (a T818
reads about 4% of the stick at a 360 degree lock, 1.6% of the base's travel) would
otherwise steer at rest as soon as the dead band is skipped. Try 5, and raise it if the
car creeps at rest; the wheel inside it reads centred, and what is left of the travel is
stretched to full. Both are 0 (the plain linear map) unless set. The title's band was
not measured; the 11 comes from the felt 20 degrees.

### Force feedback: the centring spring

`RECOMP_WHEEL_FFB_SPRING=<percent>` (default 0, off; never above 50) gives the
base a centring spring that it runs itself, so it adds no latency. The number
is the force at the software lock's edge (`RECOMP_WHEEL_LOCK`) as a percentage
of the base's maximum; beyond the edge the force holds, it does not grow, so the
spring is never a wall. Start low (10) and raise it. Needs `RECOMP_WHEEL_STEER`
and a base that offers a spring and a gain (the T818 does).

It is passive (it only resists), fades in over 1.5 s after the start, and goes
quiet at once, over about 150 ms, while the overlay is open, while the game's
window is not in front, and when the game ends (the same rule as the title's
rumble). If the base cannot do a spring or a gain, or the spring is refused, it
stays off and the log says why (`[WHEEL] force feedback: ...`). A runaway guard
cuts the force if the wheel stays near an end of the base's travel for 1.5 s
with the spring on (a spring pulls to the centre, so only a hand or a force in
the wrong direction keeps it there), and lets it back when the wheel is back
inside half the travel. The first version had the left half's coefficient
negated, which made that half push the wheel out to the end stop; the guard
exists because a unit test cannot see which way a real base pushes. The game sends
the wheel no other force. Not tested here: what the base does if the game is
killed while a spring is running (the driver should let the effect go with the
process).

#### The spring follows the car (opt-in)

With the spring on, two more settings let it follow what the car is doing. The game
tells the toolkit, each poll of the pad, whether a race is running and how fast the
car goes (`src/game/wheelfeed.c`, from the shared car snapshot in
[wheel-force-feedback-proposal.md](wheel-force-feedback-proposal.md)). Both are
off by default, and both only ever lower the spring, never raise it above
`RECOMP_WHEEL_FFB_SPRING`:

- `RECOMP_WHEEL_FFB_SPEED=<m/s>`: the speed at which the spring has its whole
  strength; slower, it is weaker, linearly, down to `RECOMP_WHEEL_FFB_REST`
  percent of it (default 30) at a standstill. 40 m/s is about 145 km/h. A
  parked car then has a light wheel and a fast one a firm one.
- `RECOMP_WHEEL_FFB_RACE_ONLY=1`: outside a race (menus, the pause menu, the
  results, a loading screen) the spring fades out, and back in when the race
  runs. A race counts while the HUD's update has run in the last 0.3 s.

The level moves at most 1.0 in a second up and 0.4 s down, so a speed change or
a pause is never a step in the force, and the spring's own strength is rewritten
on the base only for a change of about 1 percent. The runaway guard and the
mute/fade rules above still apply. Without a feed (a build of the toolkit with
another game) the spring is as before. `RECOMP_WHEEL_LOG=1` says when it sees a
race start and stop. Not tested on a wheel yet: how the speed scaling feels,
and whether this base accepts an update of a running spring cleanly.

Not done: the rest of force feedback (cornering force, impacts, road texture;
see [wheel-force-feedback-proposal.md](wheel-force-feedback-proposal.md)), the
wheel's paddles beyond the buttons above, a clutch (the title has none), a
response curve, and a setting in the overlay. This needs the toolkit's
`XBOX_INPUT_SRC_WHEEL` and `src/input/wheel_pad.h`.

## Touch (Android)

Without a controller the Android build shows on-screen controls (the
toolkit's `src/input/touch_pad.c`). They follow the game. In a race: a stick
on the left half that starts where the thumb lands, and GAS, BRAKE, BOOST,
BREAKER (Crashbreaker), LOOK (look back) and VIEW on the right. In menus and
the pause menu: a d-pad, A B X Y, LT and RT. Pause and the settings menu sit
at the top, left of the score board. BOOST keeps the accelerator down, so
one thumb can boost, and a finger can slide between GAS and BOOST without
lifting. The touch controls always use the Xbox layout, like the keys.

The controls are placed on the screen, not on the picture: aspect, fit,
ultrawide and render scale do not move them. They keep clear of the HUD's
corners (POS, LAP, the speedometer, the boost bar) and of the score board.
On a tablet they stay the size they have on a phone.

Settings, on the Input page of the settings menu: Touch controls (Auto hides
them while a controller is used and shows them at the next touch; Always;
Off), their opacity and size, and Accelerate automatically. The settings
menu itself works by touch: drag a list to scroll it, tap a row to change
it, drag or tap a slider.

## Back and the settings

Because Back + Start opens the settings, the game sees Back a moment late: a
tap arrives when you let go, a hold after a quarter of a second (View and Menu
on a Steam Deck). Start alone is not delayed.

## The title has no layout choice of its own

This was checked first, because switching a layout would have been better than rewriting
the pad:

- Its Controllers screen (pause menu > Controllers) has one line per
  controller: Vibration on / off. The string table (Data/Globalus.bin) has
  CHANGE CONTROLLER SETTINGS, CONTROLLERS, CONTROLLER 1 to 4 and Vibration
  on / off, and no name for a layout.
- It reads its actions through one table, 31 entries of channel numbers at
  0x384168 (.rdata), whose only reader is sub_00019A10: accelerate is the
  right trigger's channel, brake the left's, and so on. There is no second
  table and no index that selects one.

So the PS2 layout is a rewrite, by the host, of the pad state the title has
just read.

## How "only while driving" works

The title reads its pads in one place: sub_001B57E0, once a frame, which
holds the title's only `XInputGetState` call. The host runs right after that
call, on the title's own thread (the `ctl_pad` mark in `config/patches.json`;
`src/game/controls.c`), before the title turns the bytes into the channels
that every menu and every car reads. Whether the title is driving is read
from its memory at that same moment, so the layout follows the title's state
on the same frame.

Driving is all of:

| What | Where | Evidence |
|---|---|---|
| The game module is the current one, and is not being left | `[0x4D5370]` is the object with vtable 0x3A9ACC, or 0x3A939C in the two-player stage (the front end's is 0x3A9E7C); `[0x4D53B4]` is not 6 | the trace: the front-end vtable through boot, menus, garage, the training video and the loading screen; 6 from "Quit > Yes" until the module changes |
| Nobody has paused | `[0x4D537C]` is -1 (else the pausing player) | the stage update tests it before it looks at Start (sub_00026050 at 0x26609); 0 on the frame after Start, -1 on the frame Resume takes effect |
| The title takes a pause now | byte `[0x752D33]` is 0 | tested in the same place; set while the event is left after the results |
| A crash event's fly-by and captions are not up | byte `[0x4D0767]` is 0 | 1 through the fly-by, and from the captions until A leaves them |
| The results are not up | byte `[0x4D0738]` is 0 | 1 from the results screen to the end of the event |
| The event is in play, or counting down | `[0x73B51C]` is 2, or below 2 with the countdown's length at `[0x73B520]` set (not -1.0, not 0) | 2 from the GO; 3 when the event ends for the player; the length (4.5 s in a race, 3 s in a crash event) is written when the countdown begins |

In the two-player stage the last three rows are each player's own, and a pad
goes by its player's: the title's port 2 is player 1's, port 3 player 2's.
The race records (results, intro) are `0xC50` apart (two of them, built in a
loop of two in the game object's constructor, at `0x4CFB20`; the title's own
`sub_00167DC0` and `sub_00167DF0` read the results bytes `0x4D0738` and
`0x4D1388` and the intro bytes `0x4D0767` and `0x4D13B7`), the event records
(phase, countdown) `0x27E0` apart (`sub_00025050` steps both in one loop over
the players). A player whose event is over is back on the menu layout while
the other still drives.

The countdown counts as driving because the boost start is on the
accelerator. A crash counts as driving: the impact time, the aftertouch and
the crashbreaker are driving controls.

### Handing over to a menu

A button that is down when driving ends would otherwise turn into a press
nobody made: the bottom button, held to accelerate, is A in the pause menu,
and A resumes. So a button that is down at the hand-over, and that the title
did not see down under the PS2 layout, reads released until it is let go;
the next press of it is the menu's at once. The title's menus act on presses,
not on held buttons (with the Xbox layout, A held into a pause does not
resume either), so nothing else is needed. Going back to driving nothing is
held back: the button that confirmed "Resume" may stay down and accelerate.

## What it does not touch

- **Scripted input.** A pad driven by `RECOMP_PAD_SCRIPT` / `RECOMP_PAD_LIVE`
  keeps the Xbox layout whatever the setting is: `rt` in a test, in the scene
  drivers or in the shader pre-build's race accelerates on every machine.
  (`BO3_CTL_SCRIPTED=1` makes a script stand in for a controller, for
  `tools/headless/controls_scene.sh`.) Once a controller takes its pad back
  from a script, the setting applies to it.
- **The keyboard**: while a key is held the pad state is left as it is, so
  the keys keep their documented Xbox buttons (see below).
- **Rumble**: the title's motor speeds do not pass through this.
- **Replays**: a replay is started from the results screen, where the layout
  is already off, and it stays off (the results byte). Not checked: whether a replay reads
  the pad for anything.
- **The overlay**: while it is open the title reads a neutral pad anyway.
- **Button prompts**: they show the game's own button, not the PS2 layout's.
  In Crash mode the Crashbreaker prompt shows B (or Circle), while the PS2
  layout fires it with R2 / the right trigger. BO-147 tracks prompts that
  follow the layout while driving.

## With the Nintendo face layout

`input.face_layout = nintendo` is applied first, as the host pad is read: the
button printed A is the title's A, in menus and in the Xbox driving layout.
The PS2 layout is applied after it and goes by position, not by the letter:
the bottom button accelerates, the left one brakes and the top one changes
the view on any pad, as the thumb finds them on a DualShock; the right-hand
button does nothing while driving. In menus the face layout alone holds: the
right-hand button, printed A, confirms.

The button prompts follow it (`input.glyphs`, `assets/glyphs`): a prompt shows
the button that triggers the action, as the chosen pad prints it. With the
Nintendo layout the title's A is the right-hand button, so the Xbox and Steam
sets show B where the game says A, the PlayStation sets the circle, Positional
the right-hand dot, and the Nintendo set shows A. The table is in the
toolkit's `src/input/pad_glyphs.h`. The settings menu follows the same layout
(its A selects, its B goes back) and names the buttons the same way.

A pad script standing in for a controller (`BO3_CTL_SCRIPTED=1`) gets the
face layout too, applied by `controls.c`, because a script's buttons are
written into the report after the host pad is read and no face layout reaches
them otherwise. Its `a` is then the bottom button and its `b` the right-hand
one, as SDL names them on any pad. (Before this the scene failed with both
settings on, 20 of 25, on the Retroid Pocket 5 and on the Mac alike: the
script's `a` was taken for the right-hand button. A controller was never
affected.) A script that is not a stand-in gets neither setting.

## Tests

- `tests/controls` (`bo3_controls_test`, `ctest -R bo3_controls`): every row
  of the table, the right stick, the Nintendo layout under it, the hand-over,
  and the whole chain from a host pad's buttons (SDL and XInput) through the
  toolkit's face layout and map to what the title reads, for both face
  layouts, driving and in a menu.
- The toolkit's `input_map` (the setting's plumbing, controller against
  keyboard) and `overlay_settings` (the setting itself).
- `tools/headless/controls_all.sh <rundir-prefix>` runs the default set of
  the scene below: ps2 race, xbox race, ps2 crash, and ps2 race with
  `FACE=nintendo`.
- `tools/headless/controls_split_scene.sh <rundir> race|roadrage|teamcrush|doubleimpact|partycrash`:
  the same for the two-player modes (both pads), checked by `controls_split_check.py`.
- `tools/headless/controls_scene.sh <rundir>` with `SCHEME=ps2|xbox`,
  `EVENT=race|crash` and `FACE=xbox|nintendo`: a scripted run through boot, the menus, the garage,
  loading, the countdown, driving, a pause with the accelerator still held,
  Resume, a second pause, Quit to the front end; or a crash event's captions,
  crash, results and Continue. It asserts on `BO3_CTL_LOG` (the title's state,
  the gate, each pad change as read and as given) and `BO3_DRIVE_LOG` (the
  speed).

## Not verified

- A controller in the hand: every run above is a pad script standing in for
  one. The built-in pads of the Steam Deck and the Retroid Pocket, Windows
  (native XInput and SDL) and Linux were not run.
- Two players: run headless on Linux (Vulkan) with two scripted pads standing in for
  controllers, `tools/headless/controls_split_scene.sh <rundir> <mode>`: Race, Road Rage,
  Team Crush and Double Impact give both pads the PS2 layout from the countdown, and in
  Team Crush and Double Impact each pad goes back to the menu layout when its own
  player's event ends, while the other still drives. Party Crash plays on the first
  player's records and the first pad throughout (it is the one-player stage), so it
  goes by player 1 like a one-player event. Not run: keyboard plus controller
  (Input > First controller), a pause by player 2, real controllers, Windows, Android.
  Also reported working in split screen on macOS with two controllers.
- A race to its finish line, Road Rage, Burning Lap and online: the end of an
  event was traced in a crash event and by quitting a race.
- The crashbreaker itself was not triggered in a run (it needs the wrecks);
  the right trigger was seen to reach the title as B.
