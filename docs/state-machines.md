# State machines

Source map of GUI control state and the raylib-free playback controller. Arrows describe accepted calls; guarded calls otherwise leave state unchanged. Short state names below omit each enum's common prefix.

## SolveState

Defined at `apps/gui/gui.h:19`; stored atomically in `App.solving`.

- States: `SOLVE_IDLE`, `SOLVE_RUNNING`, `SOLVE_DONE`. Static `App` initialization starts IDLE in `main`, `apps/gui/main.c:544`.
- IDLE -> RUNNING: `app_start_solve`, `apps/gui/main.c:136`, after cube validation and request capture. Thread creation failure returns RUNNING -> IDLE there.
- RUNNING -> DONE: `solve_worker`, `apps/gui/main.c:129`, publishes output/error before the atomic store.
- DONE -> IDLE: `poll_solver`, `apps/gui/main.c:168`, joins the worker before consuming the result.
- Invariants: request fields stay fixed while RUNNING/DONE; output/error may be read after observing DONE. `app_busy`, `apps/gui/main.c:46`, treats both RUNNING and DONE as busy, preventing another request before collection.

## Transport

Defined at `apps/gui/playback.h:59`; states `TRANSPORT_EMPTY`, `TRANSPORT_PAUSED`, `TRANSPORT_PLAYING`.

- Initial EMPTY: `playback_init`, `apps/gui/playback.c:108`, zeroes the controller.
- Any -> EMPTY: `playback_set_cube`, `apps/gui/playback.c:176`, discards turns; `playback_drop_solution`, `apps/gui/playback.c:192`, finishes pending turns when a solution exists. `playback_turn`, `apps/gui/playback.c:169`, uses the latter before queuing user turns.
- Any -> EMPTY for a zero-move solution, otherwise PAUSED if Teach is on or PLAYING if off: `playback_load`, `apps/gui/playback.c:199`.
- PAUSED <-> PLAYING: `playback_toggle`, `apps/gui/playback.c:211`; playing from the end first seeks to zero.
- PLAYING -> PAUSED at a Teach boundary or solution end: `auto_play`, `apps/gui/playback.c:29`, called by `playback_tick`, `apps/gui/playback.c:146`.
- Nonempty -> PAUSED: `playback_step`, `apps/gui/playback.c:222`, and `playback_seek`, `apps/gui/playback.c:234`. `playback_seek_step`, `apps/gui/playback.c:242`, delegates to seek.
- Teach PAUSED -> PLAYING on matching typed input: `playback_teach_continue`, `apps/gui/playback.c:255`.
- Loaded transport -> PAUSED when coaching starts: `playback_coach_start`, `apps/gui/playback.c:281`.
- Invariants: EMPTY exposes no solution through `playback_view`, `apps/gui/playback.c:114`; stored solution fields may be stale. PAUSED can still have animated/queued turns. Toggle/step/seek reject EMPTY and PRACTICE/UNSYNCED; the latter reserve playhead movement for Coach.

## CoachMode

Defined at `apps/gui/playback.h:22`; states `COACH_OFF`, `COACH_SETUP`, `COACH_PRACTICE`, `COACH_UNSYNCED`, `COACH_REPAINT`, `COACH_DONE`.

- Initial OFF: `playback_init`, `apps/gui/playback.c:108`. OFF -> SETUP: `playback_coach_begin`, `apps/gui/playback.c:266`, drops any solution and resets basis to ASSUMED.
- SETUP/REPAINT -> PRACTICE: `playback_coach_start`, `apps/gui/playback.c:281`, requires a loaded step with moves, pauses transport and selects its first step.
- PRACTICE -> DONE: `coach_ready`, `apps/gui/playback.c:79`, advances past exhausted steps. Called at start and after accepted movement by `coach_drive`, `apps/gui/playback.c:93`; rotation-only steps require confirmation.
- Free PRACTICE -> UNSYNCED: `playback_coach_unsync`, `apps/gui/playback.c:313`. UNSYNCED -> PRACTICE: `playback_coach_restore`, `apps/gui/playback.c:319`, preserves the last accepted position.
- Free PRACTICE/UNSYNCED -> REPAINT: `playback_coach_repaint`, `apps/gui/playback.c:326`, drops the solution. `playback_coach_trust`, `apps/gui/playback.c:333`, and `playback_coach_paint_top`, `apps/gui/playback.c:363`, delegate from UNSYNCED, then apply reported turns or restrict painting to U.
- Any -> OFF: `playback_coach_end`, `apps/gui/playback.c:273`. PRACTICE/UNSYNCED/DONE -> OFF: `coach_forget`, `apps/gui/playback.c:73`, when set_cube/drop_solution removes the practised solution; SETUP/REPAINT survive these operations.
- Invariants: PRACTICE/UNSYNCED lock ordinary turns and transport; DEMO/FOLLOW additionally reject Coach actions until finished. Views derive locks/visibility/running from enums in `playback_view`, `apps/gui/playback.c:114`. Physical agreement is always the user's assumption, including after validation.

## CoachStep

Defined at `apps/gui/playback.h:39`; states `COACH_STEP_HIDDEN`, `COACH_STEP_SHOWN`, `COACH_STEP_DEMO`, `COACH_STEP_FOLLOW`, `COACH_STEP_ATTEMPTED`, `COACH_STEP_MISMATCH`.

