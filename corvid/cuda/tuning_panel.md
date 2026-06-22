# Live tuning panel (Dear ImGui) for the CUDA voxel viewer

A plan to integrate Dear ImGui into `tests/cuda/windows/notest_voxel_viewer.cu`
so that Escape brings up a config panel that edits the avatar and shading
"constants" at runtime, with no recompile. This is the tuning side quest for the
voxel digger (see `avatar.md`, `voxel_world.md`). NOTHING here is built yet; this
is the design recorded so a fresh session can execute it.

## Why

We have roughly forty hand-tuned constants spread across the avatar rig (camera
and saucer feel) and the shading functions (sky, terrain, ball, saucer head).
Tuning them by editing a literal, rebuilding, and relaunching is slow. A live
panel collapses that loop to dragging a slider. The panel must also make it easy
to record good values back into the code as new defaults.

## Decisions already made

- Dependency comes in through FetchContent (the repo's usual path, and the one
  that forwards sanitizer instrumentation when that ever matters). Note: this
  whole cell is Windows-only and MSAN is Linux-only, so MSAN never actually
  touches ImGui; FetchContent is still the consistent choice.
- The panel shows each field's current value, knows its original (default)
  value, colors a modified field differently so changed values are easy to read
  off and record, and offers per-field and global reset.
- Phasing is 0 then 1 then 2 (see Phasing). Each phase is independently runnable
  by the user, and phase 0 (the build and present integration) carries the real
  risk, so it lands and is verified first. Doing all three at once is acceptable
  if it proves easier, but the verification gates still apply.

## The seams in the existing code

- Present path: `cuda_d3d11_presenter::render(draw)` maps the CUDA target, runs
  the kernel, then `present()` does `swapchain_.fill_back_buffer(render_texture_)`
  followed by `swapchain_.present(1)` (`cuda_d3d11_presenter.cuh`, the `present`
  and `render` members). ImGui must draw in the gap between `fill_back_buffer`
  and `present`, onto a render target view of `swapchain_.back_buffer()`.
- The D3D11 device and immediate context that ImGui's DX11 backend needs live in
  the presenter's private `device_` (a `d3d11_device`, which exposes `.device()`
  and `.context()`). The swapchain caches the same two. The presenter needs small
  accessors to hand them out.
- The backbuffer (`d3d11_swapchain::back_buffer()`) rotates after each present
  (flip-discard) and is recreated on resize, so any RTV built from it is
  short-lived: build it fresh each frame, or cache and invalidate on
  resize/present.
- Escape already maps to `frame_action::menu` in `pump_events`
  (`corvid/sdl/frame_loop.h`); the viewer currently ignores `menu`. Reuse it to
  toggle the panel.
- Build: one executable per source (`tests/CMakeLists.txt`). SDL3 comes from a
  prebuilt drop via `corvid_require_sdl3()`; D3D11 and dxgi are linked when a
  source mentions `d3d11`. ImGui is source-only, so it does not fit the prebuilt
  drop; build it as a static library and link it into viewers whose source
  mentions `imgui` (same gating trick as `d3d11`).

## Architecture (five pieces)

### 1. Dependency and CMake

FetchContent ImGui at a pinned tag (ocornut/imgui, a recent v1.91.x or the
docking branch tag). ImGui ships no CMakeLists, so after
`FetchContent_MakeAvailable(imgui)` (which just populates the source when there
is no CMakeLists to add), define our own static library:

```
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp            # drop once the panel is done
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3 d3d11 dxgi)
target_compile_options(imgui PRIVATE -w)   # ImGui is not clean under our
                                           # -Wall -Wextra -Werror; silence it
```

Gating, mirroring the SDL3 and d3d11 logic already in the file:

- Only declare/build the `imgui` lib when a `cuda/windows/*.cu` source in the
  build mentions `imgui` (a `file(STRINGS ... REGEX "imgui")` scan, like the
  d3d11 one). It depends on SDL3, so `corvid_require_sdl3()` must have run first.
- In the CUDA executable loop, when the source mentions `imgui`, add
  `target_link_libraries(${EXECUTABLE_NAME} PRIVATE imgui)`.

The `imgui` static library compiles as plain CXX with clang++ at the MSVC ABI,
the same ABI the `.cu` executable targets, so they link together. The `cl`
second compiler never builds the viewers (they are `.cu`, clang-only), so it is
not a concern here.

### 2. RAII wrapper: `imgui_overlay`

New header `corvid/cuda/windows/imgui_overlay.h` (the project wraps C-style
global lifecycles in RAII). Single owner of the ImGui context plus both backends.
Include ordering matters: include it after the presenter so `NOMINMAX` and
`d3d11.h` are already in (the DX11 backend pulls D3D11 types).

