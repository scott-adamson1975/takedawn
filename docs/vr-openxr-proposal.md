# VR mode via OpenXR: feasibility and proposal

Status: **investigation, started 2026-10-30; nothing implemented in the game yet.**
Written 2026-10-29; the wheel work is on the `thrustmasterwheel` branches (see
[controls.md](controls.md), "Racing wheel and pedals"). Findings so far are under
"Spike results" below.

## Goal

Play Takedawn in a headset, seated, with a racing wheel and pedals. Head
tracking and stereo depth in the race; menus and videos on a virtual screen.

## The setup this was scoped for

- Headset: Meta Quest 3, used through SteamVR (Wi-Fi or USB 3).
- PC: Windows 11, RTX 4090.
- SteamVR is installed and is already the active OpenXR runtime
  (`C:\Program Files (x86)\Steam\steamapps\common\SteamVR\steamxr_win64.json`).
- Wheel: Thrustmaster T818 base, Raceline pedals (two USB devices).

## Verdict

Possible, and the project has useful groundwork, but it is a large feature:
closer to weeks of work than days. It also needs the owner to test every step
in the headset, because comfort and feel cannot be judged from the code.

This is an estimate from reading about six files and a few docs, not the whole
8,000-line Vulkan renderer. See "Not verified".

## What already helps

- **Vulkan 1.3 with dynamic rendering** (`vkCmdBeginRendering`,
  `src/d3d/nv2a_pb_vulkan.c`), so multiview is available without a rewrite.
- **Everything is in metres** (LOD switches at 15 m, 30 m and so on,
  [draw-distance.md](draw-distance.md)), so head movement and eye separation
  would come out at the right scale.
- **The game's camera is already patched.** Ultrawide
  ([ultrawide.md](ultrawide.md)) overrides the camera set-up (`sub_0002F380`,
  `sub_0002F480`: aspect and field-of-view factor), and
  [draw-distance.md](draw-distance.md) maps the camera objects (view objects at
  `0x7397C8 + player * 0x1CC`) and the render layer flags at `0x4D61C4`
  (HUD, bloom, motion blur are on/off switches there).
- **High refresh rate** ([high-refresh.md](high-refresh.md)) already runs the
  game's update and draw several times per 60 Hz simulation step, with the
  camera blended between steps (`src/game/hirefresh.c`, wraps in
  `recomp_manual.c`). That suggests drawing one frame twice, once per eye, is
  something the game tolerates. It also has a refresh grid
  (`xbox_DisplayGrid`) that a headset's frame timing could feed.
- The presenter (`src/video/fb_present_vulkan.c`) already owns the final image,
  so a mirror window and a virtual-screen layer are natural.

## Two ways to get two eyes

| | A. Render the stage twice with an eye camera | B. Per-eye transform in the vertex shader |
|---|---|---|
| Idea | Wrap the stage render and call it twice per frame, each with an eye-specific camera | Add a final clip-space transform per eye to every translated vertex shader (`d3d8_vsh_glsl.c`), using multiview |
| Culling | Correct per eye, because the game culls with the camera it is given | Wrong: the game culls with its flat camera, so turning the head shows missing scenery |
| Cost | Double the game-side render work | One pass for both eyes |
| Risk | The render must be safe to call twice (high-refresh suggests yes) | Menus, HUD and post effects get bent too |

Recommendation: **A**.

## The hard parts

1. **Headset interop.** OpenXR's Vulkan support lets the runtime choose the GPU
   and require extra instance and device extensions
   (`XR_KHR_vulkan_enable2`). The toolkit creates its own Vulkan instance and
   device (`src/d3d/nv2a_vk_core.c`), so that creation has to be reworked. It
   also adds the OpenXR loader as a dependency (and a licence notice). Riskiest
   plumbing.
2. **Resolution.** The game renders 640x480 targets, scaled up to 4x at most. A
   Quest 3 wants about 2064x2208 per eye. The render target size has to be
   decoupled from the game's 640x480 assumption. Expect a soft picture even so.
3. **HUD and menus.** 2D drawn into the same frame as the world. They need their
   own pass, shown as a floating panel; menus, garage, videos and loading
   screens go on a virtual screen.
4. **Screen-space effects** (bloom, motion blur) are wrong per eye. First
   version: switch them off with their render flags.
