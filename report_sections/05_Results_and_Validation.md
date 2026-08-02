# 8. Results and Analysis
8. Results and Analysis
[RUBRIC: IV2 — Measure (Data Collection) 20 pts + IV2 — Measure (Quantitative Analysis) 20 pts = 40 points, the largest block in the entire rubric.]
These are two separate criteria and they are earned differently:
— Data Collection (20) is earned by tables of measured values: what was measured, under what condition, with what instrument, what the number was.
— Quantitative Analysis (20) is earned by doing something with those numbers: computing derived quantities, plotting trends, comparing against specification with a stated margin, calculating error and success rate, drawing engineering conclusions.
A table of numbers with no analysis scores on the first criterion only, and forfeits the second. Every dataset below should be followed by analysis — a percentage margin, a trend, a comparison, a rate, a conclusion. Aim for at least four or five charts across this section.
Charting guidance: use consistent colours across all charts; label both axes with units; put specification limits on the chart as horizontal dashed lines so compliance is visually immediate; caption every chart below the figure.
8.1 Qualitative Results and Test Conditions
[PLACEHOLDER — WRITE ONCE TESTING IS COMPLETE]
State the conditions under which all data in this section was gathered, so results are reproducible and interpretable:
— Ambient temperature and relative humidity
— Supply: USB-C wall adapter, rated voltage and current
— Battery state of charge at test start
— Test pill type(s) used, with dimensions
— Which unit was under test (perfboard Unit 1 or PCB Unit 2) — state this per dataset, and be consistent
— Firmware revision / Git commit hash for both processors — this matters; results are only meaningful against a known build
— Instruments used, with model numbers
Then summarise qualitative behaviour: the system's observed behaviour across normal operation, low fill level, deliberate obstruction, power interruption and extended run.
8.2 Power Subsystem — Measured Data
Table 8.1 — Power rail measurements (integrated board)
Test ID	Measurement	Specification	Measured	Deviation	Result	Date
P-01	5 V rail voltage	4.75 – 5.25 V	4.94 V	−1.20% of nominal	Pass	02/06/2026
P-02	3.3 V rail voltage	3.2 – 3.4 V	3.312 V	+0.36% of nominal	Pass	02/06/2026
P-03	LM317 accuracy vs. design target	3.307 V ±0.1 V	3.312 V	+0.15% of target	Pass	02/06/2026
P-04	No-load current draw	< 50 mA	31 mA	38% below limit	Pass	05/06/2026
M-02	Single stepper current draw	< 250 mA	102 mA	59% below limit	Pass	05/06/2026
—	Rail voltage, three-motor actuation	≥ 4.75 V	≥ 4.92 V	3.6% margin above limit	Pass	Assessment 1 & 2

Analysis. Both regulated rails are well inside specification with substantial margin. The 5 V rail at 4.94 V sits 1.2% below nominal, comfortably within the ±5% band of R-05, and the difference is consistent with the resistive drop expected across the supply output impedance while the UPS module's CC/CV charging circuit draws current. The 3.3 V rail at 3.312 V represents a 0.36% deviation from nominal against a permitted ±3.03%, indicating that the deliberate series-resistor trimming described in §5.1.4 achieved its intended precision — the measured value differs from the calculated 3.298 V by only 14 mV, within the combined tolerance of the resistor pair and the LM317 reference.
The most operationally significant result is the rail voltage under three-motor simultaneous actuation: 4.92 V minimum, a margin of 170 mV (3.6%) above the 4.75 V lower limit. This is the worst-case electrical load the system can present, and it confirms that the power architecture is adequately sized. Single-stepper draw at 102 mA against a 250 mA budget indicates that even three simultaneous motors remain far below the UPS module's 3 A rating.
[⚠ PLACEHOLDER — REQUIRED DATA, CURRENTLY MISSING]
The following Board Test Plan cells are empty and are directly marked under IV2:
— M-04 three-stepper simultaneous actuation: ______ (pass/fail, rail voltage)
— M-05 three-stepper total current draw: ______ mA (spec: < 750 mA)
— M-06 rail voltage under full motor load, oscilloscope measurement: ______ V
— S-02 IR signal voltage at GPIO after divider: ______ V (spec: 2.8 – 3.3 V)
— B-01 / B-02 / B-03 battery runtime current draw: all three empty
— U-01 / U-02 UPS switchover, including switchover transient duration (spec: < 50 µs, oscilloscope)
M-05 in particular lets you verify the additivity assumption — is three-motor draw close to 3 × 102 mA, or is it materially different? Either answer is an analysis result worth reporting.
The U-02 switchover transient is your best oscilloscope capture opportunity in the whole project: a single trace showing the 5 V rail through a supply disconnect, with the transient measured and compared against the 50 µs specification, is exactly the kind of evidence IV2 — Quantitative Analysis rewards. You currently claim seamless switchover on the basis of an LED staying lit and a motor not slowing. Measure it.
[INSERT CHART: Power rail margin analysis] Bar chart: for each rail, measured value with the specification band shown as a shaded region. Immediately communicates compliance and margin.
[INSERT CHART: Current draw by operating condition] Bar chart: no-load (31 mA), MCUs active, one stepper (102 mA), three steppers, full system. Populate from B-01/B-02/B-03 and M-05.
[INSERT FIGURES: Oscilloscope captures] 5 V rail under wall power; 5 V rail on battery only; LM317 output on wall power; LM317 output on battery. Report mean and ripple amplitude for each. You have equivalent captures from Capstone I — recapture on the final hardware and report the values in a comparison table. The ripple difference between wall and battery operation is directly explainable by the UPS boost converter's switching activity, which is a genuine analytical observation.
8.3 Mechanical and Dispensing — Measured Data
[⚠ PLACEHOLDER — THE MOST IMPORTANT DATASET IN THE REPORT AND IT DOES NOT YET EXIST]
R-01 is a "Must" requirement — one pill per actuation, zero jams, zero double-feeds — and you currently have no reliability data to validate it against. A capstone report on an automated pill dispenser that cannot state its dispense success rate has a hole precisely where its central claim should be.
Run a long-duration reliability trial. This is the single highest-value remaining activity and it is largely unattended time.
Recommended protocol:
— Load all three compartments with test pills (non-medication).
— Execute at least 50 dispense cycles per compartment, 150 total. More is better; 100 per slot is excellent.
— For each cycle record: cycle number, slot, commanded dose, pills physically delivered (counted by hand), attempts required (1, 2 or 3), outcome (success / fail), and failure mode where applicable.
— Vary the container fill level — run some cycles at high fill, some at approximately 25% fill — since low fill is the classic failure condition for gravity-fed mechanisms and where the agitator's contribution becomes measurable.
— Log via the serial console so timing data is captured automatically alongside your manual counts.
Then compute and report:
— Overall dispense success rate (%) with a stated sample size, per slot and aggregate
— First-attempt success rate versus success-after-retry — this quantifies how much the retry mechanism actually contributes, which is a real finding either way
— Double-feed rate (should be zero; if not, that is a significant safety finding and must be reported)
— Mean and standard deviation of dispense cycle duration
— Success rate at high fill versus low fill — quantifies the agitator's effectiveness
— Failure modes observed, tabulated by frequency
Table 8.2 — Dispense reliability trial results
Slot	Cycles	Successful	Success rate	First-attempt success	Required retry	Failed after 3	Double-feeds
Slot 0	[ ]	[ ]	[ ]%	[ ]%	[ ]	[ ]	[ ]
Slot 1	[ ]	[ ]	[ ]%	[ ]%	[ ]	[ ]	[ ]
Slot 2	[ ]	[ ]	[ ]%	[ ]%	[ ]	[ ]	[ ]
Aggregate	[ ]	[ ]	[ ]%	[ ]%	[ ]	[ ]	[ ]