API:

- Constructor takes the `SDL_Window*`, the `ID3D11Device*`, and the
  `ID3D11DeviceContext*`. It calls `IMGUI_CHECKVERSION`, `CreateContext`,
  `ImGui_ImplSDL3_InitForD3D(window)`, and `ImGui_ImplDX11_Init(device, context)`.
- Destructor: `ImGui_ImplDX11_Shutdown`, `ImGui_ImplSDL3_Shutdown`,
  `DestroyContext`. Non-copyable, non-movable.
- `process_event(const sdl_event&)`: forwards the raw `SDL_Event` to
  `ImGui_ImplSDL3_ProcessEvent`. (`sdl_event` must expose the raw event; it has
  `raw()` per the avatar/sdl notes.)
- `begin_frame()`: `ImGui_ImplDX11_NewFrame`, `ImGui_ImplSDL3_NewFrame`,
  `ImGui::NewFrame`.
- `render(ID3D11Texture2D* back_buffer)`: `ImGui::Render`, create an RTV from
  `back_buffer`, `OMSetRenderTargets(1, &rtv, nullptr)`, then
  `ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData())`. Build the RTV fresh each
  call (the backbuffer rotates); release it after.
- `wants_mouse()` / `wants_keyboard()`: return `ImGui::GetIO().WantCaptureMouse`
  / `WantCaptureKeyboard`.

### 3. Presenter hook

Add to `cuda_d3d11_presenter`:

- `device()` and `context()` accessors returning the `ID3D11Device*` and
  `ID3D11DeviceContext*` (from `device_`), for ImGui init.
- `back_buffer()` accessor delegating to `swapchain_.back_buffer()`, for the RTV.
- An optional overlay callback on the frame path. Cleanest is a second invocable
  parameter on `render`: `render(draw, overlay, sync_interval)`, where `overlay`
  is a `void()` run inside `present()` after `fill_back_buffer` and before
  `swapchain_.present`. The presenter stays ImGui-agnostic; the viewer's overlay
  lambda calls `imgui.render(presenter.back_buffer())`. Keep a no-overlay
  overload so other viewers are unaffected.

### 4. Events, Escape, cursor

In the viewer loop:

- Feed every SDL event to `imgui.process_event(ev)` first, always (ImGui tracks
  state even while the panel is closed).
- Escape returns `frame_action::menu`; on `menu`, toggle a `bool show_config`.
  When it turns on, disable relative mouse mode (`win.set_relative_mouse_mode`
  off) so the OS cursor is free for the sliders; when it turns off, leave
  relative mode off (the game re-grabs it on the next right-button press, as it
  already does).
- While `show_config` is true, skip the game handlers (`fly_input`, dig) for
  events ImGui captured: if the event is mouse-type and `imgui.wants_mouse()`, or
  key-type and `imgui.wants_keyboard()`, consume it. Otherwise the game still
  sees it. The simplest form is to gate the existing handler composition on
  `!show_config || !imgui.wants_...()`.
- Call `imgui.begin_frame()` once per frame before building the panel, build the
  panel only when `show_config`, and pass the overlay lambda to
  `presenter.render`. ImGui draws its own cursor while the panel is up.

### 5. Config: host tuning and device render_config

Two structs, because the constants split cleanly by where they are consumed.

Host tuning (`avatar_tuning`, lives with `avatar_rig` in the viewer for now,
promote later if reused). Move every `static constexpr` feel constant off
`avatar_rig` into a plain struct of runtime floats with default member
initializers equal to the current values, and have the rig hold one and read
through it:

```
struct avatar_tuning {
  float ball_radius = 0.6F;
  float head_radius = 0.45F;
  float head_height = 0.9F;
  float boom_min = -1.5F, boom_max = 14.0F, boom_rise = 0.35F;
  float zoom_approach = 8.0F;
  float spin_rate = 0.6F;
  float saucer_lean = 0.4F;
  float heading_approach = 8.0F;
  float thrust_full = 12.0F, thrust_approach = 5.0F;
  float move_speed = 8.0F;   // was the 8.0F literal in main's input.movement
  float fov_deg = 60.0F;     // rays() recomputes tan(fov/2) so it can be live
};
```

`avatar_rig` gains an `avatar_tuning tune;` member and reads `tune.saucer_lean`
etc. The defaults instance is a default-constructed `avatar_tuning{}`. Zero
kernel changes. (Leave the dig brush and start pose as constants unless tuning
them turns out to matter.)

