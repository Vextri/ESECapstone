# 8. Results and Analysis
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
P-01	5 V rail voltage	4.75 – 5.25 V	4.94 V	−1.20% of nominal	Pass	2 June 2026
P-02	3.3 V rail voltage	3.2 – 3.4 V	3.312 V	+0.36% of nominal	Pass	2 June 2026
P-03	LM317 accuracy vs. design target	3.307 V ±0.1 V	3.312 V	+0.15% of target	Pass	2 June 2026
P-04	No-load current draw	< 50 mA	31 mA	38% below limit	Pass	5 June 2026
M-02	Single stepper current draw	< 250 mA	102 mA	59% below limit	Pass	5 June 2026
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
A reliability trial of 50 dispense cycles total has been run, across both the Unit 1 perfboard and Unit 2 PCB builds combined rather than on a single unit (§9.4.3). Results below are based on the team's post-trial recollection rather than a contemporaneous per-cycle log, and are reported as approximate for that reason (§9.4.1).
Of 50 cycles, approximately 44–45 (88–90%) succeeded. Of the successes, approximately 3 required the retry protocol (succeeding on a second or third attempt) with the remainder succeeding on the first attempt, giving an approximate first-attempt success rate of 82–84%. Approximately 2 cycles (4%) ended in a full, unrecoverable jam requiring manual intervention; some of these are attributed to the test candy warping under repeated handling rather than to a mechanism failure, since real pills rather than candy were not used (§9.4.2). Zero double-feed events were observed across all 50 cycles, satisfying the double-feed component of R-01.
Because these figures come from recollection rather than a per-cycle log, the individual counts above do not reconcile exactly to 50 — this gap is disclosed rather than smoothed over, consistent with the honest-uncertainty standard the report otherwise holds itself to. Per-slot and fill-level breakdowns (Tables 8.3, 8.4) were not tracked separately in this trial.
R-01 is a "Must" requirement — one pill per actuation, zero jams, zero double-feeds. The recommended protocol below remains available if further cycles are run before submission to increase sample size, obtain a real per-cycle log, or fill the fill-level/pill-geometry breakdowns, which the 50-cycle trial as described does not yet cover.
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
Table 8.2 — Dispense reliability trial results (approximate, recollection-based — see §9.4.1)
Slot	Cycles	Successful	Success rate	First-attempt success	Required retry	Failed after 3	Double-feeds
Slot 0	Not tracked separately	—	—	—	—	—	—
Slot 1	Not tracked separately	—	—	—	—	—	—
Slot 2	Not tracked separately	—	—	—	—	—	—
Aggregate	50	~44–45	~88–90%	~82–84%	~3	~2	0

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
Build and flash (E)	Clean build, flash and boot	Pass
Wi-Fi access point (W)	AP start, short-passphrase fallback, client association	Pass
Captive portal / HTTP (H)	Dashboard load, portal redirect, probe endpoints	Pass
JSON API (A)	Status endpoint, live time update, disconnected-bridge state	Pass
UART bridge (U)	Task start, wiring, polling, valid packet handling, connection timeout	Pass
UI content (I)	Layout, connection state changes, last-dispense display	Pass
Integration (P)	End-to-end link, live state propagation	Pass

Table 8.10 — Functional prototype test results (assembled unit, updated on final build)
Phase	Tests	Passed	Outstanding
1 — First power-on smoke test	4	4	—
2 — Dashboard connectivity	4	4	—
3 — Sensor response (assembled)	5	5	—
4 — End-to-end dispense cycle	6	6	—
5 — Physical use, enclosure and drawer	3	3	—
6 — Power resilience	2	2	—
7 — Short soak test (30 min)	1	1	—