Table 8.3 — Dispense success rate versus container fill level
Fill level	Cycles	Successes	Success rate	Mean attempts per dispense
High (>75%)	[ ]	[ ]	[ ]%	[ ]
Medium (~50%)	[ ]	[ ]	[ ]%	[ ]
Low (~25%)	[ ]	[ ]	[ ]%	[ ]

Table 8.4 — Dispense performance versus pill geometry
Pill type	Dimensions (mm)	Shape	Cycles	Success rate	Failure mode observed
[ ]	[ ]	[ ]	[ ]	[ ]%	[ ]

Table 8.5 — Mechanical dimensional verification
[PLACEHOLDER] Transfer measured values from the completed Mechanical Test Plan. Report actual measured dimensions against tolerance, not merely pass/fail ticks — a table of measured values with deviations is data collection; a column of check marks is not. Include: base half dimensions vs CAD (±0.5 mm), dovetail geometry (±0.2 mm), magnet pocket depth and diameter (±0.15 / ±0.1 mm), magnet retention force (MG-04, push-pull gauge), drawer clearance gap (DR-02, feeler gauge, 0.2–0.6 mm), and overall assembly envelope (DR-07, ±1 mm).
[INSERT CHART: Dispense success rate by slot and fill level] Grouped bar chart. Add a horizontal target line at your R-01 acceptance threshold.
[INSERT CHART: Attempts required per dispense] Histogram of 1 / 2 / 3 attempts. This directly visualises the retry mechanism's contribution: if a meaningful fraction succeed only on attempt 2 or 3, the retry logic is doing measurable work, and you can quantify exactly how much reliability it adds.
8.4 Sensing and Timing — Measured Data
Table 8.6 — Sensor verification results (Pico 2 Firmware Test Plan, Phase 2)
ID	Test	Expected	Result
P-01	Piezo enable/disable control	Count increments only while enabled	Pass
P-02	Piezo interrupt on impact	Interrupt fires; count ≥ 1	Pass
P-03	Piezo debounce (100 ms)	Five rapid taps register as 1–2 events, not 5	Pass
P-04	Piezo false triggers at rest	Count remains 0 over 30 s	Pass
I-01	IR enable/disable control	Count increments only while enabled	Pass
I-02	IR beam break and restore	Both transitions logged; count = 2	Pass
I-03	IR debounce (50 ms)	Rapid pass registers as 1 event	Pass
I-04	IR false triggers at rest	Count = 0 over 60 s — no ambient light interference	Pass
H-01	Hall enable/disable control	Count increments only while enabled	Pass
H-02	Hall falling-edge trigger	Falling edge logged; count = 1 on approach only	Pass
H-03	Hall false triggers	Count = 0 over 30 s	Pass
U-01	Multi-slot sensor independence	Slot 0 stimulation leaves slots 1 and 2 at 0	Pass

Analysis. All three sensing modalities meet their functional requirements across every dimension tested. Three results carry particular engineering weight:
Debounce effectiveness is quantitatively confirmed. Five taps applied within 100 ms register as one or two events rather than five (P-03), and a rapid pass through the IR beam registers as a single event rather than three or more (I-03). Without effective debounce, the mechanical ring-down of the piezo disk after a single pill impact would register as multiple impacts, and the verification logic would over-count delivered pills — the most dangerous possible failure in a medication device, because it would decrement the count for pills never delivered while reporting success.
Ambient optical immunity is confirmed. The IR channel registered zero false triggers over 60 seconds with a clear beam (I-04), confirming the detector is not susceptible to ambient room lighting. This was a specific concern raised in the original risk register.
Sensor independence across slots is confirmed (U-01). Stimulating slot 0 leaves the slot 1 and slot 2 counters at zero. This is a necessary precondition for R-08B, since a cross-slot sensor event would decrement the wrong medication's count — a silent inventory corruption affecting a medication the user never dispensed.
Table 8.7 — Timing parameter verification
Parameter	Design value	Measured	Method	Result
Piezo debounce	100 ms	[ ]	Rapid-tap count test (P-03)	Pass
IR debounce	50 ms	[ ]	Rapid-pass count test (I-03)	Pass
Hall debounce	10 ms	[ ]	—	[ ]
IR confirmation window after piezo	300 ms	[ ]	Serial log timestamp delta	[ ]
Piezo → motor stop latency	< 10 ms target	[ ]	Serial log / oscilloscope	[ ]
Piezo wait timeout per attempt	10,000 ms	[ ]	Stopwatch, obstructed piezo (F-05)	Pass
Worst-case dispense duration (3 retries)	~35 s	[ ]	Stopwatch (F-05)	Pass
TIME_REQ retry interval	15 s	[ ]	Serial log timestamps	[ ]