Device render_config (`corvid/cuda/render_config.cuh`, plain aggregate usable
from host and device, default member initializers equal to the current
literals). Holds the shading parameters now baked into `voxel_render.cuh`:

- sky: zenith, horizon, gradient bias, sun direction, halo color and strength
  and exponent, core color and exponent.
- terrain: light direction, ambient, sun color.
- ball: dim, tint, ambient floor.
- head: canopy color, panel frequency and amplitude, base albedo, ambient, sun,
  belly ring and spoke frequencies, paint base and range, hub radius and
  softness and color and strength, rim center and width and color and strength
  and dot frequency, jet base and slope, thrust color and strength, dome and
  belly specular powers and specular strength.
- kernel: anti-alias sample count (make it a runtime `int` the kernel loops on,
  not a `constexpr`, so 1 to 3 is a slider).

Pass a `render_config` by value into `voxel_kernel` (like `ball` and `head`),
and thread `const render_config&` through `shade_primary_ray` ->
`shade_ball`/`shade_terrain_hit`/`sky_color` and
`shade_ball` -> `shade_scene_ray` -> `shade_head`/`shade_terrain_hit`/`sky_color`.
Each `shade_*` reads its parameters from the config instead of literals. The host
keeps one live `render_config`, the panel edits it, and the loop passes it to the
kernel each frame.

## The modified-vs-original widget

Keep two instances of each config: the live one the panel edits, and a const
defaults one (default-constructed). A small helper draws a slider that compares
against the default:

```
// In the panel code, for each field:
bool tuned_slider(const char* label, float& v, float def,
                  float lo, float hi);   // returns true if v != def
```

Behavior: draw `ImGui::SliderFloat`; if `v != def`, push a distinct label color
(for example yellow) so the row reads as modified, and draw a small per-field
"reset" button that sets `v = def`. A global "Reset all" button assigns the whole
live config from defaults (`cfg = cfg_defaults;`). Group the fields under
`ImGui::TreeNode` sections (Avatar, Sky, Terrain, Ball, Head) to keep it
navigable. Colors used elsewhere: vec3 fields use `ImGui::ColorEdit3`, compared
component-wise for the modified tint.

Optional nicety (not required): a "Copy changed as C++" button that builds a text
block of only the modified fields as initializer lines and puts it on the
clipboard, so recording new defaults is a paste. Highlighting plus reset is the
must-have; this is gravy.

## Phasing

- Phase 0: dependency, CMake static lib, the `imgui_overlay` wrapper, the
  presenter accessors and overlay hook, event routing, and Escape toggling the
  ImGui demo window (`ImGui::ShowDemoWindow`). This proves the whole pipeline:
  build integration, present interleave, input capture. Verify by running the
  viewer, pressing Escape, and interacting with the demo window while the camera
  freezes under it.
- Phase 1: replace the demo window with the real panel bound to `avatar_tuning`.
  Convert the rig's `static constexpr` feel constants to `tune` fields. This is
  the recompile-free camera and saucer tuning we have been iterating on. No
  kernel changes.
- Phase 2: add `render_config`, thread it through the kernel and the `shade_*`
  functions, and add the Sky/Terrain/Ball/Head sections to the panel. This is the
  larger, mechanical plumbing job.

## Risks and gotchas

- ImGui is not warning-clean under `-Wall -Wextra -Werror`; the `imgui` target
  must build with `-w` (or per-warning `-Wno-*`). Do not let the global flags
  reach it.
- Mixed translation units: the `.cu` viewer (clang CUDA) and the `imgui` static
  lib (clang CXX) both target the MSVC ABI on Windows, so they link. The viewer
  includes `imgui.h` and the backend headers in host code only; the device code
  never touches ImGui.
- Include ordering: the DX11 backend needs `d3d11.h` with `NOMINMAX`. Include the
  overlay header after the presenter (which already pulls D3D11 with NOMINMAX).
- The backbuffer rotates each present and is recreated on resize, so build the
  ImGui RTV from `back_buffer()` each frame and release it; do not cache a stale
  one.
- Making the AA sample count a runtime loop bound (rather than `constexpr`) costs
  a little, but the loop is tiny; fine for a tunable.
- `fov_deg` live means `rays()` recomputes `tan(fov/2)` per frame instead of once
  at construction; trivial.
- clangd lags on cross-`.cuh` edits all session; trust `ide_build`, not the
  squiggles (a known gotcha for this cell).

## Verification

Build with `./scripts/ide_build.ps1 tests/cuda/windows/notest_voxel_viewer.cu`
(compiles, does not launch the blocking GUI). The user runs the viewer to verify
behavior; everything visual here is runtime-pending until they do. Run
`./format_all.ps1` before committing.