- Start/restore -> HIDDEN: `playback_coach_start`, `apps/gui/playback.c:281`, and `playback_coach_restore`, `apps/gui/playback.c:319`.
- HIDDEN <-> SHOWN in free PRACTICE: `playback_coach_show`, `apps/gui/playback.c:294`.
- HIDDEN/SHOWN -> DEMO: `playback_coach_demo`, `apps/gui/playback.c:298`, saves origin and targets the step end. DEMO -> SHOWN: `coach_drive`, `apps/gui/playback.c:93`, restores the origin after demonstrating.
- SHOWN -> FOLLOW: `playback_coach_followed`, `apps/gui/playback.c:306`; HIDDEN/SHOWN -> FOLLOW for a matching prefix: `playback_coach_typed`, `apps/gui/playback.c:340`.
- FOLLOW -> HIDDEN: `coach_drive`, `apps/gui/playback.c:93`, accepts movement and advances steps or enters DONE through coach_ready.
- Free PRACTICE -> ATTEMPTED/MISMATCH with mode UNSYNCED: `playback_coach_unsync`, `apps/gui/playback.c:313`, selects by its attempted argument. Typed mismatch passes true and therefore selects ATTEMPTED, `apps/gui/playback.c:351`.
- Invariants: substate matters only in PRACTICE/UNSYNCED. DEMO/FOLLOW alone imply running, `coach_running`, `apps/gui/playback.c:50`. UNSYNCED hides moves regardless of substate. Ending coaching need not reset this otherwise inactive field.

## CoachBasis

Defined at `apps/gui/playback.h:33`; states `COACH_BASIS_ASSUMED`, `COACH_BASIS_PAINTED` describe provenance, independently of CoachMode.

- Initial ASSUMED: `playback_init`, `apps/gui/playback.c:108`; beginning coaching resets ASSUMED in `playback_coach_begin`, `apps/gui/playback.c:266`.
- Either -> either: `playback_coach_set_basis`, `apps/gui/playback.c:279`. Its caller `app_start_solve`, `apps/gui/main.c:136`, chooses PAINTED after validation for REPAINT or a cube without a matching guided scramble, otherwise ASSUMED.
- Either -> ASSUMED after FOLLOW completes: `coach_drive`, `apps/gui/playback.c:93`. DEMO completion preserves basis.
- Invariant: PAINTED records validation of the screen cube; neither value proves the physical cube matches, `apps/gui/playback.h:31`.

## CoachTyped

Defined at `apps/gui/playback.h:164`; result values `COACH_TYPED_IGNORED`, `COACH_TYPED_MATCH`, `COACH_TYPED_MISMATCH`. This is a per-call result, not a persistent machine.

- `playback_coach_typed`, `apps/gui/playback.c:340`, returns IGNORED outside free PRACTICE or for empty input, MATCH for a valid remaining-step prefix, MISMATCH for a wrong or overlong prefix.
- Effects: IGNORED changes nothing; MATCH sets target and FOLLOW; MISMATCH enters UNSYNCED/ATTEMPTED and applies none of the submitted moves.
- Invariants: comparisons include rotations; accepted movement occurs later through tick/coach_drive. A mismatch keeps the last accepted cube/playhead for restore or trust.

## Keyboard focus

The single owner is `App.focus`, `apps/gui/gui.h:68`, an integer rather than an enum. States are 0 for none and a positive widget stop; equality with focus_input means move entry.

- `ui_focus_keys`, `apps/gui/ui.c:147`, clears on left press or Escape with empty input; Tab/Shift+Tab wrap over last frame's stops, seeking enabled ones.
- `move_input`, `apps/gui/ui.c:537`, selects the enabled prompt on click or deferred focus_prompt_next, and clears focus when that prompt becomes disabled.
- `app_toggle_screen_only`, `apps/gui/main.c:242`, and `handle_view_mouse`, `apps/gui/main.c:367`, clear focus when hiding widgets or starting a camera drag.
- Invariants: static App starts at 0. `ui_focus_stop`, `apps/gui/ui.c:180`, registers draw-order stops and gates activation on enabled; it does not own another focus flag. Stops/enablement are rebuilt each frame, so focus is an ordinal, not a stable widget ID. Tab examines at most focus_last stops; if all are disabled, activation still fails.

## Core enums

No additional enum-backed control machine exists in src/. LessonId at `include/cube/solver.h:14`, SolverId at `include/cube/solver.h:69`, XrayTier at `include/cube/solver.h:134`, and MoveBase at `include/cube/cube.h:40` classify data or select algorithms. TypeFace at `apps/gui/gui.h:98` selects a font. These have no lifecycle transitions.

## Remaining implicit state

- Turn execution uses animating, qhead/qtail and anim_t for idle/queued/animating; `playback_tick`, `apps/gui/playback.c:146`, advances it and `playback_flush`, `apps/gui/playback.c:185`, drains it.
- Teach pause/resume uses teach, teach_paused_at, queued_pos/playhead and Transport; `teach_stop_at_playhead`, `apps/gui/playback.c:22`, recognizes the compound state, and auto_play/teach_continue move it.
- Painting uses edit_mode plus typed_face for inactive/face-entry progress; `app_set_edit`, `apps/gui/main.c:247`, and `app_type_face`, `apps/gui/main.c:221`. REPAINT plus coach_top distinguishes full repaint from U-only, `apps/gui/playback.c:363`.
- Camera dragging/coasting/rest combines dragging and yaw_v/pitch_v; `handle_view_mouse`, `apps/gui/main.c:367`.
- Step-list following/manual scroll/drag combines follow_current, follow_seen, thumb_drag and scroll positions; `ui_right_panel`, `apps/gui/ui.c:1451`, resumes following on playhead change and suspends it on manual scrolling.
- Core algorithm progression uses integers/calls rather than enums: Thistlethwaite's phase loop, `src/solvers/thistlethwaite.c:197`, and Kociemba's phase-1/phase-2 handoff, `src/solvers/kociemba.c:595`. These are synchronous search control, not persistent GUI modes.