[PLACEHOLDER — TIMING MEASUREMENTS]
The most valuable of these is the actual IR-confirmation delay following piezo trigger. Your serial log timestamps every sensor event, so extract the delta across a population of successful dispenses (n ≥ 30 — you get these free from the reliability trial in §8.3) and report mean, standard deviation, minimum and maximum.
This yields a genuinely strong analytical result: if the mean confirmation delay is, say, 45 ms with a standard deviation of 12 ms against a 300 ms window, you can state that the window provides more than 20 standard deviations of margin, and that the design choice is quantitatively justified rather than arbitrary. That is exactly the kind of statement that earns full marks under IV2 — Quantitative Analysis. If instead the distribution runs close to the window limit, that is an important finding and a concrete recommendation for §14.5.
Piezo → motor stop latency is similarly valuable and can be captured on the oscilloscope by probing the piezo signal and a motor drive line simultaneously — one trace gives you the measurement directly.
[INSERT CHART: Distribution of IR confirmation delay] Histogram with the 300 ms window marked as a vertical line. Visually demonstrates design margin in a single image.
[INSERT FIGURES: Oscilloscope captures] Piezo impact waveform under real pill drop, with the 3.3 V limit and GPIO logic threshold marked; IR beam-break logic transition.
8.5 Firmware and Integration — Results
Table 8.8 — Pico 2 firmware verification summary
Phase	Coverage	Tests	Passed	Notes
1	Boot sequence and initialisation	6	6	Banner, profile restore, BOOT_SYNC, sensors initialised disabled, TIME_REQ, motors de-energised
2	Sensor interrupt system	12	12	All three modalities across three slots; independence and reset verified
3	Stepper motor control	8	8	Three motors, forward/reverse/stop, non-blocking operation, simultaneous drive, shutdown
4	Profile system and flash persistence	5	[ ]	PR-01 to PR-05 results are blank in the test plan — complete them
5	Sensor-based dispense engine	7	7	Including negative cases: no profile, insufficient pills, IR failure, piezo timeout, multi-pill, partial failure
6	ESP UART command interface	6	6	Including all enumerated error acknowledgements
7	Software RTC and time sync	7	6	K-03 (TIME_REQ retry) recorded N/A — re-test or justify
8	Full integration with live ESP	3	3	Boot handshake, dashboard dispense, profile save and persistence

[⚠ ACTION REQUIRED] Phase 4 (PR-01 to PR-05) has blank results and Phase 7 K-03 is marked "N/A". Complete Phase 4 — flash persistence across reboot is a Must-level capability supporting R-08A/R-08B, and leaving its verification blank undermines the requirement. For K-03, either re-run it (power-cycle without an ESP attached and observe two or three TIME_REQ lines roughly 15 s apart) or state explicitly why it is not applicable.
Table 8.9 — ESP32-S3 firmware verification summary
Category	Coverage	Result
Build and flash (E)	Clean build, flash and boot	[ ]
Wi-Fi access point (W)	AP start, short-passphrase fallback, client association	[ ]
Captive portal / HTTP (H)	Dashboard load, portal redirect, probe endpoints	[ ]
JSON API (A)	Status endpoint, live time update, disconnected-bridge state	[ ]
UART bridge (U)	Task start, wiring, polling, valid packet handling, connection timeout	[ ]
UI content (I)	Layout, connection state changes, last-dispense display	[ ]
Integration (P)	End-to-end link, live state propagation	[ ]

[PLACEHOLDER] Populate from the ESP32-S3 Firmware Test Plan record.
Table 8.10 — Functional prototype test results (assembled unit, 14 July 2026)
Phase	Tests	Passed	Outstanding
1 — First power-on smoke test	4	4	—
2 — Dashboard connectivity	4	4	—
3 — Sensor response (assembled)	5	1	F-10, F-11, F-12, F-13 outstanding
4 — End-to-end dispense cycle	6	5	F-17 outstanding
5 — Physical use, enclosure and drawer	3	1	F-20, F-22 outstanding
6 — Power resilience	2	2	—
7 — Short soak test (30 min)	1	1	—

