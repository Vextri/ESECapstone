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
- [x] LCD power source confirmed — draws 5V, not 3.3V, so it does NOT share the LM317-regulated sensor rail. Not currently stated anywhere in the report body; worth a one-line addition to §5.1 power architecture discussion if you want it explicit (keeps the sensor rail's noise isolation claim more complete), but not blocking anything

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

**Numbering fix**
- [x] Table 5.5 duplicate fixed — whole tail of the sequence was off (5.6-5.9), all renumbered to match document order, all 4 cross-references updated

**Integration (INT-1)**
- [ ] `[PHOTO]` Perfboard control board (top, bottom, installed)
- [ ] `[DIAGRAM]` Altium schematic (full capture, legible even split across pages)
- [ ] `[PHOTO]` PCB progression: layout view, 3D render, bare board, populated board

## Sections 4/7 — Methodology & Implementation
- [x] Table 4.3 fixed — five signed test plans + supplementary Week 12 record (not "seven"), duplicate row merged, all files now consistent
- [~] `[WRITE]`/`[PHOTO]` Week 13 schedule row — milestone text updated (final assembly done, reliability trial/demo in progress); still needs photos and final reliability-trial results once complete
- [x] Drop site 3 confirmed resolved on PCB build — still needs `[PHOTO]` of the cleared path
- [x] Background-sync fix verified — 15 dispense cycles, no recurrence
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
- [x] Section 9 (9.1–9.4) fully reviewed and written — Table 9.1 all 36 requirements verdicted, Table 9.2 computed, Table 9.3 partially filled (FU-2/INT-1 complete; FU-1/FU-3/FU-4/FU-5 need actual test-count totals from the signed Board/Mechanical/ESP32-S3 test plan PDFs — not derivable from the markdown files), §9.4 uncertainty/limitations analysis complete
- [ ] `[DATA]` Table 9.3 remaining totals — Board Test Plan (FU-1, and Phase 5 alone for FU-3), Mechanical Test Plan (FU-4), ESP32-S3 per-category counts (FU-5); pull from Appendix G documents when available
- [ ] `[WRITE]` Section 8: much of it now written; remaining placeholders are genuinely data-dependent (see items below)
- [ ] `[CHART]` Power rail margin (bar chart, measured vs. spec band)
- [ ] `[CHART]` Current draw by operating condition
- [ ] `[DATA]`/`[PHOTO]` Oscilloscope: 5V rail on wall power vs. battery; LM317 output both conditions
- [ ] `[DATA]` Timing measurements (flagged placeholder, specifics TBD)
- [ ] `[SIGN-OFF]` Complete Phase 4 flash persistence tests (blocking — supports R-08A/R-08B) + resolve the blank K-03 result
- [x] ESP32-S3 Firmware Test Plan — passed, Table 8.9 updated (all categories Pass)
- [x] Seven outstanding functional prototype tests — marked passed per confirmation through direct operation of the final unit (Table 8.10); still needs the signed Functional Prototype Test Plan in Appendix G to reflect the same status
- [ ] `[DATA]` Status check: button replacement, wiring congestion, pinched power lead (safety issue if unresolved — flag in Section 10.1 if so)
- [x] Convert "implemented" items to "verified" — escalating reminders, low-pill alerts, per-motor speed control, slot 2 rewire/direction all confirmed and updated in Table 8.11
- [ ] `[CHART]` Dispense success rate by slot and fill level (with R-01 target line)
- [ ] `[CHART]` Attempts required per dispense (histogram: 1/2/3 attempts)
- [ ] `[CHART]` IR confirmation delay distribution (histogram, 300ms window marked)
- [ ] `[DATA]`/`[PHOTO]` Oscilloscope: piezo impact + IR beam-break (Results-section versions)
- [x] Current-draw anomaly (§8.9) — resolved, combination of 3.3V distribution bottleneck + inconsistent motor de-energisation, fixed via wiring + firmware changes; stable 400mA idle on Unit 2 PCB build. `[DATA]` optional: a before/after current measurement would strengthen this for PA3
- [x] Part B validation summary — written in §9.2 from Table 9.1/9.2 compliance data (10/15 Must Met, 5 Partially Met; 9/11 Should Met; full breakdown in Table 9.2)
- [ ] `[CHART]` Idle current breakdown by component
- [ ] `[CHART]` Current draw vs. cumulative dispense events
- [ ] `[WRITE]` Pill geometry characterisation (optional but narrows a stated uncertainty)
- [~] `[DATA]` Reliability trial — 50 cycles total, ~44-45 succeeded (~3 needed retry, ~2 full jam), zero double-feeds confirmed, recollection-based approximate figures now in Table 8.2. Still open: per-slot/fill-level breakdown not tracked, so Tables 8.3/8.4 remain empty
- [x] State which unit/firmware revision produced each dataset — reliability trial ran across both units combined (noted with caveat in §9.4.3); battery runtime and current-draw resolution specifically on Unit 2
- [x] Drop site 3 status — confirmed resolved on PCB build (Section 7.4)
- [x] Battery runtime measurement (R-19) — 6600mAh/24.42Wh pack (2x3300mAh parallel), 400mA idle measured on the 5V rail on Unit 2 PCB build. Corrected calc (energy-based per §5.1.4's own formula, not simple mAh/mA ratio): P=5V×0.4A=2.0W, t=E/P=12.21h calculated, ~10.4-11.0h derated, ~43-46% of the 24h requirement, verdict Not Met
- [ ] `[CHART]` Requirements compliance by priority (stacked bar, Met/Partially Met/Not Met) — good for the presentation too

## Sections 10-14 — Impact, Ethics, Conclusion
- [x] Section 10 (10.1-10.4) reviewed — duplicate heading fixed, H4 cross-ref and content corrected (anomaly was confirmed active on Unit 1, resolved on Unit 2), life-cycle discussion skipped per decision (optional, existing §10.4 coverage is sufficient)
- [ ] `[DATA]`/`[SAFETY]` H5 — pinched power supply lead status unconfirmed. Physically inspect where the power cable passes through the Unit 2 enclosure for pinch points/sharp bends before the demonstration; update H5 (§10.1) and the related note in §8.5 once checked
- [x] §10.4 RoHS claim confirmed accurate, kept as-is
- [x] Section 11 (11.1-11.5) reviewed — duplicate heading fixed, stale §8.8 "unresolved anomaly" reference replaced with the still-accurate R-19 battery shortfall example, Table 11.1 standards verified (RoHS row removed, CSA/WCAG deliberately not added since neither was actually consulted)
- [x] Section 14 (14.1-14.7) reviewed — duplicate heading fixed, Must/Should/Could counts + full G1-G8 goals assessment written from Table 9.1/9.2, broken 25-40 numbering fixed in Strengths/Weaknesses (same copy-paste bug family), stale "unresolved anomaly" weakness replaced with real battery-runtime shortfall, §14.5 item 1.2 rewritten (thermal-measurement follow-up, was stale anomaly reference), two more broken cross-refs fixed (§8.7→§8.8, §9.5 #8→#7 caused by my own earlier renumbering)
- [x] Health Canada device classification statement — written, Class I or II with justification, framed as "would likely require...subject to determination by Health Canada"
- [x] Section 12 (12.1-12.3) reviewed and completed — duplicate heading fixed, two wrong cross-refs fixed in Table 12.1 (§5.5.4→§5.5.5 mutex, §8.7→§8.8 battery), all bracketed prior-skill-level/source cells filled in Table 12.2, Engineering Analyses 2/3/4 written
- [x] Engineering Analysis 2: GPIO protection divider bounds — written (3.44V worst-case clamp, 2.15V V_IH margin)
- [x] Engineering Analysis 3: LM317 thermal analysis — written using measured 31mA no-load 3.3V rail current (P-04); ~2.6°C rise, T_J~27.6°C, well below 125°C max, no heatsink required
- [x] Engineering Analysis 4: battery runtime calculation — written. IMPORTANT: this triggered a correction to §8.8/§9.1/§9.5 — the original calc used simple mAh/mA ratio (16.5h) but the report's own §5.1.4 methodology requires energy-based calc since 400mA was measured on the 5V rail post-boost-conversion, not at the battery. Corrected: E=24.42Wh, P=5V×0.4A=2.0W, t=12.21h calculated, ~10.4-11.0h derated (~43-46% of 24h target, still Not Met)
- [x] Engineering Analysis 5: stepper angular displacement — already correctly reframed as N/A (sensor-terminated design), no action needed
- [x] Section 13 reviewed and mostly closed out — duplicate heading fixed, R-14/R-15 ID collision fixed (renamed R-37/R-38, they clashed with existing requirement IDs), risk closures R-01/02/03/05/06/08/09/10/13 filled in with real Section 8/9 evidence
- [x] R-37/R-38 S/O/D scores confirmed as proposed (S7/O3/D5 RPN105; S6/O8/D4 RPN192)
- [ ] `[DECISION]` Optional: state the RPN Low/Medium boundary explicitly in §13.1 (only "≥80 = High" is currently defined; table usage implies Low is roughly ≤30 but this isn't stated in the methodology text)
- [x] Goals achieved section (G1-G8) — written, evidence-based against Table 9.1, honest about partial achievement (G1/G2/G5/G8 Partially Achieved, G3/G4/G6/G7 Achieved)
- [ ] `[DATA]` §14.1: actual firmware line count (bracketed placeholder, e.g. `git ls-files '*.c' '*.h' | xargs wc -l`)
- [ ] `[WRITE]` Lessons learned / §14.4 Learning Experience (must be written by you and Alan directly — the section's own instruction says so, not something Claude should draft)
- [ ] `[DATA]`/`[WRITE]` §14.6 Commercialisation — blocked on Appendix D's BOM cost data (still empty); write once real prototype cost is entered
- [ ] `[WRITE]` §14.7 Closing conclusion paragraph (write last, 3-4 sentences, no overclaiming — deliberately left for the very end)

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
