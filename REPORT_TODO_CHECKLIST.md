# PortaPill Report To-Do Checklist

Running list of everything the report still needs, across every section. Organized by section so it's easy to work through with Claude one piece at a time. Tell me what you've finished and I'll update the checkboxes, or update them yourself.

**Tags:** `[DIAGRAM]` `[PHOTO]` `[CHART]` `[DATA]` (a measurement/test result needed) `[DECISION]` (your call needed) `[WRITE]` (prose to write, often only after data exists) `[SIGN-OFF]`

Status key: `[ ]` not started · `[~]` in progress · `[x]` done

---

## Front Matter
- [~] `[WRITE]` Abstract — drafted (problem, solution, verification differentiator, offline architecture, dual-MCU safety property all written); two sentences left as placeholders for results and closing claim, to fill in once Section 9 testing is complete

## Sections 1-2 — Introduction & Literature Review
Content is done. Only open item:
- [~] `[DIAGRAM]` Figure 1 — system block diagram (draft exists, needs final polish)

## Sections 3-4 — Requirements & System Design
Content is done. Open items:
- [ ] `[DATA]` Test IDs for R-15, R-32, R-36 in the traceability matrix (deferred until we reach the testing sections)
- [ ] `[DIAGRAM]` Figure 2 — dispense state machine diagram
- [ ] `[DECISION]` Fold your BOM data into Table 4.4, or keep it separate for Appendix D?
- [ ] `[DATA]` Optional: verify whether the LCD draws 3.3V from the ESP32-S3's own onboard regulator or the LM317 sensor rail

## Section 5 — Functional Units (the big one — heaviest diagram/data section)