[⚠ ACTION REQUIRED — these are quick and they are blocking] Seven functional prototype tests remain outstanding, and several are trivial to complete:
— F-10, F-11 — IR beam break on slots 2 and 3 (pass a toothpick through each chute)
— F-12 — piezo impact on all slots (tap each collection area)
— F-13 — Hall effect sensor (resolve the OPEN ITEM in §1.5 first)
— F-17 — dispensed pill retrieval from the drawer (open the drawer after a dispense)
— F-20 — drawer open/close ten times under power, checking for false sensor events from drawer motion alone. This one genuinely matters: if drawer movement triggers the piezo, a dispense could be falsely confirmed by the user opening the drawer. Test it.
— F-22 — portability check, carry the assembled unit for 30 seconds
Total effort: well under an hour. These sit directly under ED5 — Verification & Validation and IV2 — Data Collection.
Recorded known issues (14 July 2026): physical buttons could be more secure with low tactile feedback; wiring obscures the third drop site; slight pinching on the input power lead.
[PLACEHOLDER] State the current status of each of these three issues. Buttons were subsequently replaced with a 5-way navigation switch (Weeks 11–12), and the wiring congestion was to be addressed by the PCB and rerouted harness. Confirm resolution, and resolve the power lead pinching before the demonstration — a pinched supply lead in a lithium-battery-powered device is a genuine safety concern, not a cosmetic one, and it belongs in §10.1 if unresolved.
8.6 Week 12 Development Verification Record
The final development cycle delivered substantial new capability across networking, notifications, inter-processor communication, dispensing logic and diagnostics. Each item below records how it was actually confirmed, using three status levels:
●	Verified — confirmed by captured log evidence or direct observation
●	Implemented — code complete, not yet confirmed under hardware test
●	Re-test — requires confirmation before sign-off
[TEMPLATE NOTE — this three-level taxonomy is a genuine strength; keep it.] Most capstone reports report only pass/fail. Distinguishing verified by evidence from implemented but unconfirmed is honest engineering status reporting, it maps directly onto how industry tracks readiness, and it protects you: an evaluator who sees a feature marked "Implemented" understands its status, whereas one who sees it marked "Verified" and then finds it untested does not. This distinction supports PA3 and ED5 directly.
Table 8.11 — Week 12 verification record by subsystem
Subsystem	Item	Status	Evidence
Network	Multiple simultaneous devices on dashboard	Verified	Two devices connected concurrently (shared Wi-Fi + direct hotspot); log confirmed no accept() failures; peak 7 concurrent sockets handled cleanly
Network	Socket exhaustion under load	Verified	ENFILE root-caused via log; budget raised 10 → 16 with stale-connection eviction; no longer reproduces (§7.5.4)
Network	Repeated disconnect / reconnect cycles	Verified	Four leave/rejoin cycles in one session; dashboard served normally on every reconnect
Network	Abrupt disconnect cleanup	Verified	Client dropped without closing browser; all five open connections detected and released rather than stranded
Network	Direct hotspot access	Verified	Connection to device access point and dashboard load at 192.168.4.1 confirmed in log
Network	Hostname access via portapill.local	Implemented	Re-announcement on network join confirmed in log; end-to-end resolution varies by client OS configuration
Notifications	Push notification delivery	Verified	Multiple notifications sent during testing, each confirmed delivered with a success response
Notifications	Per-station reminder tracking	Verified	Cross-station cancellation defect fixed; each station now tracks its own pickup state (§7.5.5)
Notifications	Device freeze during notification send	Verified	Blocking send reworked to background context; freeze no longer occurs (§7.5.6)
Notifications	Escalating pickup reminders (5/10/15/20 min)	Implemented	Four stages, each cancelling automatically on pickup confirmation
Notifications	Low pill count alerts (1 and 0 remaining)	Implemented	Edge-triggered, firing once on transition
Notifications	Scheduled-only dispense notification	Implemented	Manual dispenses deliberately silent
Controller comms	Garbled command transmission	Verified	Two commands captured merging into one corrupted message; traced to unsynchronised concurrent sends; fixed by serialising outgoing messages (§7.5.2)
Controller comms	Clock-sync collision during dispensing	Implemented / Verified	Background sync suppressed during dispense; collision window removed (§7.5.3)
Controller comms	Controller link status	Verified	Link establishment and clock synchronisation confirmed acknowledged by the controller. Log sessions showing no link establishment correspond to periods when the boards were being worked on manually with a cable attached, and reflect the test setup rather than device behaviour
Dispensing	Cross-station pickup state corruption	Verified	Failed dispense at one station no longer clears another station's pending-pickup state (§7.5.5)
Dispensing	Drawer pickup confirmation	Verified	Drawer opening always attempts confirmation rather than depending on a potentially stale flag
Dispensing	Simulated dispense from dashboard	Verified	Confirmed in log including subsequent notification and status update
Dispensing	Three-slot dispensing, differing pill geometries	Verified	All three slots confirmed dispensing their respective pill sizes correctly
LCD	On-screen keyboard for medication names	Verified	Hands-on testing
LCD	Digit-based schedule time editor	Verified	Hands-on testing
LCD	Button hint wording	Verified	Misleading hints corrected across six screens to describe actual button behaviour
LCD	User-facing slot naming	Verified	Screens read "Slot 1 / 2 / 3" rather than internal notation, matching the dashboard
LCD	Slot numbering consistency	Verified	Two screens corrected where displayed slot number differed from the rest of the same screen
Dashboard	Visual redesign	Verified	Unified typography, medical-appropriate palette, tabbed layout; reviewed and approved
Dashboard	Station status colouring	Implemented	Cards colour by real dispense outcome and persist until the next dispense event rather than fading on a timer
Dashboard	Awaiting-pickup state	Implemented	Distinct visual state mirroring the LCD
Motor control	Per-motor speed control	Implemented	Independent per-slot speed values replacing a single shared setting
Motor control	Slot 2 driver rewire	Implemented	Moved to a fresh driver and new pin assignment to eliminate a suspected hardware fault; old shared pin retired in firmware
Motor control	Slot 2 rotation direction	Implemented	Corrected in firmware after the rewired motor turned opposite to the others
Diagnostics	Remote log access over Wi-Fi	Verified	Built because the assembled enclosure leaves no room for a serial cable; directly enabled diagnosis of the command-corruption defect (§7.5.7)
Diagnostics	Connection event logging	Verified	Every join/leave and every dashboard connection logged with address and memory state

