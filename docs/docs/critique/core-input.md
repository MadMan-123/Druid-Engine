# Critique: core-input

Files: `src/core/input.c`, input sections of `include/druid.h` (2097–2144, 2423–2463).

## Dead / unreachable / redundant code

1. **[structural] [READ-VERIFIED]** `initInput`, `destroyInput`, and engine `processInput` (input.c:56/96/163) have **zero call sites in the entire repo**. The frame loop (`application.c:9-14 inputUpdate`) only calls the `app->inputProcess` hook + `updateInputAxes`; the editor installs its own C++ `processInput` (Editor/src/main.cpp:136, 1126). So gamepad hotplug, the slot table, and `SDL_EVENT_QUIT` handling are dead unless game code manually wires them — meaning `gamepads[]` stays all-NULL and every gamepad query is a silent no-op by default.
2. **[moderate] [READ-VERIFIED]** `enum MouseButton` (druid.h:2136-2144) is dead *and* wrong: the only `isMouseDown` callers (Editor/src/main.cpp:62, 130) pass SDL's 1-based `SDL_BUTTON_RIGHT`, because `SDL_BUTTON_MASK(MOUSE_LEFT)` = `1u << (0-1)` is UB and `MOUSE_RIGHT`(1) would test the *left* button. The enum must be deleted or remapped to 1-based.
3. **[trivial] [READ-VERIFIED]** `static const b8 *state` (input.c:4) caching is pointless — `isKeyDown`/`isKeyUp` (277/283) re-fetch `SDL_GetKeyboardState(NULL)` every call, so the `initInput:59` assignment is redundant.
4. **[trivial] [READ-VERIFIED]** `SDL_PumpEvents()` at processInput:165 is redundant; `SDL_PollEvent` pumps internally.
5. **[trivial] [READ-VERIFIED]** Six scalar getters (`getInputAxisX/Y`, `getLookAxisX/Y`, input.c:255-273) duplicate the Vec2 getters, which duplicate the `DAPI extern` globals (druid.h:2460-2463). Three ways to read the same four floats.

## AI-slop patterns

6. **[trivial] [READ-VERIFIED]** checkForGamepadConnection:132-143 — after the `gamepadCount >= GAMEPAD_MAX` guard at 119, a free slot must exist; the trailing "no slot found, close pad" fallthrough defends an impossible state (only reachable if `gamepadCount` desyncs).
7. **[trivial] [READ-VERIFIED]** `isKeyUp`/`isButtonUp` are copy-pastes of the Down variants with `!`. Worse, `isButtonUp` (303) returns `false` ("not up") under ImGui capture — physically inverted semantics for the capture path.
8. **[trivial] [READ-VERIFIED]** Triple-negation maze: `getKeyboardAxis` negates x (200), `getPrimaryJoystickAxis`/`getJoystickAxis` negate raw axes (48-49, 212-213), then `updateInputAxes` negates again (238). Net sign is only derivable by hand-tracing.

## Memory allocation

None (SDL owns everything; no engine heap use).

## Perf

9. **[trivial] [READ-VERIFIED]** isButtonDown/Up:297/311 emit `DEBUG("No gamepad found...")` on every call with no pad — per-frame log spam for any polling game without a controller (compounded by finding 1: the slot table is never populated).

## API ergonomics

10. **[moderate] [READ-VERIFIED]** No bounds check on `controllerID` in `isButtonDown/Up` (294/308) and `getJoystickAxis` (208): `controllerID >= GAMEPAD_MAX` reads out of `gamepads[4]` bounds. The editor already hardcodes `getJoystickAxis(1, ...)` (main.cpp:61) — valid, but one off the array edge.
11. **[moderate] [READ-VERIFIED]** `gamepadCount` (input.c:6) has external linkage with no declaration in druid.h — invisible export, symbol-collision risk; should be `static` or declared.
12. **[moderate] [READ-VERIFIED]** Capture asymmetry: `setInputCaptureState` gates axes/buttons/mouse but not `isKeyDown/isKeyUp` — ImGui text fields still leak WASD to any game polling keys directly.

## Error handling / silent failures

13. **[moderate] [READ-VERIFIED]** `setMouseCaptured` (337): if `renderer`/`display`/`sdlWindow` is NULL it silently does nothing and leaves `mouseCaptured` stale — `isMouseCaptured` then lies. No log, no return value.
14. **[moderate] [READ-VERIFIED]** `destroyInput` (96-108) quits `SDL_INIT_EVENTS` *before* `SDL_CloseGamepad` calls, never quits `SDL_INIT_GAMEPAD` (opened by display.c:32), and never resets `gamepadCount`. Moot today only because it's never called (finding 1). [NEEDS-RUNTIME] whether SDL3 tolerates the ordering.