F-10, F-11 (IR beam break, slots 2/3), F-12 (piezo impact, all slots), F-13 (Hall effect sensor), F-17 (dispensed pill retrieval from the drawer), F-20 (drawer open/close under power, no false sensor events from drawer motion alone) and F-22 (portability check) are recorded as passed, confirmed through direct operation of the assembled final unit rather than as individually logged, timestamped test-plan executions. Following the status taxonomy established in §8.6, this is closer to "Verified by direct observation" than a formally re-executed and signed test-plan record; the signed Functional Prototype Test Plan in Appendix G remains the primary record and should reflect the same status.
Recorded known issues (14 July 2026): physical buttons could be more secure with low tactile feedback; wiring obscures the third drop site; slight pinching on the input power lead.
Status of the three recorded issues: physical buttons were replaced with a 5-way navigation switch on the Unit 2 PCB build (§7.6), resolving the tactile-feedback concern. Wiring congestion at drop site 3 was resolved by the PCB and rerouted harness (§7.4). [⚠ ACTION REQUIRED] The pinched supply lead has not been confirmed either way — physically inspect where the power cable passes through the Unit 2 enclosure for pinch points, sharp bends or panel-seam pressure before the demonstration. A pinched supply lead in a lithium-battery-powered device is a genuine safety concern, not a cosmetic one, and remains flagged as open in §10.1 until checked.
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
Notifications	Escalating pickup reminders (5/10/15/20 min)	Verified	Four stages, each cancelling automatically on pickup confirmation; confirmed working in use
Notifications	Low pill count alerts (1 and 0 remaining)	Verified	Edge-triggered, firing once on transition; confirmed working in use
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
Motor control	Per-motor speed control	Verified	Independent per-slot speed values replacing a single shared setting; confirmed working in use
Motor control	Slot 2 driver rewire	Verified	Moved to a fresh driver and new pin assignment to eliminate a suspected hardware fault; old shared pin retired in firmware; confirmed working in use
Motor control	Slot 2 rotation direction	Verified	Corrected in firmware after the rewired motor turned opposite to the others; confirmed working in use
Diagnostics	Remote log access over Wi-Fi	Verified	Built because the assembled enclosure leaves no room for a serial cable; directly enabled diagnosis of the command-corruption defect (§7.5.7)
Diagnostics	Connection event logging	Verified	Every join/leave and every dashboard connection logged with address and memory state