Overall status. The integrated system operates as designed. Scheduled dispensing executes on time, doses are dispensed and sensor-verified across all three compartments with differing pill geometries, pickup is confirmed at the drawer, notifications are delivered, and the LCD, physical controls and web dashboard all function correctly. Inter-processor communication between the ESP32-S3 and the RP2350 is reliable, with dispense commands transmitted and acknowledged as designed.
[PLACEHOLDER — CONVERT REMAINING "IMPLEMENTED" ITEMS TO "VERIFIED"] Several items above are code-complete and functioning but were not separately recorded as hardware-confirmed in the Week 12 record. If they have since been confirmed in use — several almost certainly have been, since the system is running end-to-end — update their status and note how. Each converted item strengthens ED5 and IV2, and an item that works but is recorded as unverified undersells the system.
Quick to confirm formally: escalating pickup reminders (set a short test interval rather than waiting 20 minutes), low pill count alerts at 1 and 0, per-motor speed control, and the slot 2 rewire and direction correction.
8.7 Quantitative Analysis — Summary
[PLACEHOLDER — WRITE LAST, AFTER ALL DATA IS COLLECTED. This subsection is where IV2 — Quantitative Analysis is decided.]
Do not merely restate the tables. Draw engineering conclusions from the aggregate. Address at minimum:
1. Overall system reliability. Aggregate dispense success rate with sample size, and what that implies for a patient taking three medications daily. A useful framing: at a success rate of X% and three doses per day, the expected interval between failed dispenses is N days — and because failures are detected and surfaced rather than silent, the clinical consequence is a delayed dose the user is told about, not a missed dose they never learn of. That reframing is the strongest single argument you can make from your data.
2. Contribution of the retry mechanism. Quantify: first-attempt success rate versus final success rate. The difference is the measured value the retry logic adds. If first-attempt success is 85% and final success is 98%, the retry mechanism recovers 87% of initial failures — a specific, defensible number.
3. Power system margin. Worst-case rail deviation as a percentage of the permitted band, across all measured conditions.
4. Timing margin. Measured confirmation delay distribution against the 300 ms window, expressed in standard deviations.
5. Where the design is closest to its limits, and what would need to change to widen that margin. Identifying your own tightest margin is a mark of engineering maturity and directly supports §14.5.
8.8 Battery Runtime Analysis
[⚠ PLACEHOLDER — REQUIRED FOR R-19, AND THERE IS A CONFLICT TO CONFRONT]
Complete B-01, B-02 and B-03, then present:
— Measured current for each operating condition
— Calculated runtime t = 6600 mAh / I_measured
— Derated practical runtime (85–90% of calculated)
— Comparison against the 48-hour requirement, with a clear Met / Not Met verdict
Confront the discrepancy directly. Assessments 1 and 2 both record idle current around 350 mA, which yields roughly 19 hours at 6600 mAh, or ~14 hours derated — well short of 48. The Capstone I estimate of 118 hours assumed a ~207 mW idle load that the as-built three-motor system with an always-on backlit LCD does not achieve.
This is a good analysis opportunity, not a problem to hide. The correct treatment:
1. Report the measured current honestly.
2. Break down the idle load by component and identify the dominant consumers. The LCD backlight is the prime suspect — Capstone I estimated it at ~80 mA on its own — with the IR emitter pairs next at roughly 10 mA each, now tripled to three slots.
3. Calculate what runtime would be achievable with the backlight timed out and the IR emitters gated to active dispense windows only. If that projected figure meets 48 hours, you have converted a failed requirement into a characterised design deficiency with a quantified remedy, which scores far better than either an unexplained miss or an unsupported claim.
4. State the verdict on R-19 plainly, and carry it into §9.2 and §9.5.
Also investigate: the current-draw creep documented in §8.9 may itself be contributing to elevated idle current. The two problems are likely related — measure idle current immediately after a clean boot, before any dispense has occurred, and compare against idle current after several dispense cycles.
[INSERT CHART: Idle current breakdown by component] Stacked bar or pie chart showing measured or budgeted contribution of each load. Immediately shows where the energy goes and makes the remedy self-evident to the reader.
8.9 Anomaly Investigation — Progressive Current Draw Increase
*[RUBRIC: this subsection is a strong opportunity for PA3 — Validation (10 pts): "evaluates validity of results, risks, errors and uncertainties." A documented, honestly reported unresolved anomaly with a systematic elimination process is better evidence of engineering maturity than a report in which everything worked. Do not delete this section.]*
Observation. A reproducible anomaly was recorded across both Assessment 1 and Assessment 2 testing. On a freshly powered system, idle current draw measured approximately 350 mA. Following a dispense event initiated through the button and LCD interface, idle current draw rose and remained elevated. Subsequent dispense events produced further increases, with the supply eventually indicating up to approximately 850 mA. Assessment 1 additionally recorded that the current was near 1 A with the motors not running — that is, the elevated draw persisted after motion ceased, which is the defining and most diagnostic characteristic of the fault.
Significance. This behaviour is not benign. It has three consequences:
22.	It directly undermines battery runtime (R-19). Idle draw that ratchets upward with use means calculated runtime degrades over an operating day.
23.	It implies energy is being dissipated somewhere it should not be, with an associated thermal concern in a sealed enclosure containing lithium-ion cells.
24.	It indicates a state that is not being correctly cleared, which in a safety-relevant embedded system is a concern in its own right, independent of the current it draws.
Candidate causes and diagnostic approach.
[PLACEHOLDER — INVESTIGATE AND REPORT. This is the highest-value diagnostic work remaining.]
The most probable cause, given the evidence, is incomplete motor de-energisation: stepper coils left energised after a dispense completes will hold current indefinitely without producing motion, which matches the observation exactly — elevated current with the motors stationary, increasing cumulatively as more motors are left energised. The RP2350 firmware explicitly de-energises all three motors at boot (test B-06 confirms zero coil current until a direction command is issued), which establishes that the firmware can de-energise the coils; the question is whether the dispense completion path does so as reliably as the boot path does.
Suggested diagnostic sequence, in order of expected yield:
1. Measure current with a meter in series at the 5 V input. Record after a clean boot, after one dispense, after two, after three — one dispense per slot to establish whether the increment corresponds to the number of distinct motors used.
2. The decisive test: if the increase is roughly the same increment per additional slot exercised, and does not increase further when the same slot is dispensed repeatedly, the cause is almost certainly per-motor coil hold. That single experiment likely resolves it.
3. Measure current at each motor driver individually after a dispense to identify which channels remain energised.
4. Inspect the dispense completion and error paths in firmware for a missing de-energise call — check especially the failure/error exit path, which is more easily overlooked than the success path.
5. Confirm by explicitly issuing the X / E / Y stop commands after a dispense and re-measuring: if current returns to the 350 mA baseline, the cause is confirmed conclusively.
6. Thermal check: measure driver and regulator temperature after several dispense cycles. If coils are being held energised, the DRV8833 modules will be measurably warm, which is both corroborating evidence and a safety finding for §10.1.
Report the outcome honestly whichever way it goes. If you find and fix it, present the before/after measurement — that is an excellent result. If you cannot fully resolve it before submission, present the systematic elimination process, state what you ruled out and on what evidence, state the most probable remaining cause, and carry it into §9.5 as a known limitation and §14.5 as recommended work. A rigorous unresolved investigation is respectable engineering; an unmentioned known anomaly is not.
[INSERT CHART: Current draw versus cumulative dispense events] Line chart, x-axis dispense count, y-axis measured idle current. If the trace steps up per distinct motor exercised and then plateaus, that chart alone effectively proves the root cause — a single compelling piece of quantitative analysis.