5. **Frame timing.** The headset wants frames at its own rate (72 to 120 Hz).
   `xrWaitFrame` would have to drive the pacing; delicate. Streaming over
   Wi-Fi or USB adds latency on top. Test over the cable first, at 90 Hz.
6. **Scenery visibility.** Track visibility lists are precomputed per unit for a
   forward-looking camera. Looking sideways or back may show missing or
   low-detail scenery. Unknown how bad.
7. **Comfort.** The biggest unknown. A chase camera at speed with shake, and the
   crash cameras, are nauseating. Realistically: a camera fixed to the car for
   driving, with chase and crash cameras shown on the flat virtual screen. It
   has not been checked which views Burnout 3 offers.

## Suggested phases (each worth stopping at)

1. **Theatre mode.** Create the OpenXR session on the existing Vulkan device and
   show the flat game image on a floating screen, wheel for driving. Proves the
   interop and the SteamVR path; useful on its own.
2. **Head-tracked in-car view.** Stereo rendering in races only, no HUD, bloom
   and motion blur off. Answers the culling, resolution and comfort questions.
3. **Everything else.** HUD panel, menus, settings, comfort options, a proper
   pacing pass.

## Open questions for next time

- Does SteamVR's OpenXR runtime accept the toolkit's Vulkan instance and
  device set-up, and which extension versions does it offer? (First thing
  phase 1 checks.)
- Is the stage render safe to call twice per frame in every mode?
- Which camera views exist in a race, and is one rigidly attached to the car?
- How much scenery is missing when looking away from the travel direction?
- USB 3 or Wi-Fi: which feels better with the wheel (latency)?

## Not verified

- No renderer code beyond the Vulkan set-up, the vertex shader output line and
  the render-pass calls was read in depth.
- No camera object layout was inspected; addresses above come from the existing
  docs.
- Nothing was tried in a headset, and no OpenXR code was written.
- Whether the stage render can be re-entered safely is inferred from how the
  high-refresh mode draws, not tested.

## Where to resume

1. Finish feeling and tuning the wheel (the response curve, steering deadzone
   and an overlay page were offered but not built).
2. Start phase 1. Work in the toolkit clone (`C:\source\xboxrecomp-takedawn`,
   branch `takedawn`); see [building.md](building.md) and [windows.md](windows.md).

## Spike results (2026-10-30)

`tools/win/xr-probe.c` (built to `bin/xr-probe.exe`; build line in its header) loads the OpenXR
loader and the Vulkan loader at run time, as the game does for Vulkan, and asks the runtime.

**Stage 1, run (no headset, SteamVR not running, nothing started): SteamVR's OpenXR runtime
offers 42 instance extensions**, including `XR_KHR_vulkan_enable2` (so the runtime-assisted
Vulkan instance and device creation the plan relies on exists), `XR_KHR_composition_layer_depth`,
`XR_KHR_visibility_mask`, `XR_KHR_win32_convert_performance_counter_time`, `XR_FB_display_refresh_rate`,
`XR_EXT_hand_tracking` and `XR_EXT_debug_utils`. It does **not** offer `XR_KHR_composition_layer_cylinder`
or `_equirect2`: a curved screen is not available, so theatre mode is a flat quad layer.

**Stages 2 and 3, run with the Quest 3 over Link and SteamVR up: all pass.**

- Runtime "SteamVR/OpenXR" 2.18.4; the system is "SteamVR/OpenXR : oculus", orientation and position
  tracking, 16 layers, a swapchain up to 8192x8192.
- **Recommended eye size 2528x2704** (two views, one sample). That is about 3.4 times the pixels of the
  game's biggest render scale target per eye (640x480 at 4x is 2560x1920), so the picture will be soft
  unless the game renders larger than the 4x cap; see "The hard parts", 2.
- The runtime states Vulkan 1.0 to 1.2 as its supported range, yet **a Vulkan 1.3 instance made through
  `xrCreateVulkanInstanceKHR` succeeded**, and the GPU it names is the RTX 4090 (Vulkan 1.4.357), the card
  the game already uses. `xrCreateVulkanDeviceKHR` made a device with a graphics queue (family 0).
  So the first open question is answered: **the runtime-assisted creation works with the Vulkan version the
  toolkit asks for.** What is untested is the toolkit's own long extension and feature lists on top of the
  runtime's additions (present-wait, HDR metadata, dynamic rendering features): the two creation calls
  would need to add those to the create-info before handing it over.