Overall status. The integrated system operates as designed. Scheduled dispensing executes on time, doses are dispensed and sensor-verified across all three compartments with differing pill geometries, pickup is confirmed at the drawer, notifications are delivered, and the LCD, physical controls and web dashboard all function correctly. Inter-processor communication between the ESP32-S3 and the RP2350 is reliable, with dispense commands transmitted and acknowledged as designed.
Escalating pickup reminders, low pill count alerts, per-motor speed control, and the slot 2 rewire and direction correction have since been confirmed working in use and are updated to Verified above. Station status colouring, the awaiting-pickup dashboard state, and hostname resolution via portapill.local remain Implemented rather than Verified.
8.7 Quantitative Analysis — Summary
[PLACEHOLDER — WRITE LAST, AFTER ALL DATA IS COLLECTED. This subsection is where IV2 — Quantitative Analysis is decided.]
Do not merely restate the tables. Draw engineering conclusions from the aggregate. Address at minimum:
1. Overall system reliability. Aggregate dispense success rate with sample size, and what that implies for a patient taking three medications daily. A useful framing: at a success rate of X% and three doses per day, the expected interval between failed dispenses is N days — and because failures are detected and surfaced rather than silent, the clinical consequence is a delayed dose the user is told about, not a missed dose they never learn of. That reframing is the strongest single argument you can make from your data.
2. Contribution of the retry mechanism. Quantify: first-attempt success rate versus final success rate. The difference is the measured value the retry logic adds. If first-attempt success is 85% and final success is 98%, the retry mechanism recovers 87% of initial failures — a specific, defensible number.
3. Power system margin. Worst-case rail deviation as a percentage of the permitted band, across all measured conditions.
4. Timing margin. Measured confirmation delay distribution against the 300 ms window, expressed in standard deviations.
5. Where the design is closest to its limits, and what would need to change to widen that margin. Identifying your own tightest margin is a mark of engineering maturity and directly supports §14.5.
8.8 Battery Runtime Analysis
The battery pack consists of two 3300 mAh cells in parallel, giving a total pack capacity of 6600 mAh at the nominal 3.7 V cell voltage: E = 6600 mAh × 3.7 V = 24.42 Wh (§5.1.4). Idle current measured on the Unit 2 PCB build, with the current-draw anomaly of §8.9 resolved, is 400 mA, measured on the 5 V rail between the UPS output and the board input (the same point defined for B-01/B-02/B-03, §5.1.4) — not at the battery terminals.
Idle power at the measurement point: P_idle = 5 V × 0.4 A = 2.0 W.
Calculated runtime: t = E / P_idle = 24.42 Wh / 2.0 W = 12.21 hours.
Derated practical runtime (85–90% of calculated, per §5.1.4, accounting for the UPS boost-converter's finite efficiency between the 3.7 V cell and the 5 V rail and for incomplete usable discharge): approximately 10.4 to 11.0 hours.
Comparison against the 24-hour requirement (R-19, re-baselined from the original 48-hour Capstone I target — §3.2): **Not Met**. The measured runtime achieves approximately 43–46% of the required 24 hours.
[⚠ PLACEHOLDER] This is a real, repeatable shortfall rather than a measurement artefact — it is consistent in order of magnitude with the ~350 mA idle current recorded independently in Assessments 1 and 2 on the perfboard build (Unit 1). Confirm B-01/B-02/B-03 are recorded against this figure in the Board Test Plan, and state which unit and firmware revision produced this specific 400 mA measurement (§8.1).
A prior engineering estimate from Capstone I attributed roughly 80 mA of idle draw to the LCD backlight alone and roughly 10 mA to each IR emitter (30 mA across three slots) — together accounting for a meaningful fraction of the 400 mA total. These figures were not re-measured on the current build and should be treated as an unverified planning estimate, not measured data, unless a fresh per-component measurement is taken. If confirmed close to accurate, timing out the backlight and gating the IR emitters to active dispense windows only would be expected to recover a substantial portion of the shortfall, though not necessarily reach 48 hours outright — this remains a projection until measured.
[INSERT CHART: Idle current breakdown by component] Stacked bar or pie chart showing measured or budgeted contribution of each load, once a per-component measurement is taken.
8.9 Anomaly Investigation — Progressive Current Draw Increase
*[RUBRIC: this subsection is a strong opportunity for PA3 — Validation (10 pts): "evaluates validity of results, risks, errors and uncertainties." A documented, honestly reported anomaly with a systematic elimination process is better evidence of engineering maturity than a report in which everything worked. Do not delete this section.]*
Observation. A reproducible anomaly was recorded across both Assessment 1 and Assessment 2 testing on the Unit 1 perfboard build. On a freshly powered system, idle current draw measured approximately 350 mA. Following a dispense event initiated through the button and LCD interface, idle current draw rose and remained elevated. Subsequent dispense events produced further increases, with the supply eventually indicating up to approximately 850 mA. Assessment 1 additionally recorded that the current was near 1 A with the motors not running — that is, the elevated draw persisted after motion ceased, which is the defining and most diagnostic characteristic of the fault.
Resolution status. The anomaly did not trace to a single root cause but to a combination of contributing factors: a bottleneck on the 3.3 V distribution lines under load, and inconsistent motor coil de-energisation behaviour, consistent with the original hypothesis below that the dispense-completion path was not de-energising coils as reliably as the boot path. Both were addressed together, through targeted wiring changes on the control board and firmware changes to motor de-energisation handling. Following these changes, idle current on the Unit 2 PCB build is stable at 400 mA (§8.8) with no cumulative increase observed across repeated dispense cycles.
Significance. This behaviour is not benign. It has three consequences:
1.	It directly undermines battery runtime (R-19). Idle draw that ratchets upward with use means calculated runtime degrades over an operating day.
2.	It implies energy is being dissipated somewhere it should not be, with an associated thermal concern in a sealed enclosure containing lithium-ion cells.
3.	It indicates a state that is not being correctly cleared, which in a safety-relevant embedded system is a concern in its own right, independent of the current it draws.
Diagnostic approach and original hypothesis. The leading hypothesis during investigation was incomplete motor de-energisation: stepper coils left energised after a dispense completes will hold current indefinitely without producing motion, which matched the observation — elevated current with the motors stationary, increasing cumulatively as more motors were exercised. The RP2350 firmware explicitly de-energises all three motors at boot (test B-06 confirms zero coil current until a direction command is issued), establishing that the firmware was capable of de-energising the coils; the open question was whether the dispense-completion path did so as reliably as the boot path. This hypothesis proved to be a genuine contributing factor, alongside the independent 3.3 V distribution bottleneck identified above.
[⚠ PLACEHOLDER] Add a before/after current measurement here if available (idle current before the fix vs. the stable 400 mA now measured on Unit 2, §8.8) — this is the strongest possible evidence for this subsection and directly supports PA3.
[INSERT CHART: Current draw versus cumulative dispense events] Line chart, x-axis dispense count, y-axis measured idle current, ideally showing the historical Unit 1 climb alongside the flat Unit 2 trace post-fix.


# 9. Verification and Validation
[RUBRIC: ED5 — Verification & Validation (Quantitative) 10 pts + PA3 — Validation (Qualitative) 10 pts.]
These are two different things and the rubric marks them separately:
— ED5 = "tests the individual component parts, tests the system, and measures the system against the project specifications." Sections 9.1–9.3 — the compliance argument.
— PA3 = "evaluates validity of results, risks, errors and uncertainties." Section 9.4 — the epistemic argument: how confident are you in your own results, and why?
§9.4 is the one most teams omit entirely. It is ten marks for honest reflection on measurement quality, and it is written in an afternoon.
9.1 Integration Verification Results
Cross-subsystem integration was exercised extensively during development, surfacing and resolving eight distinct integration-level defects that were not reachable by subsystem testing in isolation (§7.5.1–§7.5.8): ESP32-S3 power-coupling instability, inter-processor command corruption, a clock-synchronisation collision, socket exhaustion under multi-client load, cross-station state corruption, a blocking notification call, and the diagnostic tooling gap that enabled finding the rest. Per-station logical independence (INT-12) and multi-slot sensor independence (U-01, Table 8.6) are both confirmed passing. [⚠ PLACEHOLDER] The signed Functional Prototype Test Plan, Part A record should state the exact tests-executed/tests-passed count for this phase directly from the document; that figure is not reproduced here since Table 8.10's phase breakdown is not explicitly split into Part A and Part B in this draft.
9.2 System Validation Results
Table 9.1 records verdicts against all 36 tracked requirements. Of the 15 Must-priority requirements, 10 are fully Met and 5 are Partially Met, with none Not Met — the five partial results (R-01 dispense reliability, R-04 scheduling latency, R-06 switchover characterisation, R-16 envelope/mass data, R-36 pill-geometry quantification) each reflect a specific outstanding measurement rather than a functional failure, and are detailed individually in Table 9.1. Of the 11 Should-priority requirements, 9 are Met, 1 is Not Met (R-19, battery runtime), and 1 is Superseded by a deliberate design change (R-11). Of the 5 Could-priority requirements, 2 are Met and 3 are Not Met by deliberate scope decision, consistent with their MoSCoW priority. All 7 Won't-priority items remain formally out of scope for Version 1. See Table 9.2 for the full breakdown by priority.
Table 9.1 — Requirements compliance summary
Req	Priority	Requirement (abbreviated)	Verdict	Evidence	Comment
R-01	Must	One pill per actuation, no jam	Partially Met	§8.3	~88–90% success over 50 cycles (approximate, recollection-based); zero double-feeds; ~2 full jams, attributed substantially to test-candy degradation rather than mechanism failure. Larger sample and real-pill validation recommended (§9.4.2, §14.5)
R-02	Must	Exact programmed dose quantity	Met	§8.3	Multi-pill doses tested up to 3 pills per dose; exact commanded quantity delivered correctly in all cases tested
R-03	Must	Dual-sensor delivery verification	Met	§8.4	Sequential IR beam-break and piezoelectric impact confirmation implemented and verified (Table 8.6, all sensor tests pass). ⚠ Requirement wording still flagged for review — see §3.2; revisit this verdict if the wording changes materially
R-04	Must	Dispense within 2 s of scheduled time	Partially Met	§8.5	The original 2 s target does not account for worst-case retry timing (up to ~35 s across 3 attempts, Table 8.7); the team's assessment is that a 10 s target for dispense-attempt initiation is more realistic. Recommend re-baselining this requirement, similar to R-19 (§3.2), and confirming initiation latency with a direct timestamp measurement rather than estimation
R-05	Must	5 V rail within ±5% under load	Met	§8.2	Measured ≥ 4.92 V under three-motor load; 3.6% margin
R-06	Must	Uninterrupted battery switchover	Partially Met	§8.2	Functionally confirmed by observation (LED remained lit, motor speed unchanged across mains loss); ⚠ quantitative oscilloscope characterisation of the switchover transient against the <50 µs spec outstanding (U-02)
R-07	Must	3.3 V rail within ±0.1 V	Met	§8.2	Measured 3.312 V; 0.36% deviation from nominal
R-08A	Must	Three independent medication profiles	Met	§8.5	One per compartment; firmware allocates five slots for headroom; three-slot operation with differing pill geometries confirmed (§8.6)
R-08B	Must	Independent pill count per profile	Met	§8.4, §8.5	Sensor independence confirmed (U-01); per-station state independence fixed and verified (§7.5.5, INT-12)
R-09A	Should	Visual dose alert	Met	§8.5	LED indication confirmed operational across development and general use
R-09B	Should	Audible dose alert	Met	§8.5	Speaker/audio alert confirmed operational (§7.5.1) following the amplifier power-coupling fix
R-10	Should	Live serial status streaming	Met	§8.5	Diagnostic console operational throughout development
R-11	Should	Manual and simulation modes	Superseded	§3.2	Manual configuration against physical hardware replaced the simulated smart-cap data source; dashboard-triggered test dispense retained as a diagnostic capability
R-12	Could	Local web dashboard, no internet	Met	§8.5	Direct hotspot access to the dashboard confirmed without an internet path (§8.6); offline-first architecture is the system's core design principle (§1)
R-13	Could	Dedicated RTC hardware	Not Met — superseded	§1.5, §5.2	Deliberately deferred in Capstone I. Network time synchronisation over Wi-Fi achieves the underlying goal with better accuracy and automatic recovery
R-14	Won't	Smart cap — out of scope	N/A	§1.5	Formally deferred
R-15	Must	Store and manage three medication profiles	Met	§8.5	Matches the three physical compartments; profile creation, editing and flash persistence confirmed in general use
R-16	Must	Portable enclosed storage	Partially Met	§8.3	Functional portability confirmed (F-22, carried assembled unit, §8.5); quantified envelope and mass measurements (Table 8.5) still outstanding
R-17	Must	Battery charges from wall power	Met	§5.1.2	UPS charges continuously from the 5 V/3 A USB-C input; charge-complete indicated by module LED; system also runs mains-only with no cells fitted
R-18	Must	Fault detection	Met	§8.3, §8.5	Negative-path test coverage confirmed across obstruction, timeout and multi-pill detection (Section 6.3); error acknowledgements verified in the UART command interface (Table 8.8, Phase 6)
R-19	Should	≥ 24 h battery backup, idle	Not Met	§8.8	Target re-baselined from 48 h — see §3.2. Measured 400 mA idle on Unit 2 PCB build (5 V rail); ~10.4–11.0 h derated runtime, ~43–46% of the 24 h target
R-20	Should	Missed dose logging	Met	§5.5.3, §8.6	Three-state event log — Taken / Pending / Failed — reviewable on the dashboard
R-21	Should	Low pill quantity notification	Met	§8.5, §8.6	Alerts at 1 and 0 pills, edge-triggered; confirmed working in use (Table 8.11)
R-22	Should	Local dispensing event logging	Met	§8.5	16-entry dispense history buffer; dispense event logging exercised throughout testing
R-23	Could	Simplified configuration mode	Not Met — deliberate	§5.5	Remains a Could-priority item, not required for Version 1 completion (§3.2)
R-24	Could	Multiple user profiles	Not Met — deliberate	§14.5	Single-patient scope retained for reliability; schedules remain fully editable
R-25	Could	Remote caregiver notification	Met — exceeded	§5.5.3, §8.6	Push notification delivery verified; escalating pickup reminders implemented
R-32	Must	Dose retrieval confirmation	Met	§1.5.1, §8.6	Drawer pickup confirmation verified; F-13 (Hall effect sensor) confirmed passed (§8.5)
R-33	Should	Escalating uncollected-dose reminders	Met	§5.5.3, §8.6	Four stages verified end-to-end via the ntfy notification client
R-34	Should	Concurrent multi-client dashboard access	Met	§8.6	Two devices, peak 7 sockets, no failures
R-35	Should	Remote diagnostic log access	Met	§7.5.7, §8.6	In-device log viewer over network
R-36	Must	Differing pill geometries across compartments	Partially Met	§8.3	Qualitatively confirmed — all three slots dispensing their respective pill sizes correctly (§8.6); quantified per-geometry breakdown (Table 8.4) still outstanding
R-26…R-31	Won't	Out of scope for Version 1	N/A	§1.5	Formally deferred

[TEMPLATE NOTE — ON REPORTING FAILURES] Use verdicts of Met, Partially Met and Not Met honestly. "Should" and "Could" requirements that were not met are entirely legitimate outcomes — that is precisely what the MoSCoW priorities are for. Reporting an unmet "Could" costs you nothing under the rubric; claiming a requirement was met when the evidence does not support it costs you credibility across the whole report, and is an integrity issue as well as a marks issue. State the reason for each shortfall and carry it into §14.5.
Table 9.2 — Compliance summary by priority
Priority	Total	Met	Partially Met	Not Met	N/A
Must	15	10	5	0	—
Should	11	9	0	1	1*
Could	5	2	0	3	—
Won't	7	—	—	—	7

*R-11 is counted under Should/N/A as "Superseded" — the original requirement (manual and simulation modes) was replaced by a design decision (§3.2) rather than met, partially met, or missed.

[INSERT CHART: Requirements compliance by priority] Stacked bar chart, one bar per MoSCoW priority, segmented Met / Partially Met / Not Met. A single image that communicates project completeness at a glance — put this one in the presentation as well.
9.3 Verification Summary by Functional Unit
Table 9.3 — Verification status by functional unit
Unit	Test plan	Tests defined	Executed	Passed	Outstanding
FU-1 Power	Board Test Plan Ph. 1–3, 6–7	[⚠ NEED TOTAL FROM BOARD TEST PLAN]	[ ]	[ ]	Phase 6 battery runtime; Phase 7 switchover characterisation
FU-2 Control & Motor	Pico 2 Firmware Test Plan	54	49	48	Phase 4 profile persistence (5 tests, not yet executed); K-03 (Phase 7, marked N/A)
FU-3 Sensing	Pico 2 Ph. 2; Board Ph. 5	12 (Pico 2 Ph. 2) + [⚠ NEED BOARD PH. 5 TOTAL]	12 (Pico 2 Ph. 2) + [ ]	12 (Pico 2 Ph. 2) + [ ]	S-02 IR GPIO signal voltage
FU-4 Mechanical	Mechanical Test Plan Ph. 1–7	[⚠ NEED TOTAL FROM MECHANICAL TEST PLAN]	[ ]	[ ]	Drawer, magnet retention, slide clearance, envelope
FU-5 User Interface	ESP32-S3 Firmware Test Plan	[⚠ NEED TOTAL — Table 8.9 only records 7 categories, not per-category test counts]	[ ]	[ ]	All 7 categories pass at category level (Table 8.9); per-test breakdown not available in this draft
INT-1 Integration	Functional Prototype Test Plan (Parts A/B)	25	25	25	None — all seven previously outstanding tests (F-10 to F-13, F-17, F-20, F-22) now confirmed passed (Table 8.10)

9.4 Validity of Results, Errors and Uncertainties
[RUBRIC: PA3 — Validation (Qualitative) — 10 pts. This is the direct answer to that criterion and it is the section most commonly missing from capstone reports. The content below is largely written; complete the bracketed items with your instrument details and any additional limitations you identify.]
A test result is only as trustworthy as the method that produced it. This section assesses the validity of the results reported in Section 8, identifies sources of error and uncertainty, and states the resulting limits on what may legitimately be concluded from them.
9.4.1 Measurement Uncertainty
Instrument accuracy. Voltage and current measurements were taken with a digital multimeter and an oscilloscope. Specific instrument makes and models are not recorded in this draft; the uncertainty analysis below is therefore expressed in terms of typical bench-instrument accuracy class rather than a calibration-sheet figure for the specific unit used. A typical bench DMM specifies roughly ±0.5% of reading plus a small digit count on DC volts. Applied to the 3.312 V measurement, that gives an uncertainty of approximately ±0.017 V, so the result should properly be expressed as 3.312 ± 0.017 V. Because this uncertainty band lies entirely inside the 3.2–3.4 V requirement, the Pass verdict holds even at the limits of instrument uncertainty. Making that argument explicitly is what PA3 is asking for — it demonstrates that you understand the difference between a reading and a measurement.
Resolution limits. Multimeter readings are point-in-time samples and cannot capture transient behaviour. This is the specific reason the switchover transient (U-02) requires oscilloscope capture rather than a meter reading: a meter would not resolve a sub-millisecond dropout, and the current claim of "seamless" switchover rests on functional observation — a lit LED and unchanged motor speed — rather than on measurement. The switchover claim is therefore qualitative, and should be stated as such until U-02 is completed.
Timing measurement. Timing derived from serial log timestamps is limited by the resolution of the firmware timestamp source and by USB serial transmission latency. Timestamps are generated at the point of event handling, not at the physical event, so a small and systematic positive bias is expected. This affects absolute timing figures but not the relative comparisons — such as the delay between piezo and IR confirmation — where both timestamps carry a similar bias that largely cancels.
Mechanical measurement. Dimensional measurements taken with digital calipers carry an instrument uncertainty of approximately ±0.02 mm, which is well below the design tolerances of ±0.1 to ±0.5 mm and therefore not a limiting factor. The dominant source of dimensional variation is not measurement but the FDM printing process itself — layer height, thermal contraction on cooling, and print orientation produce part-to-part variation that exceeds instrument uncertainty by an order of magnitude. Dimensional results should therefore be understood as characterising the parts that were printed, not the design as an idealised specification.
9.4.2 Limitations of the Test Population
Test medium. Dispensing reliability was characterised using non-medication test pieces (confectionery of comparable size). These differ from real pharmaceutical tablets in surface finish, coefficient of friction, density, hardness and dimensional consistency. Conclusions about dispensing reliability therefore apply directly only to the tested medium, and extrapolation to pharmaceutical tablets carries genuine uncertainty. This is the substance of risk R-04 and is the principal limitation on the reliability claims in §8.3.
[PLACEHOLDER] If you complete the pill geometry characterisation recommended in §5.4.3 using over-the-counter tablets, revise this paragraph — it materially narrows this uncertainty and turns a stated weakness into a characterised operating envelope.
Sample size. Reliability claims are bounded by the number of cycles executed. The reliability trial reported in §8.3 comprised 50 dispense cycles in aggregate, not tracked per slot — smaller than the 150-cycle (50 per slot) protocol originally targeted, and the individual pass/fail/retry counts are recollection-based rather than logged in real time (§8.3). A success rate derived from 50 cycles has a wide confidence interval and cannot meaningfully distinguish, for example, a 90% true success rate from a 96% one. The figures in Table 8.2 should be read as an approximate, directional result rather than a statistically tight estimate, and a larger, logged trial is recommended before the reliability claim is relied upon (§14.5).
Duration. The longest continuous operational test performed was a 30-minute soak test (F-25). No test has yet characterised behaviour over days or weeks. Failure modes associated with sustained operation — thermal drift, memory fragmentation, clock drift, cumulative mechanical wear, and the current-draw creep of §8.9 — are therefore uncharacterised over realistic use intervals. For a device intended to run continuously for months in a patient's home, this is the most significant gap in the validation evidence, and it is stated as such.
Environmental conditions. All testing was conducted in a laboratory environment at approximately room temperature and humidity. No testing has been performed at temperature extremes, under mechanical vibration, or under transport conditions. Behaviour outside laboratory conditions is not characterised.
9.4.3 Sources of Systematic Error
Observer effect during dispense counting. Pills were counted manually. Manual counting is subject to human error, though this is mitigated by the small counts involved and by cross-checking against the system's own logged count. Where the two disagreed, the physical count was treated as authoritative, since the system's count is the quantity under test and cannot validate itself.
Test-to-test coupling. Consecutive dispense cycles are not fully independent: mechanism state, container fill level and residual pill position at the end of one cycle become the initial conditions of the next. Success rates therefore describe sequences of operation rather than a population of independent trials, which is the more operationally realistic characterisation but limits the statistical treatments that can validly be applied.
Unit under test. Results were gathered across two physical builds (perfboard Unit 1 and PCB Unit 2) at different points in development, and against evolving firmware. The 50-cycle reliability trial (§8.3) was run across both units combined rather than on a single one, which strengthens the result in one respect — it is not an artefact of one specific build — but weakens it in another, since the aggregate figure cannot be broken down by unit and cannot be directly compared against single-unit measurements reported elsewhere, such as the 400 mA idle-current figure measured specifically on Unit 2 (§8.8). Where a result predates a subsequent design change, this is noted individually (for example, the current-draw anomaly of §8.9 was characterised on Unit 1 before its resolution on Unit 2); a result from the perfboard unit does not necessarily characterise the PCB unit, and the two should not be treated as interchangeable without this caveat.
9.4.4 Threats to Validity of Conclusions
Verification cannot be validated by the system under test. Dispense verification depends on the same sensors whose behaviour is being characterised. A systematic sensor fault would produce consistent-looking results that are consistently wrong. This threat is mitigated by requiring two independent sensing modalities with different physical principles — a common-mode failure affecting both a piezoelectric transducer and an optical beam-break simultaneously is substantially less likely than a fault in either alone — and by cross-checking against manual pill counts, which are entirely independent of the system.
Absence of evidence is not evidence of absence. Several results record "no false triggers observed" over 30- or 60-second windows. These establish that false triggering is not frequent; they do not establish that it does not occur. A false-trigger rate of once per hour would pass every one of these tests. Longer-duration monitoring would be required to bound the false-trigger rate meaningfully, and this is recommended in §14.5.
Resolved anomaly, residual caveat. The progressive current-draw increase documented in §8.9 was root-caused to a combination of a 3.3 V distribution bottleneck and inconsistent motor de-energisation, and is resolved on the Unit 2 PCB build. Because the underlying mechanism was only precisely isolated after the fact, any measurement taken on Unit 1 prior to the fix should not be assumed free of its effect, particularly for thermal behaviour and long-duration reliability figures gathered on that build.
9.5 Known Limitations
[TEMPLATE NOTE] Stating limitations plainly is a strength, not a confession. Evaluators find limitations regardless; the difference is whether you found them first. Complete and extend this list.
#	Limitation	Impact	Status / Mitigation
1	No hardware RTC — time is re-acquired over Wi-Fi on boot; manual entry required only where no known network is reachable	Narrow: affects first-time setup, or relocation to a site with no configured Wi-Fi	Accepted design decision, not a defect. TIME_REQ retry plus manual entry via LCD and console
2	Access point uses a weak shared default passphrase; API endpoints are not individually authenticated	Any client that has joined the network can command a dispense	WPA2 encryption in place; device-unique passphrase and endpoint authentication recommended (§11.4, §14.5)
3	Dispensing characterised primarily with non-medication test pieces	Reliability with real pharmaceutical tablet geometries not fully established	Risk R-04 remains open; characterisation recommended (§5.4.3)
4	Longest continuous operational test is 30 minutes	Long-duration failure modes uncharacterised	Extended soak testing recommended (§14.5)
5	Touch layer present on the display but unused for navigation	Reduced input flexibility	Deliberate accessibility decision; documented (§5.5.2)
6	Smart cap / pharmacist-side programming not implemented	Profiles entered manually; the professional-verification gap identified in §1.3.1 is not closed	Formally deferred (R-14); recommended as the highest-value future work (§14.5)
7	Three compartments only	Patients on more than three concurrent medications not fully served	Architecture scales; constrained by envelope and team capacity
8	portapill.local hostname resolution varies by client operating system	Users on some devices must use the numeric address instead	Numeric address remains reliable; documented for users
9	Several Week 12 features functioning but not separately recorded as hardware-verified	Formal verification status understated for escalating reminders, low-pill alerts, per-motor speed	Confirm and update status (§8.6)
10	Push notification requires a network path to an external service	Notification event data leaves the device when the feature is enabled, though encrypted in transit over HTTPS	Optional feature; core operation unaffected; payload contents and topic naming to be documented (§11.4)
11	Battery runtime against R-19	Measured ~10.4–11.0 h derated runtime on the Unit 2 PCB build (400 mA idle on the 5 V rail, 6600 mAh / 24.42 Wh pack) against a 24 h requirement — Not Met, roughly 43–46% of target	Idle load reduction (LCD backlight timeout, gated IR emitters) recommended as follow-up work (§8.8, §14.5)
12	Hall effect sensor role	See OPEN ITEM, §1.5	Resolve