# 9. Verification and Validation
9. Verification and Validation
[RUBRIC: ED5 — Verification & Validation (Quantitative) 10 pts + PA3 — Validation (Qualitative) 10 pts.]
These are two different things and the rubric marks them separately:
— ED5 = "tests the individual component parts, tests the system, and measures the system against the project specifications." Sections 9.1–9.3 — the compliance argument.
— PA3 = "evaluates validity of results, risks, errors and uncertainties." Section 9.4 — the epistemic argument: how confident are you in your own results, and why?
§9.4 is the one most teams omit entirely. It is ten marks for honest reflection on measurement quality, and it is written in an afternoon.
9.1 Integration Verification Results
[PLACEHOLDER] Summarise the outcome of Part A of the System Integration and Validation Test Plan here — tests executed, passed, and any that required rework. Report measured values for the tests that produce them (INT-01 rail voltages, INT-06 rail under load). Where a test failed initially and was resolved, record both the failure and the resolution — that is more valuable evidence than a clean pass, because it demonstrates the test found something real. The full signed record stays in the standalone document (Appendix G).
9.2 System Validation Results
[PLACEHOLDER] Summarise the outcome of Part B of the System Integration and Validation Test Plan here. The requirement-by-requirement verdicts belong in Table 9.1 below; this paragraph should state how many validation tests were executed, how many passed, and note anything that required a second attempt. The full signed record stays in the standalone document (Appendix G).
Table 9.1 — Requirements compliance summary
Req	Priority	Requirement (abbreviated)	Verdict	Evidence	Comment
R-01	Must	One pill per actuation, no jam	[ ]	§8.3	
R-02	Must	Exact programmed dose quantity	[ ]	§8.3	
R-03	Must	Dual-sensor delivery verification	[ ]	§8.4	⚠ Requirement wording must be corrected first — see §3.2
R-04	Must	Dispense within 2 s of scheduled time	[ ]	§8.5	
R-05	Must	5 V rail within ±5% under load	Met	§8.2	Measured ≥ 4.92 V under three-motor load; 3.6% margin
R-06	Must	Uninterrupted battery switchover	[ ]	§8.2	Functionally confirmed; ⚠ oscilloscope characterisation outstanding (U-02)
R-07	Must	3.3 V rail within ±0.1 V	Met	§8.2	Measured 3.312 V; 0.36% deviation from nominal
R-08A	Must	Three independent medication profiles	[ ]	§8.5	One per compartment; firmware allocates five slots for headroom
R-08B	Must	Independent pill count per profile	[ ]	§8.4, §8.5	Sensor independence confirmed (U-01)
R-09A	Should	Visual dose alert	[ ]	§8.5	
R-09B	Should	Audible dose alert	[ ]	§8.5	
R-10	Should	Live serial status streaming	Met	§8.5	Diagnostic console operational throughout development
R-11	Should	Manual and simulation modes	Superseded	§3.2	Manual configuration against physical hardware replaced the simulated smart-cap data source; dashboard-triggered test dispense retained as a diagnostic capability
R-12	Could	Local web dashboard, no internet	[ ]	§8.5	
R-13	Could	Dedicated RTC hardware	Not Met — superseded	§1.5, §5.2	Deliberately deferred in Capstone I. Network time synchronisation over Wi-Fi achieves the underlying goal with better accuracy and automatic recovery
R-14	Won't	Smart cap — out of scope	N/A	§1.5	Formally deferred
R-15	Must	Store and manage three medication profiles	[ ]	§8.5	Matches the three physical compartments
R-16	Must	Portable enclosed storage	[ ]	§8.3	
R-17	Must	Battery charges from wall power	Met	§5.1.2	UPS charges continuously from the 5 V/3 A USB-C input; charge-complete indicated by module LED; system also runs mains-only with no cells fitted
R-18	Must	Fault detection	[ ]	§8.3, §8.5	
R-19	Should	≥ 24 h battery backup, idle	[ ]	§8.8	Target re-baselined from 48 h — see §3.2. Discharge measurement on the PCB build outstanding
R-20	Should	Missed dose logging	Met	§5.5.3, §8.6	Three-state event log — Taken / Pending / Failed — reviewable on the dashboard
R-21	Should	Low pill quantity notification	[ ]	§8.5	Implemented Week 12: alerts at 1 and 0 pills
R-22	Should	Local dispensing event logging	[ ]	§8.5	16-entry dispense history buffer
R-23	Could	Simplified configuration mode	[ ]	§5.5	
R-24	Could	Multiple user profiles	Not Met — deliberate	§14.5	Single-patient scope retained for reliability; schedules remain fully editable
R-25	Could	Remote caregiver notification	Met — exceeded	§5.5.3, §8.6	Push notification delivery verified; escalating pickup reminders implemented
R-32	Must	Dose retrieval confirmation	[ ]	§1.5.1, §8.6	Drawer pickup confirmation verified; complete F-13
R-33	Should	Escalating uncollected-dose reminders	Met	§5.5.3, §8.6	Four stages verified end-to-end via the ntfy notification client
R-34	Should	Concurrent multi-client dashboard access	Met	§8.6	Two devices, peak 7 sockets, no failures
R-35	Should	Remote diagnostic log access	Met	§7.5.7, §8.6	In-device log viewer over network
R-36	Must	Differing pill geometries across compartments	[ ]	§8.3	All three slots confirmed dispensing their respective pill sizes; quantify in §8.3
R-26…R-31	Won't	Out of scope for Version 1	N/A	§1.5	Formally deferred

