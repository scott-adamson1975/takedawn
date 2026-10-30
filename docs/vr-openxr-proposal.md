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
