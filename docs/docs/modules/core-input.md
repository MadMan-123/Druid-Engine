# core-input

Files: `src/core/input.c` (289 lines), input-related sections of `include/druid.h`
(KeyCode / ControllerCode / JoystickCode / MouseButton enums, ~lines 2097–2143;
input declarations ~lines 2423–2463).

## Synopsis

SDL3-backed input layer for the Druid Engine. It owns the process-wide SDL event
pump (`processInput` drains the SDL event queue every call), the gamepad slot
table (`gamepads[GAMEPAD_MAX]`), and four derived global axes
(`xInputAxis`/`yInputAxis` for movement, `xLookAxis`/`yLookAxis` for camera look)
that fuse keyboard WASD, primary-gamepad sticks, and relative mouse motion. The
module has no per-module header; everything exported is declared directly in
`druid.h`, most with `DAPI` (i.e., callable from external/game/editor code). It
depends on SDL3 (keyboard/gamepad/mouse state, event queue), the engine's
`Application` struct (to set `app->state = EXIT` on `SDL_EVENT_QUIT`), and the
global `Renderer *renderer` (for `setMouseCaptured`, which needs
`renderer->display->sdlWindow`).

## SDL event flow

- `initInput()` assumes SDL is already initialized elsewhere (comment: "SDL_Init
  already called by initDisplay — do not re-init here"), then calls
  `SDL_PumpEvents()` once so already-connected controllers register before
  `SDL_GetGamepads()` is queried.
- `processInput(Application *app)` is the per-frame entry point: it calls
  `SDL_PumpEvents()` then loops `SDL_PollEvent(&evnt)` (static `SDL_Event evnt`),
  handling exactly three event types: `SDL_EVENT_QUIT` → `app->state = EXIT`;
  `SDL_EVENT_GAMEPAD_ADDED` → `checkForGamepadConnection`;
  `SDL_EVENT_GAMEPAD_REMOVED` → `checkForGamepadRemoved`. All other events are
  discarded (`default: ;`).
- Who calls `processInput` is **not visible in these files** — it takes an
  `Application *`, and `druid.h` declares an application layer (`run`,
  `startApplication`, `applicationRenderStep`), so the caller is presumably the
  main loop in the application module (external to this module's file set).
- Keyboard/gamepad-button/mouse queries do **not** go through the event loop;
  they poll SDL state directly (`SDL_GetKeyboardState`, `SDL_GetGamepadButton`,
  `SDL_GetMouseState`, `SDL_GetRelativeMouseState`) at call time.

## Public API Reference

All declared in `include/druid.h`. "No call sites in these files" means the
caller is likely external (game code, editor, or the application main loop).

### `void initInput()` (druid.h:2425 — no DAPI)
Caches the SDL keyboard-state pointer and opens all already-connected gamepads
into free slots of `gamepads[]` (up to `GAMEPAD_MAX`), incrementing
`gamepadCount` and logging via `DEBUG`. Side effects: `SDL_PumpEvents`,
`SDL_OpenGamepad` per pad, `SDL_free` on the id list. No params/return. No call
sites in these files — likely called by engine init (e.g., alongside
`initSystems`/`initDisplay`).

### `void destroyInput()` (druid.h:2426 — no DAPI)
Shutdown: calls `SDL_QuitSubSystem(SDL_INIT_EVENTS)`, then closes and NULLs every
open entry in `gamepads[]`. Note it does **not** reset `gamepadCount`. No call
sites in these files — likely engine shutdown.

### `DAPI void processInput(Application *app)`
Per-frame SDL event pump/drain (see "SDL event flow" above). Param: `app` —
mutated only on quit (`app->state = EXIT`, `ApplicationState` enum
`{ RUN, EXIT }`). Side effects: consumes the entire SDL event queue, may
open/close gamepads via the two hotplug handlers. No call sites in these files.

### `DAPI void checkForGamepadConnection(SDL_Event *event)`
Handler for `SDL_EVENT_GAMEPAD_ADDED`. Reads `event->gdevice.which`; ignores the
event if that instance ID is already open; `WARN`s and returns if
`gamepadCount >= GAMEPAD_MAX`; otherwise `SDL_OpenGamepad` into the first free
slot (`gamepadCount++`). If no free slot is found after opening (defensive
fallthrough), closes the pad again. Called by `processInput`; exported with DAPI
so external code could also feed it events.

### `DAPI void checkForGamepadRemoved(SDL_Event *event)`
Handler for `SDL_EVENT_GAMEPAD_REMOVED`. Finds the slot matching
`event->gdevice.which`, `SDL_CloseGamepad`s it, NULLs the slot, decrements
`gamepadCount` (guarded against underflow). Called by `processInput`.

### `DAPI b8 isKeyDown(KeyCode key)` / `DAPI b8 isKeyUp(KeyCode key)`
Level-triggered keyboard queries. Both re-fetch `state = SDL_GetKeyboardState(NULL)`
each call and index by `key` (KeyCode values are SDL scancodes, so direct
indexing is valid). Return `state[key]` / `!state[key]`. Note: **not** gated by
`imguiCapturingInput` (unlike the button/mouse queries). Called internally by
`getKeyboardAxis`; also intended for game code.

### `DAPI b8 isButtonDown(u32 controllerID, ControllerCode button)` / `DAPI b8 isButtonUp(u32 controllerID, ControllerCode button)`
Gamepad button queries against `gamepads[controllerID]` via
`SDL_GetGamepadButton`. Both return `false` early when `imguiCapturingInput` is
set (note: `isButtonUp` returning false while UI has focus is the
"captured" behavior as written, even though the button is physically up). If the
slot is NULL they log `DEBUG("No gamepad found...")` and return `false`. No
bounds check on `controllerID` against `GAMEPAD_MAX`. No call sites in these
files — external API.

### `DAPI b8 isMouseDown(u32 button)`
Returns whether mouse `button` (see `enum MouseButton`: `MOUSE_LEFT=0`,
`MOUSE_RIGHT=1`, `MOUSE_MIDDLE=2`, `MOUSE_X1=3`, `MOUSE_X2=4`) is pressed via
`SDL_GetMouseState(NULL,NULL) & SDL_BUTTON_MASK(button)`. Returns `false` when
`imguiCapturingInput`. Note: SDL's `SDL_BUTTON_MASK` expects 1-based button
indices (SDL_BUTTON_LEFT == 1) while `MOUSE_LEFT` is 0 — see open questions. No
call sites in these files.

### `DAPI void getMouseDelta(f32 *x, f32 *y)`
Thin wrapper over `SDL_GetRelativeMouseState(x, y)` — writes the relative mouse
motion accumulated since the previous call into `*x`/`*y`. Side effect: SDL
resets its internal relative accumulator, so this is a consume-once API per
frame. Called by `updateInputAxes`; also exported.

### `DAPI Vec2 getKeyboardAxis()`
Builds a WASD movement vector from `isKeyDown(KEY_D/A/W/S)`: x = D(+1)+A(-1)
then negated (`axis.x = -axis.x`), y = W(+1)+S(-1). Returns the raw (un-clamped,
un-deadzoned) Vec2. Called by `updateInputAxes`; also exported.

### `DAPI Vec2 getJoystickAxis(u32 controllerID, JoystickCode axis1, JoystickCode axis2)`
Reads two raw SDL gamepad axes from `gamepads[controllerID]`, negates both,
normalizes by 32767. Returns `{0,0}` if slot is NULL. No deadzone applied (the
deadzoned path is the static `getPrimaryJoystickAxis`). No call sites in these
files — external API. No bounds check on `controllerID`.

### `DAPI void updateInputAxes(void)`
Recomputes the four global axes each call. If `imguiCapturingInput`, zeroes all
four and returns. Otherwise: fuses `getKeyboardAxis()` +
`getPrimaryJoystickAxis(JOYSTICK_LEFT_X/Y)` into
`xInputAxis = clamp(-kb.x + -stick.x, -1, 1)`, `yInputAxis = clamp(kb.y +
stick.y, -1, 1)`; fuses `getMouseDelta` + right stick (scaled by
`LOOK_AXIS_GAMEPAD_SCALE` = 18.0) into `xLookAxis = -mouseX + ...`,
`yLookAxis = -mouseY + ...` (look axes are un-clamped; mouse part is
pixels-per-frame, stick part is normalized*18). Side effect: consumes the SDL
relative-mouse accumulator. No call sites in these files — likely called once
per frame by the application loop.

### `DAPI Vec2 getInputAxis(void)` / `DAPI Vec2 getLookAxis(void)` / `DAPI f32 getInputAxisX(void)` / `getInputAxisY` / `getLookAxisX` / `getLookAxisY`
Pure getters returning the cached globals set by `updateInputAxes`. No side
effects. No call sites in these files — game-facing API. (The globals are also
directly `DAPI extern`, so callers can bypass the getters.)

### `DAPI void setInputCaptureState(b8 captured)`
Sets the file-static `imguiCapturingInput` flag ("Global flag set by editor when
ImGui captures input"). While set: axes are zeroed by `updateInputAxes`, and
`isButtonDown/Up` and `isMouseDown` return false. Keyboard queries are NOT
gated. No call sites in these files — caller is the editor/ImGui layer.

### `DAPI void setMouseCaptured(b8 captured)`
Enables/disables SDL relative mouse mode on `renderer->display->sdlWindow`
(guarded: silently does nothing, including not updating the `mouseCaptured`
flag, if `renderer`/`display`/`sdlWindow` is NULL). Side effects: hides/warps
the OS cursor per SDL relative mode; sets static `mouseCaptured`. No call sites
in these files.

### `DAPI b8 isMouseCaptured(void)`
Returns the static `mouseCaptured` flag (tracks the last successful
`setMouseCaptured` call, not live SDL state). No call sites in these files.

### Macros (input.c, internal)
- `INPUT_AXIS_DEADZONE` (0.2f) — radial-per-axis deadzone used only by
  `getPrimaryJoystickAxis`.
- `LOOK_AXIS_GAMEPAD_SCALE` (18.0f) — converts normalized right-stick deflection
  to the same magnitude domain as per-frame mouse pixels in `updateInputAxes`.

### Macro (druid.h:2440)
- `GAMEPAD_MAX` (4) — size of the `gamepads[]` slot table; hotplug beyond this
  is refused with a `WARN`.

## Data Structures

### Enums (druid.h)
- **`KeyCode`** (~1900s–2097): named keys aliasing SDL3 scancodes
  (`KEY_D = SDL_SCANCODE_D`, ..., `KEY_COUNT = SDL_SCANCODE_COUNT`). Used to
  index the SDL keyboard-state array directly.
- **`ControllerCode`** (2099–2123): gamepad buttons aliasing
  `SDL_GAMEPAD_BUTTON_*`, with both Xbox (`BUTTON_A/B/X/Y`) and PlayStation
  (`BUTTON_CROSS/CIRCLE/SQUARE/TRIANGLE`) names mapping to the same values.
- **`JoystickCode`** (2125–2133): stick/trigger axes aliasing
  `SDL_GAMEPAD_AXIS_*` (left/right X/Y, left/right trigger).
- **`enum MouseButton`** (2136–2143): `MOUSE_LEFT=0 .. MOUSE_X2=4`,
  `MOUSE_BUTTON_COUNT=5` (0-based, unlike SDL's 1-based button numbering).

### Globals (exported, `DAPI extern` in druid.h)
- **`SDL_Gamepad *gamepads[GAMEPAD_MAX]`** (input.c:5): slot table of open
  gamepad handles; NULL = free slot. Handles are owned by this module: opened in
  `initInput`/`checkForGamepadConnection`, closed in
  `checkForGamepadRemoved`/`destroyInput`. SDL-owned objects, not ECS-managed;
  the array itself is static storage. Slot index doubles as the `controllerID`
  used by `isButtonDown`/`getJoystickAxis`.
- **`u32 gamepadCount`** (input.c:6): count of occupied slots. Defined non-static
  in input.c but not found among the declarations grepped in druid.h — appears
  to be exported-by-linkage only.
- **`f32 xInputAxis, yInputAxis, xLookAxis, yLookAxis`** (input.c:8–11): the
  fused per-frame axes. Written only by `updateInputAxes`; read by the six
  getters and (via `DAPI extern`) any external code. Static storage, process
  lifetime.

### File-static state (input.c)
- **`SDL_Event evnt`** (line 3): scratch event reused by `processInput`'s poll
  loop.
- **`const b8 *state`** (line 4): cached pointer from `SDL_GetKeyboardState`
  (SDL-owned memory; re-fetched on every `isKeyDown`/`isKeyUp` anyway).
- **`b8 imguiCapturingInput`** (line 14): editor/UI focus flag, set via
  `setInputCaptureState`.
- **`b8 mouseCaptured`** (line 335, declared mid-file): mirrors the last
  successful relative-mouse-mode change.

Nothing in this module is ECS-managed or heap-allocated by the engine; all
dynamic resources are SDL handles.

## Internal (static) functions

- `getPrimaryGamepad(void)` — returns the first non-NULL entry in `gamepads[]`
  (the "primary" pad), or NULL.
- `applyAxisDeadZone(f32 value, f32 deadZone)` — rescales an axis so output is 0
  inside the deadzone and ramps linearly to ±1 outside it, clamped.
- `getPrimaryJoystickAxis(JoystickCode axis1, JoystickCode axis2)` — negated,
  normalized, deadzoned two-axis read from the primary gamepad; `{0,0}` if none.

## Open questions / unclear intent

1. **Who calls `processInput` and `updateInputAxes`** — no call sites exist in
   these files. `processInput(Application*)` is presumably invoked by the main
   loop (`run`/`startApplication` in the application module), but the actual
   caller and call order relative to `updateInputAxes` (which must run after the
   pump but before game logic to be coherent) cannot be confirmed from this file
   set.
2. **`isMouseDown` button indexing** — `enum MouseButton` is 0-based
   (`MOUSE_LEFT = 0`) but SDL3's `SDL_BUTTON_MASK` is defined for 1-based
   buttons (`SDL_BUTTON_LEFT == 1`, mask `1 << (button-1)`). Passing
   `MOUSE_LEFT` (0) yields a mask of `1 << -1`. Whether callers are expected to
   pass the engine enum or SDL's 1-based constants is unclear from these files.
3. **`destroyInput` calls `SDL_QuitSubSystem(SDL_INIT_EVENTS)` before closing
   gamepads**, and never quits the gamepad subsystem it implicitly relies on;
   also `gamepadCount` is not reset. Whether this ordering/omission is
   intentional (e.g., display module owns SDL_Quit) is not determinable here.
4. **Double negation conventions** — `getKeyboardAxis` negates x
   (`axis.x = -axis.x`), `getPrimaryJoystickAxis`/`getJoystickAxis` negate both
   raw axes, and `updateInputAxes` negates again (`-keyboardAxis.x`,
   `-moveStickAxis.x`, `-mouseX`). The intended handedness/screen convention
   (why x is flipped twice for movement but y isn't) is not documented anywhere
   in these files.
5. **Keyboard not gated by `imguiCapturingInput`** — button and mouse queries
   return false while ImGui has focus, but `isKeyDown`/`isKeyUp` (and therefore
   any direct external use of them) are not gated; only the fused axes are.
   Whether that asymmetry is deliberate is unclear.
6. **`gamepadCount` linkage** — defined non-static in input.c but no `extern`
   declaration was found in druid.h's input section; it may be declared
   elsewhere or intended as internal despite external linkage.
7. **`isButtonUp` semantics under capture** — returning `false` (i.e., "not up")
   when ImGui captures input inverts the physical state; whether callers rely on
   this is unknowable from this file set.