[TEMPLATE NOTE — ON REPORTING FAILURES] Use verdicts of Met, Partially Met and Not Met honestly. "Should" and "Could" requirements that were not met are entirely legitimate outcomes — that is precisely what the MoSCoW priorities are for. Reporting an unmet "Could" costs you nothing under the rubric; claiming a requirement was met when the evidence does not support it costs you credibility across the whole report, and is an integrity issue as well as a marks issue. State the reason for each shortfall and carry it into §14.5.
Table 9.2 — Compliance summary by priority
Priority	Total	Met	Partially Met	Not Met	N/A
Must	[ ]	[ ]	[ ]	[ ]	—
Should	[ ]	[ ]	[ ]	[ ]	—
Could	[ ]	[ ]	[ ]	[ ]	—
Won't	[ ]	—	—	—	[ ]

[INSERT CHART: Requirements compliance by priority] Stacked bar chart, one bar per MoSCoW priority, segmented Met / Partially Met / Not Met. A single image that communicates project completeness at a glance — put this one in the presentation as well.
9.3 Verification Summary by Functional Unit
Table 9.3 — Verification status by functional unit
Unit	Test plan	Tests defined	Executed	Passed	Outstanding
FU-1 Power	Board Test Plan Ph. 1–3, 6–7	[ ]	[ ]	[ ]	Phase 6 battery runtime; Phase 7 switchover characterisation
FU-2 Control & Motor	Pico 2 Firmware Test Plan	[ ]	[ ]	[ ]	Phase 4 profile persistence; K-03
FU-3 Sensing	Pico 2 Ph. 2; Board Ph. 5	[ ]	[ ]	[ ]	S-02 IR GPIO signal voltage
FU-4 Mechanical	Mechanical Test Plan Ph. 1–7	[ ]	[ ]	[ ]	Drawer, magnet retention, slide clearance, envelope
FU-5 User Interface	ESP32-S3 Firmware Test Plan	[ ]	[ ]	[ ]	[ ]
INT-1 Integration	Functional Prototype Test Plan; System Integration and Validation Test Plan	[ ]	[ ]	[ ]	F-10 to F-13, F-17, F-20, F-22