**Power (FU-1)**
- [ ] `[DIAGRAM]` Power distribution block diagram
- [ ] `[DIAGRAM]` LM317 regulator schematic (R1/R2 values, input/output capacitors)
- [ ] `[WRITE]` Thermal analysis for the LM317 — now scoped to the 3.3V sensor rail current only, not both MCUs (power architecture correction applied)
- [~] `[DATA]` Battery runtime — multi-day test in progress (full idle day, moderate-use day, run to depletion); also run the `Q`-command de-energize check while on the bench (see note below)
- [ ] `[DATA]` Optional: confirm whether the UPS module is genuinely the HW-465A cited in the report, or the "EStarDyn 15W 3A" branded board shown in photos — may be the same part resold, worth a 2-minute label check for the Appendix E datasheet reference
- [ ] `[DATA]` Diagnose the climbing-idle-current symptom (current rises with motor use and doesn't return to baseline) — likely cause: motors not de-energized automatically at end of a normal dispense cycle (only at boot and via manual `Q`). Test: run a motor, let it finish normally, check current, then send `Q` manually and see if it drops. If so, this is a small firmware fix (call the de-energize routine at the end of every dispense attempt), and genuinely good ED4 material once diagnosed and fixed.

**Embedded Control (FU-2)** — GPIO table, drive parameters, and pickup-state ownership all confirmed and written up
- [x] Motor GPIO assignment table — all 12 pins confirmed and written into Table 5.1
- [ ] `[DIAGRAM]` RP2350 interface schematic (piezo/IR conditioning with resistor values, 3 DRV8833 connections, UART1 with direction arrows)
- [ ] `[DIAGRAM]` Interrupt dispatch flowchart
- [ ] `[DIAGRAM]` **Dispense state machine flowchart** — the single most important diagram in this section
- [ ] `[DATA]` Boot sequence serial trace (real capture from final firmware)
- [ ] `[DIAGRAM]` Flash memory layout diagram (annotated memory map is enough)
- [x] Stepper drive parameters — confirmed (full-step bipolar, per-motor timing); no fixed steps-per-dispense exists by design (sensor-terminated, not step-counted)
- [ ] `[DATA]` Optional: if a representative step-count figure is still wanted for illustration, log `stepper_get_step_count()` immediately after a real dispense (currently unlogged) — not required, since the angular-displacement calculation doesn't apply to a sensor-terminated design

**Sensing (FU-3)**
- [x] Divider network resistor values — confirmed from schematic images: piezo 1kOhm series / 2.2kOhm shunt (worked the full Vout calculation, worst-case ~3.44V); IR and Hall are single 10kOhm pull-ups to 3.3V, not dividers
- [ ] `[DIAGRAM]` Dispense verification timing diagram (motor/piezo/IR on one time axis, success + failure/retry cases)
- [ ] `[DATA]`/`[PHOTO]` Oscilloscope capture: piezo impact waveform, real pill drop
- [ ] `[DATA]`/`[PHOTO]` Oscilloscope capture: IR beam-break transition

**Mechanical (FU-4)**
- [ ] `[DIAGRAM]` SolidWorks assembly views (required set, TBD)
- [ ] `[PHOTO]` Iteration photographs (2-3 across your 8 iterations, captioned)
- [ ] `[DATA]` Final assembled envelope: L x W x H, mass with/without batteries, per-container pill capacity (R-16 claims need real numbers)

**User Interface (FU-5)** — GPIO table, notification content, topic security, and WPA2 all confirmed and written up
- [x] ESP32-S3 GPIO assignment table — all pins confirmed (LCD, LED, UART, I2S, Hall x3, buttons x5, USB console) and written into Table 5.8
- [ ] `[DIAGRAM]` ESP32-S3 FreeRTOS task architecture diagram (tasks, shared state, mutex boundary)
- [ ] `[DIAGRAM]` Optional: ESP32-S3 hardware interface schematic (LCD/audio/Hall/button wiring), parallel to the RP2350 one above — not previously tracked, worth considering now that Table 5.8 documents so many ESP32-S3 pins
- [ ] `[PHOTO]` Web dashboard screenshot (final version, real data, not placeholder text)
- [ ] `[PHOTO]` LCD interface screens: boot/main, slot menu, profile edit, schedule editor, dispense in progress/success/failure, low-pill alert (photograph the physical screen, not a render)
- [x] Notification payload content — confirmed (names the actual medication in every message)
- [ ] `[DECISION]`/`[TODO]` Change the ntfy topic from "Portapill_Notify" to something random before this ships with real medication data — replacement generated (`pp-e66c1fabea51ec7b5c9c9441098b42a4`), still needs to be applied in `ntfy_credentials.h` and resubscribed on your ntfy client
- [x] WPA2 confirmed active (9-character passphrase, genuinely on the WPA2 path)
- [x] Pickup lifecycle state ownership (Pending/Taken/Failed/Timeout) — fully traced and documented: RP2350 supplies only the raw dispense success/fail bit; ESP32-S3 decides all four lifecycle states unilaterally via its own sensor and clock
- [x] Per-station reminder "independence" clarified — pickup state is tracked per-slot but cleared collectively on any drawer-open event (single shared drawer, single active Hall sensor); documented as a deliberate design choice, not a bug

**Integration (INT-1)**
- [ ] `[PHOTO]` Perfboard control board (top, bottom, installed)
- [ ] `[DIAGRAM]` Altium schematic (full capture, legible even split across pages)
- [ ] `[PHOTO]` PCB progression: layout view, 3D render, bare board, populated board

## Sections 4/7 — Methodology & Implementation
- [ ] `[WRITE]` Complete the Week 13 schedule row (final assembly, reliability trial, final demo)
- [ ] `[DATA]` Confirm drop site 3 is resolved on the PCB build + photo of the cleared path
- [ ] `[DATA]` Verification status for the background-sync fix (how many cycles run, over what period, no recurrence)
- [ ] `[DIAGRAM]` V-model test strategy diagram
- [ ] `[PHOTO]` Mechanical fabrication evidence
- [ ] `[PHOTO]` Perfboard build (top/bottom/installed/button-LED mini board)
- [ ] `[PHOTO]` PCB build evidence (same set)
- [ ] `[DATA]`/`[PHOTO]` Optional: oscilloscope capture of 3.3V rail during audio activity, before/after your fix
- [ ] `[DATA]` Captured serial log evidence of the merged-command bug
- [ ] `[PHOTO]` GitHub repository screenshot (commit history)
- [ ] `[PHOTO]` Final assembly photos
- [ ] `[WRITE]` Required deliverable noted in the source (check what this is when we get here)

## Sections 8-9 — Results and Validation (biggest data-dependent section)
- [ ] `[WRITE]` Whole section, once testing is complete — most of it is placeholder until then
- [ ] `[CHART]` Power rail margin (bar chart, measured vs. spec band)
- [ ] `[CHART]` Current draw by operating condition
- [ ] `[DATA]`/`[PHOTO]` Oscilloscope: 5V rail on wall power vs. battery; LM317 output both conditions
- [ ] `[DATA]` Timing measurements (flagged placeholder, specifics TBD)
- [ ] `[SIGN-OFF]` Complete Phase 4 flash persistence tests (blocking — supports R-08A/R-08B) + resolve the blank K-03 result
- [ ] `[DATA]` Populate results from the ESP32-S3 Firmware Test Plan record
- [ ] `[DATA]`/`[SIGN-OFF]` Seven outstanding functional prototype tests (flagged as quick/blocking)
- [ ] `[DATA]` Status check: button replacement, wiring congestion, pinched power lead (safety issue if unresolved — flag in Section 10.1 if so)
- [ ] `[WRITE]` Convert "implemented" items to "verified" where confirmed in use
- [ ] `[CHART]` Dispense success rate by slot and fill level (with R-01 target line)
- [ ] `[CHART]` Attempts required per dispense (histogram: 1/2/3 attempts)
- [ ] `[CHART]` IR confirmation delay distribution (histogram, 300ms window marked)
- [ ] `[DATA]`/`[PHOTO]` Oscilloscope: piezo impact + IR beam-break (Results-section versions)
- [ ] `[WRITE]` Investigate and report the highest-value diagnostic item (flagged, check specifics when we arrive)
- [ ] `[WRITE]` Part B validation summary (how many tests run/passed)
- [ ] `[CHART]` Idle current breakdown by component
- [ ] `[CHART]` Current draw vs. cumulative dispense events
- [ ] `[WRITE]` Pill geometry characterisation (optional but narrows a stated uncertainty)
- [ ] `[DATA]` Actual cycle counts per slot + honest confidence-interval statement
- [ ] `[WRITE]` State which unit/firmware revision produced each dataset (perfboard vs. PCB results aren't interchangeable)
- [ ] `[DATA]` Drop site 3 status (duplicate of the item above, confirm once)
- [~] `[DATA]` Battery runtime measurement on the PCB build (R-19) — multi-day test in progress, see Section 5 Power notes above
- [ ] `[CHART]` Requirements compliance by priority (stacked bar, Met/Partially Met/Not Met) — good for the presentation too

## Sections 10-14 — Impact, Ethics, Conclusion
- [ ] `[WRITE]` Optional life-cycle discussion (materials, manufacture, use-phase, service life, end-of-life)
- [ ] `[WRITE]` Verify and extend the flagged subsection (check specifics when we arrive)
- [ ] `[WRITE]` Health Canada device classification statement (framed as "would likely require...", not asserted as fact)
- [ ] `[WRITE]` Complete bracketed cells: prior skill levels and how they were actually acquired, be specific about sources
- [ ] `[WRITE]` Engineering Analysis 2: GPIO protection divider bounds
- [ ] `[WRITE]` Engineering Analysis 3: LM317 thermal analysis (ties to the Section 5 one above)
- [ ] `[WRITE]` Engineering Analysis 4: battery runtime calculation
- [ ] `[WRITE]` Engineering Analysis 5: stepper angular displacement per dispense — reframed: dispense is sensor-terminated, not step-counted, so there's no fixed figure to derive; if still wanted, log a few real `stepper_get_step_count()` values and report a range, not a single derived spec
- [ ] `[WRITE]` Risk register: close out R-01, R-02, R-03, R-05, R-06, R-08, R-09, R-10, R-13 with actual status, and score newly-identified risks R-14/R-15
- [ ] `[WRITE]` Goals achieved section (G1-G8, one to two sentences each, evidence-based, honest about partial achievement) — write after Section 9 is done
- [ ] `[WRITE]` Expand supporting evidence for flagged claims
- [ ] `[WRITE]` Lessons learned (must be written by you and Blaise directly — not something I should draft)
- [ ] `[WRITE]` Elevator pitch paragraph (also needed for the final presentation)
- [ ] `[WRITE]` Closing conclusion paragraph (write last, 3-4 sentences, no overclaiming)

## Appendices
- [ ] `[WRITE]` Clean up reference list (see earlier notes — some entries incomplete)
- [ ] `[DATA]` Attach/reference datasheets: RP2350, ESP32-S3, DRV8833, LM317, 28BYJ-48, MAX98357A, ST7796S, HW-465A, 18650 cell, piezo, IR pair, Hall sensor
- [ ] `[SIGN-OFF]` All test plans signed and dated before submission
- [ ] `[WRITE]` Optional appendix item (check specifics when we arrive)

## Presentation-Only Extras (not required in the written report)
- [ ] `[DIAGRAM]` Simplified/elevator-pitch version of Figure 1
- [ ] `[DIAGRAM]` Capstone I to Capstone II "what changed and why" summary slide (content already exists in Section 1.5's Table 1.1)
- [ ] `[DIAGRAM]` Live demo flow diagram (updated version of your old Capstone I "Connect to Wi-Fi -> Load UI -> Send Command -> Check Status" diagram)

---
**How to use this:** work through it section by section with me as we go, same as we've been doing. Tell me what's done and I'll flip the checkbox; tell me when something new comes up (a new test, a new diagram, a newly-discovered gap) and I'll add it here.