**Environment found:** SteamVR is the active runtime (registry `ActiveRuntime`) and ships an OpenXR loader
(`SteamVR/bin/win64/openxr_loader.dll`, 1.1.62). The OpenXR headers are the Khronos SDK's
(`KhronosGroup/OpenXR-SDK` release-1.1.47, Apache-2.0 or MIT) and are fetched into
`~/.cache/bo3-windeps/openxr/` for now; if this goes in, `fetch-deps.sh` should fetch them the way it
does the Vulkan headers, and the loader stays a run-time `LoadLibrary` (the system's, else a shipped copy).

**Where it would plug in (read, not changed):**

- The toolkit makes its Vulkan instance in one place and its device in another
  (`src/d3d/nv2a_vk_core.c`: `vkCreateInstance` around line 155, `vkCreateDevice` around line 381). With a
  headset these two calls go through `xrCreateVulkanInstanceKHR` / `xrCreateVulkanDeviceKHR`, which take
  the same create-info and add what the runtime needs, and the physical device is the one
  `xrGetVulkanGraphicsDevice2KHR` returns (the code today picks its own).
- The presenter (`src/video/fb_present_vulkan.c`, the present function around line 3200) ends each frame
  with one `vkCmdBlitImage` of the game's image into the window's swapchain image (around line 3413). Theatre
  mode is a second blit from the same source image into an OpenXR swapchain image, then `xrEndFrame` with a
  quad layer: a small, contained change. The window can stay as the mirror.
- Which camera views exist in a race is still unknown: the docs only say the change-view button (Y) exists.
  The per-player view object is at `0x7397C8 + player * 0x1CC`; recording it while pressing Y would show the
  mode field and tell which views there are and whether one is attached to the car.

## Next steps (in order)

1. ~~Run `xr-probe --system`~~ Done 2026-10-30, passes (above).
2. Record the view object while cycling the camera (extend the telemetry recorder) to learn the views.
3. Phase 1, theatre mode, behind a setting, off by default: OpenXR instance and session created with the
   toolkit's Vulkan device, a quad layer fed by a second blit, `xrWaitFrame` pacing kept separate from the
   game's, the wheel for driving.

## Camera views (recorded 2026-10-30, 137 s of a normal race, 8 presses of change-view)

`BO3_TELEMETRY` now also records the camera (`tools/telemetry/find_views.py`). Position is relative to the
player's car origin, in the car's frame (x right, y up, z ahead).

| View | Where | Notes |
|---|---|---|
| **Low front ("bumper") camera** | right 0.00 (+/-0.09), up **+0.66**, ahead **+1.15 at a standstill, sliding back to about 0.0 above 60 m/s** | Rigid to the car: the view matrix's forward axis matches the car's forward at 0.99. 5133 of 8241 frames. Up is constant (0.64 to 0.66). |
| **Chase camera** | up +1.4 to +1.5, ahead about -5.0 to -6.1, sways sideways in corners (right std 1.4) | 2781 frames. |
| Others (crash cameras, other angles) | 3 to 10 m away, anywhere | 327 frames, 4 percent. |

The change-view button toggled between the first two; no third driving view was seen in these 8 presses.
The view object (`0x7397C8`, 0x1CC bytes) has no slot that clearly holds the mode: the best candidates changed
near only 2 of 8 presses. The mode is not needed for VR: the camera is placed from the car's matrix.

**What it means for VR**

- **There is a usable in-car view.** The low front camera is on the car's centre line, rigid to the car, and
  its height of 0.66 m above the car's origin is about eye height: the wheel centres sit about 0.06 m below
  the origin and the wheel radius fitted from the spin is about 0.35 m, so the ground is about 0.4 m below the
  origin and the camera is about 1.07 m above the road. The scale is real metres, so a 1:1 headset view works.
- **Phase 2 would use its own camera**, not the game's: each frame, the car's matrix plus a seat offset
  (centre line, up +0.66, ahead a fixed 0.0 to 0.2) plus the head pose from OpenXR. The slide of the front camera
  (1.2 m back with speed) must not be copied: a seat that moves under the player is uncomfortable.
- **Comfort filters** apply to the car's rotation: its roll and pitch spikes in a landing or a hit should be
  low-passed before they drive the view.
- **Crash cameras, replays, results, menus** (the 4 percent, and every non-race screen): a virtual screen
  (theatre mode), not a 1:1 view. The test for "in a race with the car view" is the car's phase being 2.
- Not yet known: whether the car's body is drawn from this camera's position (a bonnet and nose in view, or
  a hidden model), and how it looks with the camera slid to the fixed seat point.

## Updated next steps

1. Phase 1 (theatre mode) is still the first deliverable; it is independent of the camera findings.
2. Before phase 2: draw the stage twice from a test camera (no headset) to check the render is safe to
   re-enter and the car looks right from the seat point (answers "is the stage render safe to call twice").
3. Resolution: the recommended 2528x2704 per eye is far above the game's 4x cap (2560x1920 total, 640x480 base).

## The seat camera test (2026-10-30): the camera can be replaced

`BO3_SEATCAM=<right>,<up>,<ahead>` (`src/game/seatcam.c`, launcher `bin/run-seatcam.cmd`, off by default) draws the
race from a seat in the player's car. **It works**: the stage's render begins by building the camera from the
game's camera object, and the `uw_stage_render` mark is that function's entry, so writing the seat there (and
restoring the title's values at the next flip) changes what the world is drawn from, with the HUD untouched.

**The camera object** (the race's view is at `0x4D6200`, the object is at its `+ 0x68`; `0x4CFD70` in the test):

| Offset | Holds |
|---|---|
| `+0x10` | the field of view in degrees (90.0) first, then zeros |
| `+0x20` | the orientation as a **quaternion** (x, y, z, w): the standard quaternion of the rotation whose columns are the camera's x, y, z axes in the world, which for a car are its left, up and forward (the car's matrix rows are left, up, forward; left x up = forward, a proper rotation) |
| `+0x30` | the position (x, y, z), then a fourth float (about 6.8 in the chase view) |

Writing matrix rows there (the first attempt) blanks the world: the picture was the HUD over a dark fill.

**Seen in one 130 s race through the log:** the override applied for over 3000 renders; it correctly stayed
off while the car was not driving (+0x3D0 not 4: the crashes and the respawn) and for a camera more than 8 m from
the car (a crash camera). The title's own camera was 5.3 m from the car (chase) or 1.3 m (front) when applied, and
the same override gave the same seat in both views.

Looking around (the right stick, or J, L, I and K in the test) turns the seat's orientation about the seat point:
a stand-in for the headset's head tracking.

**What the picture looked like (the owner's report, one 130 s race, seat 1.0 m up and 0.3 m behind the origin):**

- **Scenery** looked right looking around (sideways and up): no missing scenery, no pop-in noticed. This answers
  the visibility-list worry (open question 6, "The hard parts") for sideways and up; **backwards was not tried**.
- **The HUD** was perfect (it is drawn separately from the world).
- **Crashes** used the game's traditional camera (the override stays off for them, as designed).
- **The car's body** was visible for a moment and then never again; the owner would like some visual reference of
  the car in a VR view. The cause is not known: the title's car draw (`sub_0018DE00`) returns at once only if the
  byte at `car + 0x18F8` is 0 or the "vehicles" render flag (`0x4D61CE`) is 0, and the byte read 1 in every frame of
  both the front and chase views in the camera recording, so the draw is called in both. The seat camera now logs
  those two bytes (and the wreck state) whenever one changes (`seat1-seatcam.log`), so the next run shows when the
  body vanishes and what flips. Possibilities not yet separated: the body is not drawn from inside it (backface
  culling), it is hidden by the title after a crash and respawn, or it is skipped by the model draw (`sub_000303D0`)
  for a camera that close.

**Where this leaves the plan:** the in-car view is feasible without visibility work for the directions tried; a
visible car (bonnet, nose) is the open point and is a design choice as much as a bug: real cockpits have a body.

## The seat per car, the roof, and the car turning off (2026-10-30, after the owner's screenshots)

**The screenshots** (seat 1.0 m up and 0.3 m behind the origin): the HUD and the world are right, the bonnet and the
windscreen frame are drawn, and the interior is dark with a faint steering-wheel ring just left of centre (so the
car models have a steering wheel, and this car's is on the left). **The roof is cut away**, and the change-view
button turns the car's body off and on.

- **The roof**: the title's near clip plane is 0.5 m (draw-distance.md); with the seat inside the cabin the roof and
  the pillars are nearer than that and are clipped. `BO3_SEATCAM_NEAR=<m>` (default 0.1) lowers it while seated: the
  RwCamera (`view + 0x58`) is searched once for the pair 0.5, 10000.0 (the near and far planes, side by side) and the
  offset logged. Untested: whether the host's depth handling copes with 0.1 (distant depth flicker would show).
- **The seat differs per car**, in all three axes: a small coupe against a big saloon, and the wheel on the left in
  a US car and the right in a UK one. It is kept per car model, found by the size field of the car's model (`car +
  0x50` is the .bgv, its `+ 8` is unique to a model), in a text file (`BO3_SEATCAM_FILE`, one line a car: key,
  right, up, ahead) and set in the game with the numpad (4/6, 8/2, 9/3, 5 resets, 0 saves). A car with no line has the
  default seat. The model also holds the wheel hubs (`+ 0xBB0 + 0x40 * w`, used by soft-body damage), so a default
  seat could later be derived from the wheelbase and track.
- **The car turning off**: not the draw byte (`car + 0x18F8`) nor the vehicles flag at the render's start (both 1 in
  both views); the log now also reads them at the flip, to see whether the title changes them during the render.

## Views by the car's state, and the steering wheel question (2026-10-30)

**Views.** The title's change-view toggles two views, and the car is drawn in one (chase) and not in the other (front).
The seat camera follows that (`seatcam.c`):

| The title's view | Car drawn? | The camera |
|---|---|---|
| Chase (its camera 5 m behind) | yes | **the driver's seat**, behind the steering wheel, off the centre line by a per-car amount; the key V also cycles a **close chase** (1.7 up, 4.3 back, 7 degrees down) and a **far chase** (2.9 up, 9.5 back, 10 degrees down) |
| Front (its camera 1.3 m from the car) | no | **over the bonnet**, centred by default (1.1 up, 0.35 ahead), nothing of the car to sit in |

Which view the title is in is read from how far its own camera is from the car (under 2.5 m: the front view). Both
the seat and the over-bonnet point are kept per car (file `seatcam.txt`: `0x<model key> <slot> <right> <up> <ahead>`,
slot 0 the seat, 1 over the bonnet) and set in the game with the numpad (4/6 left and right, 8/2 ahead and back, 9/3
up and down, 5 reset, 0 save). The chase views are fixed numbers and rigid to the car.

**An identifiable steering wheel in the model?** Looked for, not found. A car's `.bgv` has up to 16 entries (body,
doors, bumpers, bonnet, boot, wheels) and no separate steering wheel entry; any wheel is triangles inside the
body entry. The model files can be read offline (`pveh/COMP/Car*.bgv`; a level's vertex-buffer offsets are relative
to the level header, level 3 the most detailed at about 5000 to 7000 vertices; wheel hubs are in the header). A search
of the vertices for a ring 12 to 24 cm in radius facing the driver gave, for five cars, a different best ring each
time, mostly on the centre line, none at a plausible driver's position, so **automatic detection is not reliable**
and the hand-set per-car points stay. A steering wheel visible in the owner's screenshot sits slightly left of the
camera's centre line in that car. (The offline reader and ring search are scratch scripts, not in the repo.)

## A driver or helmet as the anchor? (2026-10-30, not found)

The owner reported that the in-car view works and asked whether the car model has a driver whose helmet could anchor
the seat. Checked offline in `pveh/COMP/Car*.bgv` (level offsets are relative to the level header; level 3 is the
most detailed):

- No file in the game's data is named for a driver or helmet.
- A car model has **10 part entries** (the first int of each: `0x90A` the body, `0x203` three times, `0xFF02` three
  times, `0xFF01`, `0xFF03` twice): none stands out as a driver. Every entry's own frame is the identity, so there
  is no pivot to read a head position from.
- The model is not welded: it is about a thousand small pieces, so connected-piece analysis does not isolate a figure.
- Vertex density in the cabin: in Car1 the densest round ball (about 135 vertices within 14 cm, std about 5 cm
  each way) is at 0.66 m above the origin, the height of the title's own front camera, which would fit a head; but it
  is not repeated in other cars (their densest clusters are at about 0.2 m, flat, and the same in several cars: shared
  parts such as a seat or driveline). One car is not a rule.
- The owner's screenshots from the seat show a very dark interior with a faint steering-wheel ring and no visible driver.

So no reliable driver anchor exists in the data examined. The hand-set per-car seat stays; the title's own front camera
height (0.66 m above the origin in the recording) is the best single reference for head height.