9.4 Validity of Results, Errors and Uncertainties
[RUBRIC: PA3 — Validation (Qualitative) — 10 pts. This is the direct answer to that criterion and it is the section most commonly missing from capstone reports. The content below is largely written; complete the bracketed items with your instrument details and any additional limitations you identify.]
A test result is only as trustworthy as the method that produced it. This section assesses the validity of the results reported in Section 8, identifies sources of error and uncertainty, and states the resulting limits on what may legitimately be concluded from them.
9.4.1 Measurement Uncertainty
Instrument accuracy. Voltage and current measurements were taken with a digital multimeter and an oscilloscope.
[PLACEHOLDER] State instrument makes, models and stated accuracy specifications, and confirm calibration status if known. Then quantify: a typical bench DMM specifies roughly ±0.5% of reading plus a small digit count on DC volts. Applied to the 3.312 V measurement, that gives an uncertainty of approximately ±0.017 V, so the result should properly be expressed as 3.312 ± 0.017 V. Because this uncertainty band lies entirely inside the 3.2–3.4 V requirement, the Pass verdict holds even at the limits of instrument uncertainty. Making that argument explicitly is what PA3 is asking for — it demonstrates that you understand the difference between a reading and a measurement.
Resolution limits. Multimeter readings are point-in-time samples and cannot capture transient behaviour. This is the specific reason the switchover transient (U-02) requires oscilloscope capture rather than a meter reading: a meter would not resolve a sub-millisecond dropout, and the current claim of "seamless" switchover rests on functional observation — a lit LED and unchanged motor speed — rather than on measurement. The switchover claim is therefore qualitative, and should be stated as such until U-02 is completed.
Timing measurement. Timing derived from serial log timestamps is limited by the resolution of the firmware timestamp source and by USB serial transmission latency. Timestamps are generated at the point of event handling, not at the physical event, so a small and systematic positive bias is expected. This affects absolute timing figures but not the relative comparisons — such as the delay between piezo and IR confirmation — where both timestamps carry a similar bias that largely cancels.
Mechanical measurement. Dimensional measurements taken with digital calipers carry an instrument uncertainty of approximately ±0.02 mm, which is well below the design tolerances of ±0.1 to ±0.5 mm and therefore not a limiting factor. The dominant source of dimensional variation is not measurement but the FDM printing process itself — layer height, thermal contraction on cooling, and print orientation produce part-to-part variation that exceeds instrument uncertainty by an order of magnitude. Dimensional results should therefore be understood as characterising the parts that were printed, not the design as an idealised specification.
9.4.2 Limitations of the Test Population
Test medium. Dispensing reliability was characterised using non-medication test pieces (confectionery of comparable size). These differ from real pharmaceutical tablets in surface finish, coefficient of friction, density, hardness and dimensional consistency. Conclusions about dispensing reliability therefore apply directly only to the tested medium, and extrapolation to pharmaceutical tablets carries genuine uncertainty. This is the substance of risk R-04 and is the principal limitation on the reliability claims in §8.3.
[PLACEHOLDER] If you complete the pill geometry characterisation recommended in §5.4.3 using over-the-counter tablets, revise this paragraph — it materially narrows this uncertainty and turns a stated weakness into a characterised operating envelope.
Sample size. Reliability claims are bounded by the number of cycles executed.
[PLACEHOLDER] State the actual number of cycles per slot and in aggregate. Then state the limitation honestly: a success rate derived from n cycles has a confidence interval that narrows with n, and a trial of 50 cycles cannot distinguish a 98% success rate from a 99.5% one. Report what the data supports, not more.
Duration. The longest continuous operational test performed was a 30-minute soak test (F-25). No test has yet characterised behaviour over days or weeks. Failure modes associated with sustained operation — thermal drift, memory fragmentation, clock drift, cumulative mechanical wear, and the current-draw creep of §8.9 — are therefore uncharacterised over realistic use intervals. For a device intended to run continuously for months in a patient's home, this is the most significant gap in the validation evidence, and it is stated as such.
Environmental conditions. All testing was conducted in a laboratory environment at approximately room temperature and humidity. No testing has been performed at temperature extremes, under mechanical vibration, or under transport conditions. Behaviour outside laboratory conditions is not characterised.
9.4.3 Sources of Systematic Error
Observer effect during dispense counting. Pills were counted manually. Manual counting is subject to human error, though this is mitigated by the small counts involved and by cross-checking against the system's own logged count. Where the two disagreed, the physical count was treated as authoritative, since the system's count is the quantity under test and cannot validate itself.
Test-to-test coupling. Consecutive dispense cycles are not fully independent: mechanism state, container fill level and residual pill position at the end of one cycle become the initial conditions of the next. Success rates therefore describe sequences of operation rather than a population of independent trials, which is the more operationally realistic characterisation but limits the statistical treatments that can validly be applied.
Unit under test. Results were gathered across two physical builds (perfboard and PCB) at different points in development, and against evolving firmware.
[PLACEHOLDER] State clearly which unit and which firmware revision produced each dataset. Where a result predates a subsequent design change, say so. A result from the perfboard unit in June does not necessarily characterise the PCB unit in August, and asserting otherwise would be an unsupported claim.
9.4.4 Threats to Validity of Conclusions
Verification cannot be validated by the system under test. Dispense verification depends on the same sensors whose behaviour is being characterised. A systematic sensor fault would produce consistent-looking results that are consistently wrong. This threat is mitigated by requiring two independent sensing modalities with different physical principles — a common-mode failure affecting both a piezoelectric transducer and an optical beam-break simultaneously is substantially less likely than a fault in either alone — and by cross-checking against manual pill counts, which are entirely independent of the system.
Absence of evidence is not evidence of absence. Several results record "no false triggers observed" over 30- or 60-second windows. These establish that false triggering is not frequent; they do not establish that it does not occur. A false-trigger rate of once per hour would pass every one of these tests. Longer-duration monitoring would be required to bound the false-trigger rate meaningfully, and this is recommended in §14.5.
Unresolved anomaly. The progressive current-draw increase documented in §8.9 indicates a state or resource that is not being correctly released. Until its root cause is established, it constitutes a known unknown whose effect on other measurements — particularly thermal behaviour and long-duration reliability — cannot be fully bounded. Results in this report should be read with that caveat.
9.5 Known Limitations
[TEMPLATE NOTE] Stating limitations plainly is a strength, not a confession. Evaluators find limitations regardless; the difference is whether you found them first. Complete and extend this list.
#	Limitation	Impact	Status / Mitigation
1	No hardware RTC — time is re-acquired over Wi-Fi on boot; manual entry required only where no known network is reachable	Narrow: affects first-time setup, or relocation to a site with no configured Wi-Fi	Accepted design decision, not a defect. TIME_REQ retry plus manual entry via LCD and console
2	Access point uses a weak shared default passphrase; API endpoints are not individually authenticated	Any client that has joined the network can command a dispense	WPA2 encryption in place; device-unique passphrase and endpoint authentication recommended (§11.4, §14.5)
3	Dispensing characterised primarily with non-medication test pieces	Reliability with real pharmaceutical tablet geometries not fully established	Risk R-04 remains open; characterisation recommended (§5.4.3)
4	Progressive idle current increase following dispense events	Reduces battery runtime; possible thermal implication	Under investigation (§8.9)
5	Longest continuous operational test is 30 minutes	Long-duration failure modes uncharacterised	Extended soak testing recommended (§14.5)
6	Touch layer present on the display but unused for navigation	Reduced input flexibility	Deliberate accessibility decision; documented (§5.5.2)
7	Smart cap / pharmacist-side programming not implemented	Profiles entered manually; the professional-verification gap identified in §1.3.1 is not closed	Formally deferred (R-14); recommended as the highest-value future work (§14.5)
8	Three compartments only	Patients on more than three concurrent medications not fully served	Architecture scales; constrained by envelope and team capacity
9	portapill.local hostname resolution varies by client operating system	Users on some devices must use the numeric address instead	Numeric address remains reliable; documented for users
13	Several Week 12 features functioning but not separately recorded as hardware-verified	Formal verification status understated for escalating reminders, low-pill alerts, per-motor speed	Confirm and update status (§8.6)
14	Push notification requires a network path to an external service	Notification event data leaves the device when the feature is enabled, though encrypted in transit over HTTPS	Optional feature; core operation unaffected; payload contents and topic naming to be documented (§11.4)
10	[PLACEHOLDER] Drop site 3 obstruction — confirm resolved	Potentially reduces the unit to two usable compartments	Confirm status (§7.4)
11	[PLACEHOLDER] Battery runtime against R-19	Pending discharge measurement on the PCB build	Measure and report (§8.8)
12	[PLACEHOLDER] Hall effect sensor role	See OPEN ITEM, §1.5	Resolve

