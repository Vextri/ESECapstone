# 3. Project Requirements
3.1 Requirements Methodology
Requirements were captured using the MoSCoW prioritisation framework (Must / Should / Could / Won't) and expressed in formal engineering language using the modal verbs shall (mandatory), will (intended) and may (optional). Each requirement is written to name the responsible subsystem, the required behaviour, and the condition under which it applies, so that responsibility is unambiguous and each requirement is independently testable.
Each requirement carries a stable identifier (R-nn) used consistently across this report, the subsystem test plans, and the validation evidence in Section 9, providing end-to-end traceability from user need through design and test to result.
Requirements are additionally categorised as User (patient-facing safety and usability) or Dev Team (engineering, firmware and hardware implementation), following the classification introduced in the Capstone I revision.
3.2 Project Scope, MoSCoW Requirements
Table 3.1: MoSCoW requirements, as delivered
ID	Requirement	Category	Must	Should	Could	Won't
R-01	The electromechanical subsystem shall release one pill per actuation cycle through the dispensing chute without mechanical jam, completing a successful cycle in approximately 5 seconds under normal conditions (estimated; not yet formally measured)	Dev	X			
R-02	The electromechanical subsystem shall dispense the exact programmed dose quantity per cycle with no discrepancy from the expected count	Dev	X			
R-03	The electromechanical subsystem shall confirm both piezoelectric impact and infrared beam-break within a 300 ms confirmation window following impact detection, with up to three retry attempts before declaring the dispense a failure	Dev	X			
R-04	The control subsystem shall initiate a dispense sequence within 2 seconds of a user-programmed scheduled dose time	Dev	X			
R-05	The power management subsystem shall maintain the primary supply rail within ±5% of 5 V nominal under full electromechanical load	Dev	X			
R-06	The power management subsystem shall sustain continuous system operation upon external power loss by switching to battery supply with no interruption	Dev	X			
R-07	The power management subsystem shall supply a regulated 3.3 V sensor supply rail within ±0.1 V under all operating load conditions, dedicated to the piezoelectric, infrared and Hall-effect sensor divider network	Dev	X			
R-08A	The control subsystem shall maintain three independent medication profiles simultaneously, one per physical compartment, with isolated dose quantity tracking per profile	Dev	X			
R-08B	The control subsystem shall track remaining pill count independently for each active medication profile without cross-profile interference	Dev	X			
R-09A	The UI subsystem will present a visual dose alert on the display within approximately 500 ms of a dose-related event	User		X		
R-09B	The UI subsystem will activate an audible alert on the device within approximately 500 ms of a dose-related event	User		X		
R-10	The control subsystem will provide a manually-triggered USB serial diagnostic mode, invoked via the V command, that streams piezoelectric and infrared sensor status to a connected terminal at 5 Hz while active	Dev		X		
R-11	The UI subsystem will support full dispensing validation without physical bottle hardware using configurable operating modes including manual entry and simulation	Dev		X		
R-12	The UI subsystem may display active medication profile status on a locally hosted web dashboard accessible via browser without internet connectivity, reflecting state changes within 2 seconds	Dev			X	
R-13	The control subsystem may maintain dose scheduling accuracy using dedicated real-time clock hardware	Dev			X	
R-14	Automatic prescription loading via smart cap hardware is not included in Version 1	Dev				X
R-15	The system shall store and manage three distinct medication profiles, corresponding to the three physical dispensing compartments	User	X			
R-16	The system shall provide portable and enclosed pill storage within the dispensing unit	User	X			
R-17	The system shall charge the backup battery from external wall power during normal operation	Dev	X			
R-18	The system shall detect fault conditions that prevent successful pill delivery or uninterrupted system operation	Dev	X			
R-19	The power management subsystem shall provide a minimum of 24 hours of battery backup operation under idle conditions	Dev		X		
R-20	The system will retain and log dose events for user review, distinguishing doses taken, pending collection, and failed	User		X		
R-21	The system will notify the user when pill quantity is low and a refill is required	User		X		
R-22	The system will log dispensing events locally for verification and troubleshooting	Dev		X		
R-23	The system may support an alternative simplified configuration mode for general users	User			X	
R-24	The system may support multiple user profiles with individual scheduling	User			X	
R-25	The system may provide remote notifications to a caregiver or patient	User			X	
R-32	The system shall confirm that a dispensed dose was physically retrieved by the user	User	X			
R-33	The system will escalate reminders at increasing intervals while a dispensed dose remains uncollected	User		X		
R-34	The system will support concurrent dashboard access from multiple client devices without service degradation	Dev		X		
R-35	The system will provide remote diagnostic log access over the network without physical cable connection	Dev		X		
R-36	The dispensing mechanism shall accommodate differing pill geometries across the three compartments	Dev	X			
R-26	A dedicated mobile application is out of scope for Version 1	User				X
R-27	Automatic prescription refill functionality is out of scope for Version 1	User				X
R-28	AI-based medication recommendations are out of scope for Version 1	User				X
R-29	Cloud-based event logging is out of scope for Version 1	Dev				X
R-30	QR code configuration and verification are out of scope for Version 1	Dev				X
R-31	A lock and unlock mechanism for the pill cap is out of scope for Version 1	Dev				X

R-32 through R-36 were added during Capstone II to formally specify five capabilities delivered in Week 12 development that had no corresponding requirement: dose pickup confirmation, escalating uncollected-dose reminders, multi-client dashboard support, remote log access, and per-compartment accommodation of differing pill geometries. R-32 in particular specifies the third verification stage described in Section 1.5.1, the project's strongest differentiator, and is prioritised Must because pickup confirmation is on the critical path of the delivered verification chain.

R-25, remote notifications, remains recorded as a Could. It was originally expected to go unmet, but push notification delivery is implemented and verified in the delivered system (Section 5.5.3, Section 8.6). It is recorded in Section 9 as Met, exceeding its original Could-level expectation, rather than left as an unmet aspirational item.

R-19, battery backup, is set at 24 hours rather than the 48-hour figure carried over from the Capstone I single-compartment design. The battery exists to bridge a mains interruption, not to serve as a primary supply: the device is mains-powered in normal use, spends the overwhelming majority of its operating life idle, and 24 hours of autonomy covers the great majority of domestic power outages given the added load of three stepper drivers, three sensor pairs, a backlit display and an active Wi-Fi radio. The discharge test validating this figure must be run on the PCB build specifically, since the reference 350 mA idle-draw figure was measured on the perfboard build and the two may differ.

R-15 and R-08A both specify three medication profiles, one per physical compartment. Capstone I's original targets (R-15: up to six profiles; R-08A: a minimum of five) reflected an early scope set before the team had a working prototype or a settled physical form factor. Once the enclosure was sized against what could be reliably 3D printed, assembled and wired by a two-person team within the semester, three compartments was set as the Version 1 target: enough to demonstrate the multi-medication capability that is the project's core use case, without the added mechanical and firmware complexity of a fourth or fifth compartment. The firmware data structures still allocate five profile slots, of which three are in active use; this is not an inconsistency, since profiles are stored configurations and compartments are physical dispensing paths, two legitimately different quantities, and the spare slots provide headroom to add compartments in a future revision without changing the stored data format. This distinction should be stated explicitly in Section 5.2.3.

R-11, simulation mode, described in the Capstone I design as loading a pre-defined demonstration profile in response to a simulated smart-cap insertion, a placeholder for smart-cap hardware that was later deferred (Section 1.5). The delivered system instead configures real profiles through the LCD interface and the web dashboard against real hardware, which supersedes the need for a simulated data source. R-11 is marked superseded on that basis. The dashboard-triggered test dispense recorded in the Week 12 development record is a distinct and genuinely useful capability, a manually triggered diagnostic dispense, and should be recorded separately as such rather than under R-11.

R-13, dedicated real-time clock hardware, is a Could and is Not Met by the delivered hardware, an acceptable outcome for a Could. The final board has no battery-backed RTC; time is kept in software on the RP2350 and synchronised via the ESP32-S3 over Wi-Fi (Section 1.5, change 5). This is recorded accordingly in Section 9.2 and Section 9.5.

R-09A, R-09B, R-10 and R-12 carry timing figures inherited from Capstone I. Each was re-checked against the delivered firmware source rather than re-asserted from memory, and the figures are reported here with the basis for each stated explicitly, since a code-derived estimate and a bench-instrumented measurement are not the same class of evidence and should not be presented as though they were.
- R-09A/B (500 ms): the Pico firmware holds a deliberate 300 ms delay after sensor confirmation before it acknowledges the dispense to the ESP32-S3 (pill_dispenser.c). From acknowledgement, the ESP32-S3 side queues the LED and audio events synchronously with no polling delay, adding single-digit milliseconds of FreeRTOS task-switch latency. The combined path is approximately 300 to 320 ms, plausibly inside the 500 ms figure, but this is a timing-budget estimate derived from reading the code, not an oscilloscope or logic-analyser measurement, and should be confirmed by test before final submission.
- R-10 (5 Hz): confirmed exactly in firmware (a 200 ms print interval in the V command's monitor loop). However, the original wording, "stream live status during active testing", overstated what this mode actually is: it is a manually-triggered, blocking diagnostic mode that must be started from the serial terminal and that suspends normal stepper and dispense operation while running, not a continuous background telemetry stream. It also covers only the piezo and IR sensors; Hall-effect status is no longer available on the Pico, since that sensor now reports through the ESP32-S3. R-10's wording above has been corrected to describe this behaviour accurately.
- R-12 (2 seconds): confirmed in firmware. The dashboard polls /api/status on a 1.5 second interval, comfortably inside the 2-second figure. State changes triggered from the same browser session (saving a profile, a manual dispense) update immediately on POST completion rather than waiting for the next poll; only externally-triggered changes (a scheduled dispense, another device, another browser tab) are bound by the 1.5 second polling interval. The 2-second claim is a polling-based guarantee, not an event-driven one, and that distinction is worth keeping in mind if it is challenged in review.
- R-01 (approximately 5 seconds): not measured. This is an informal estimate from operating the device by hand for a single successful, non-jammed cycle, and is stated in the table as an estimate rather than a verified figure. It excludes the time for the user to physically open the drawer and retrieve the dose, and it does not apply to the jam-recovery path, which runs to approximately 35 seconds worst case across three retries (Section 4.4).

3.3 SMART Criteria
Table 3.2: Application of the SMART framework to the requirement set
Criterion	Interpretation for this project	Measurement method
Specific	Each requirement names one responsible subsystem, one defined action, and one outcome, leaving no interpretive ambiguity.	Structured requirement language identifying subsystem, expected behaviour, and applicable condition.
Measurable	Performance thresholds are numerical wherever physically meaningful: voltage tolerances, timing windows, dose count accuracy, runtime duration.	Digital multimeter readings, oscilloscope captures, timestamped serial log output, and physical pill counts.
Achievable	Every target is grounded in a component specification or in measured bench data from the prototype, not in aspiration.	Subsystem test results in Section 8, traced to datasheet limits in Appendix E.
Relevant	Every requirement traces to a core product function: safe, scheduled, verified medication delivery.	Each requirement maps to at least one integration or validation test (Section 3.4, Section 9.3).
Time-Bound	Timing constraints appear explicitly in requirement text rather than being implied by context.	Timed serial log observation, scheduled dose trigger tests, oscilloscope timing measurement.

3.3.1 SMART Goal Statements for Core Requirements
SMART Goal 1, Sensor-Verified Dispensing (R-01, R-02, R-03, R-18)
Element	Statement
Specific	Implement a closed-loop dispensing cycle in which the RP2350 actuates the compartment stepper motor and confirms physical pill delivery through a piezoelectric impact sensor followed by an infrared beam-break sensor before logging the event as successful. Requires C firmware development on Pico SDK 2.2.0, interrupt handling, GPIO signal conditioning design, and mechanical sensor integration.
Measurable	Success is measured by dispense success rate across a defined trial population of cycles, by verified absence of false-positive confirmations, and by correct pill count decrement. Benchmark: one pill delivered per actuation, both sensor confirmations captured within the 300 ms window, zero double-dispense events.
Achievable	Both sensing modalities were individually validated on the bench prior to integration, piezo transient response confirmed on oscilloscope, IR beam-break interrupt confirmed in serial log, and the full sequence was demonstrated end-to-end in the Pico firmware test plan (F-03, F-06).
Relevant	This is the project's principal engineering contribution and the differentiator identified in Section 2.5. Without it, PortaPill is functionally equivalent to existing Tier 3 devices.
Time-Bound	Individual sensor verification completed by Capstone II Assessment 1 (June 2026); full closed-loop cycle demonstrated by Assessment 2 (July 2026); long-duration reliability trial required before final submission. Dependencies: mechanical mechanism must be finalised before reliability data is meaningful.

SMART Goal 2, Uninterruptible Power Management (R-05, R-06, R-07, R-17, R-19)
Element	Statement
Specific	Implement a power architecture that maintains a stable 5 V rail, powering the RP2350, ESP32-S3 and motor drivers directly, and a separately regulated 3.3 V rail dedicated to the sensor divider network, both under full electromechanical load; switches to battery backup on mains loss with no interruption to active operation or programmed data; and charges that battery from wall power during normal operation.
Measurable	5 V rail held within ±5% of nominal and 3.3 V rail within ±0.1 V under full load; zero-interruption switchover on mains loss, no reset and no data loss; minimum 24 hours of battery backup at idle, measured on the PCB build.
Achievable	Grounded in the HW-465A UPS module specification (approximately 96% efficiency, zero-delay automatic source switching, integrated overcharge/overdischarge/overcurrent/over-temperature protection) and a 1S2P 18650 pack (6600 mAh, 24.42 Wh) sized against measured idle draw (Section 4.7); validated by P-01, M-06 and PWR-05 (rail stability), U-01, U-02 and F-23 (switchover), and PWR-09 (charging) in the Power Board Test Plan.
Relevant	Directly serves G5 (continue operating through a mains power failure without reset or data loss). A medication-adherence device that loses its schedule or a dispense-in-progress state during a power interruption is unsafe by the definition set out in Section 1.3.1.
Time-Bound	Rail stability and UPS switchover validated on the perfboard build ahead of Capstone II Assessment 1; the 24-hour battery runtime figure was re-baselined from the original 48-hour Capstone I target (Section 3.2) and its discharge test must still be run on the PCB build specifically before final submission (Section 8.8), since the reference idle-draw figure was measured on the perfboard.

SMART Goal 3, Multi-Compartment Mechanical Dispensing (R-01, R-15, R-16)
Element	Statement
Specific	Design and validate three independent, physically distinct dispensing mechanisms, one per compartment, within a single portable enclosure, each releasing one pill per actuation without mechanical jam and accommodating a different pill geometry per compartment (R-36).
Measurable	Zero jam or double-feed events across a defined trial population of actuations per compartment; three compartments demonstrated to operate without cross-compartment interference; enclosure, drawer and container fit within the dimensional tolerances set in the Mechanical Enclosure Test Plan.
Achievable	Grounded in eight physical design iterations in SolidWorks, each validated independently of electronics via dimensional inspection and manual actuation (FU-4, Section 4.2); validated by MEC-03, MEC-04 and F-14 to F-16 (R-01) and TP-04 to TP-06, F-21 and F-22 (R-16) in the Mechanical Enclosure and Functional Prototype Test Plans.
Relevant	Directly serves G4 (manage multiple independent medications without cross-contamination of counts or schedules) and G1 (eliminate the need to physically open a container). The three-compartment mechanism is the physical precondition for every other requirement in the multi-medication use case identified in Section 1.3.1.
Time-Bound	Mechanical design and dimensional validation completed ahead of PCB integration (Section 7); a long-duration jam and double-feed reliability trial across all three compartments is required before final submission, the same dependency noted for Goal 1.

SMART Goal 4, Accessible Multi-Modal User Interface (R-09A, R-09B, R-12, R-21, R-23)
Element	Statement
Specific	Provide dose-event alerting through at least two independent sensory channels, a visual display alert and an audible tone, plus a browser-based dashboard, using large-format text and discrete physical buttons rather than a touchscreen, structured for an elderly, non-technical user.
Measurable	Visual and audible alerts fire within an estimated 500 ms of a dose event (Section 3.2); the dashboard reflects state changes within 2 seconds via its 1.5 second poll interval; the low-pill notification triggers correctly at its configured threshold.
Achievable	Grounded in the accessibility-first design decision documented in Section 1.5 (change 4: physical buttons replacing the Capstone I touchscreen); validated by F-19 and the LED indication tests (R-09A), the audio alert test (R-09B), and F-05 to F-08 and INT-02 (R-12) in the ESP32-S3 Firmware and Functional Prototype Test Plans.
Relevant	Directly serves G6 (alert through more than one sensory channel) and G8 (interface usable by an elderly, non-technical user), the primary design constraint from which the interface decisions in Section 1.4 follow.
Time-Bound	Button navigation, LED and audio alerting demonstrated in the ESP32-S3 Firmware Test Plan; dashboard polling and low-pill notification validated in the Functional Prototype Test Plan. R-23 (a simplified configuration mode) remains a Could and is not required for Version 1 completion.

3.4 Requirements Traceability Matrix
Table 3.3: Requirements traceability
Req ID	Design section	Verification test(s)	Validation evidence	Status
R-01	Section 5.4 Mechanical	MEC-03, MEC-04, F-14 to F-16	Section 9.2	[Met / Partially Met / Not Met]
R-02	Section 5.2, 5.4	F-06 (multi-pill dose)	Section 9.2	[ ]
R-03	Section 5.3 Sensing	F-03, F-04, F-05, T-01	Section 9.2	[ ]
R-04	Section 5.2 Control	F-18 (scheduled dispense)	Section 9.2	[ ]
R-05	Section 5.1 Power	P-01, M-06, PWR-05	Section 9.2	[ ]
R-06	Section 5.1 Power	U-01, U-02, F-23	Section 9.2	[ ]
R-07	Section 5.1 Power	P-02, P-03, PWR-07	Section 9.2	[ ]
R-08A/B	Section 5.2 Control	PR-01 to PR-05, U-01	Section 9.2	[ ]
R-09A	Section 5.5 UI	F-19, LED indication tests	Section 9.2	[ ]
R-09B	Section 5.5 UI	Audio alert test	Section 9.2	[ ]
R-10	Section 5.2 Control	B-01, U-04	Section 9.2	[ ]
R-12	Section 5.5 UI	F-05 to F-08, INT-02	Section 9.2	[ ]
R-15	Section 5.2 Control	[NEED TEST ID]	Section 9.2	[ ]
R-16	Section 5.4 Mechanical	TP-04 to TP-06, F-21, F-22	Section 9.2	[ ]
R-17	Section 5.1 Power	PWR-09	Section 9.2	[ ]
R-18	Section 5.2, 5.3	F-04, F-05, F-07, INT-06	Section 9.2	[ ]
R-19	Section 5.1 Power	B-01, B-02, B-03	Section 9.2	[ ]
R-21	Section 5.5 UI	F-19 (low-pill indication)	Section 9.2	[ ]
R-22	Section 5.2, 5.5	F-07, dispense history buffer	Section 9.2	[ ]
R-32	Section 5.3, 1.5.1	[NEED TEST ID, e.g. drawer/Hall pickup test]	Section 9.2	[ ]
R-36	Section 5.4 Mechanical	[NEED TEST ID]	Section 9.2	[ ]

[PLACEHOLDER: R-15, R-32 and R-36 are Must requirements with no assigned verification test yet, every Must requirement needs at least one before submission, or must be reprioritised out of Must. R-33, R-34, R-35, R-20, R-23 to R-25 are Should/Could and are recommended but not mandatory to add here for completeness.]


# 4. System Design
4.1 System Architecture Overview
PortaPill is architected around a deliberate separation of concerns across two microcontrollers connected by a single asynchronous serial link. This is the system's defining architectural decision, and it is made for a specific engineering reason.
Medication dispensing is a real-time, safety-relevant function: a sensor interrupt indicating that a pill has struck the collection drawer must be serviced within a bounded and predictable time, because the motor stop decision depends on it. Graphical interface rendering, Wi-Fi association, HTTP request handling and DNS response are latency-tolerant but computationally bursty functions: a screen redraw or a TCP retransmission can occupy the processor for an unpredictable interval.
Running both classes of work on a single microcontroller would place the dispensing loop in contention with the network and graphics stack. A Wi-Fi client association or a page render could delay servicing of a sensor interrupt, and the resulting jitter would appear directly as inconsistent verification timing, precisely the behaviour the verification design exists to eliminate.
The system therefore partitions responsibility strictly:
●	The Raspberry Pi Pico 2 (RP2350) is the execution authority for the physical dispense action itself. It owns motor actuation, the piezoelectric and infrared dispense-verification interrupts, the dispense state machine, medication profile storage in flash, schedule evaluation, and the software real-time clock. It is the single source of truth for whether a given dispense attempt physically succeeded or failed, reported upward as a single result bit (ACK, result=ok or result=fail); it has no concept of pending or taken, and no role in deciding them. The RP2350 does use its own timeout internally, a 10-second per-attempt sensor wait inside its own retry loop, but this is an unrelated mechanism: it governs whether a single dispense attempt is judged successful, not whether a dose was ever collected. The pickup-lifecycle Timeout state described below is a separate concept, decided entirely on the ESP32-S3, that the RP2350 has no representation of. It runs no network stack and no graphics library.
●	The ESP32-S3 is the presentation and connectivity layer, and is the sole authority over pickup lifecycle state. It owns the LCD, the button interface, the RGB status indicators, the I2S audio channel, the Wi-Fi access point, the DNS responder and the HTTP server. On receiving a successful dispense ACK from the RP2350, it unilaterally creates the Pending state; on its own Hall-effect drawer sensor tripping (Table 5.6, GPIO 33, a single physical sensor covering the shared collection drawer), it unilaterally transitions to Taken with no UART round-trip to the RP2350 for that transition; and on its own self-armed deadline expiring with no ACK received, it unilaterally declares a Timeout. All three of these decisions are made entirely on the ESP32-S3, using its own sensor and its own clock, a direct consequence of the pickup-confirmation hardware being physically wired to the ESP32-S3 rather than the RP2350. It holds a cached copy of dispense-success state for display purposes but is never authoritative over that specific bit.

The two communicate over UART1 at 115200 baud, 8N1, using a line-oriented ASCII protocol. The ESP32-S3 issues commands; the RP2350 executes them and returns acknowledgements and status. The protocol is human-readable ASCII, so the entire inter-processor conversation can be observed and injected with a serial terminal, which proved decisive during debugging, as described in Section 7.
The consequence of this partitioning is a safety property worth stating explicitly: if the ESP32-S3 crashes, hangs, or is disconnected entirely, the RP2350 continues to execute scheduled dispensing without interruption. The user loses the display and the dashboard; they do not lose their medication.
(The system block diagram is presented as Figure 1 in Section 1.4.1; it is referenced here rather than duplicated.)
4.2 Preliminary Design and Design Decomposition
The problem was decomposed into functional units according to three criteria drawn from the project guideline: each unit must be either hardware or software; each must be an essential system component, whether designed or off-the-shelf; and each must be assignable to a single team member as an independent task. A fourth criterion was added by the team: each unit must be independently testable without the others being complete, since a two-person team working in parallel cannot afford serialised integration.
This produced five design units plus one integration artefact.
Table 4.1: Functional unit decomposition and ownership
Unit	Description	Owner	Independently testable via	Section
FU-1	Power Management: 5 V generation, UPS switchover, 3.3 V regulation, battery charge management	Alan	Bench supply, multimeter and oscilloscope; no microcontroller required	Section 5.1
FU-2	Embedded Control and Motor: RP2350 firmware, stepper drive, dispense state machine, profile persistence, software RTC	Alan / Blaise	USB serial command console; motors driven with no mechanism attached	Section 5.2
FU-3	Sensing and Dispense Verification: piezo and IR conditioning, interrupt architecture, verification algorithm	Blaise / Alan	Manual sensor stimulation with serial counter readout; no mechanism required	Section 5.3
FU-4	Mechanical Dispensing: enclosure, singulation mechanism, drawer, containers, sensor mounting	Blaise	Dimensional inspection and manual actuation; no electronics required	Section 5.4
FU-5	User Interface: ESP32-S3 firmware, LCD, buttons, LEDs, audio, Wi-Fi AP, web dashboard	Alan / Blaise	Runs standalone with no RP2350 attached; reports "pending" bridge state	Section 5.5
INT-1	System Integration: control board, custom PCB, wiring harness, inter-processor protocol	Alan / Blaise	Requires FU-1 to FU-5	Section 5.6, 7

The decomposition was validated in practice. Each subsystem carries its own standalone test plan, executed before integration, the Board Test Plan, the Pico 2 Firmware Test Plan, the ESP32-S3 Firmware Test Plan and the Mechanical Enclosure Test Plan, and only after all four had passed was the Functional Prototype Test Plan executed against the assembled unit.
Two specific design provisions made this independence real rather than nominal:
●	The RP2350 firmware exposes a single-character USB serial command interface (Appendix B) allowing every motor, every sensor and the complete dispense engine to be exercised without the ESP32-S3, the mechanism, or the enclosure present. It further provides an M command that injects ESP-format command lines directly, allowing the entire inter-processor protocol to be tested with no ESP32-S3 attached at all.
●	The ESP32-S3 firmware degrades gracefully with no RP2350 present, reporting a "pending" bridge connection state and serving placeholder profile data, so the Wi-Fi, DNS, HTTP and dashboard layers could be developed and tested against a board on its own.
The M injection command and the graceful-degradation behaviour are deliberate design-for-testability decisions rather than incidental conveniences, and are worth stating in exactly those terms when presenting the architecture.
4.3 Component Classification
Table 4.2: Off-the-shelf versus custom-designed content by subsystem
Subsystem	Off-the-shelf (sourced)	Custom designed / developed
Power Management	HW-465A Type-C UPS module, 18650 Li-ion cells, LM317 regulator	Power path architecture and rail budget; LM317 feedback network designed and trimmed for 3.3 V; sensor divider networks for GPIO protection; power distribution and terminal layout on perfboard and PCB
Embedded Control	Raspberry Pi Pico 2 (RP2350), DRV8833 driver modules	Complete interrupt-driven firmware in C on Pico SDK 2.2.0: unified GPIO interrupt dispatcher, three-slot dispense state machine, flash-persisted profile manager with integrity checking, software RTC with DST handling, UART command interface, USB serial diagnostic console
Sensing	Piezoelectric disks, IR emitter/detector pairs, Hall effect sensor	Signal conditioning networks; interrupt architecture with per-sensor debounce; two-modality sequential verification algorithm with bounded confirmation window and retry/recovery logic
Mechanical	28BYJ-48 steppers, N52 neodymium magnets, fasteners	Entire mechanical assembly designed in SolidWorks and 3D printed across eight iterations: two-part dovetailed base with magnetic closure, three-part top assembly, three containers and mounts, three dispensing mechanisms, sliding drawer, side and top panels, speaker holder, button mini-housing
User Interface	ESP32-S3 (Waveshare Pico), ST7796S 4.0" TFT, MAX98357A I2S amplifier, RGB LEDs, tactile switches / 5-way switch	Complete ESP-IDF v6.0 application: Wi-Fi AP, captive DNS responder, HTTP server and JSON API, web dashboard, LCD UI with on-screen keyboard and digit-based schedule editor, button navigation state machine, LED status logic, audio alert generation, UART bridge with mutex-protected shared state and NVS persistence
Integration	(none, fully custom)	Integrated perfboard control board; custom two-layer PCB designed in Altium Designer with GPIO protection, module headers and motor driver placement; keyed wiring harnesses

4.4 Principle of Operation
The system operates as an event-driven finite state machine distributed across the two processors. Figure 2 presents the complete operational flow.
[INSERT FIGURE 2: System state diagram, as built. Must show physical-button navigation (not touch), three independent dispensing paths, the piezo-then-IR verification sequence with the 300 ms window and three-attempt retry loop, and the deep-sleep states as implemented or removed if not present.]
Figure 2: PortaPill system state diagram showing primary states, active substates, and the dispense verification and recovery path.

State 1, Powered Off. No power applied; volatile memory cleared. Medication profiles and schedules persist in RP2350 flash and ESP32-S3 NVS. The software real-time clock does not persist; time must be re-established on the next boot.
State 2, Initialisation. On application of power:
1.	The 5 V and 3.3 V rails stabilise.
2.	The RP2350 boots, initialises clocks, configures GPIO, PWM, UART1 and the interrupt subsystem, restores medication profiles from the last flash sector with magic-number and checksum validation, and initialises all three steppers de-energised (verified by test B-06: no coil current is drawn until a direction command is issued, which matters materially for idle power on battery).
3.	The ESP32-S3 boots, initialises NVS, restores cached slot state and dispense history, configures the UART bridge on GPIO 11/12, launches its FreeRTOS bridge task, and starts the Wi-Fi access point, captive DNS responder and HTTP server.
4.	The RP2350 emits one BOOT_SYNC line per active slot, communicating its authoritative profile state upward.
5.	The RP2350 emits TIME_REQ, requesting current epoch time; unanswered requests are retried at 15-second intervals. Until time is established the clock reports "time not set" and schedule evaluation cannot proceed.
6.	The ESP32-S3 responds CMD|action=SET_TIME|epoch=...; the RP2350 sets its software clock, applies US Eastern local conversion with daylight-saving awareness, and acknowledges.
7.	The ESP32-S3 renders the main screen showing slot status.
State 3, Idle. The nominal resting state. The RP2350 evaluates the schedule against the software clock and polls the serial and UART interfaces; the ESP32-S3 services the button interface, refreshes the display, answers HTTP requests, and polls the RP2350 for status.
State 4, Active. Entered on any event; identifies the event source and routes to the appropriate substate.
State 4a, Dispense Event. The operationally critical path, entered from a scheduled dose time, a button-initiated manual dispense, or a POST /api/dispense request:
1.	The target slot is validated: profile active, sufficient pills remaining. Failure produces an immediate error acknowledgement with no motor movement (tests F-01, F-02, E-05, E-06).
2.	The compartment RGB indicator is set purple, signalling a dispense in progress.
3.	The piezo and IR sensors for that slot are enabled and their counters reset.
4.	The compartment's stepper is driven forward through its DRV8833.
5.	The system waits for a piezoelectric impact event indicating a pill has struck the drawer, with a 10,000 ms timeout per attempt. On detection the motor is stopped.
6.	Within a 300 ms confirmation window following impact, an infrared beam-break event is required, confirming the pill physically transited the dispensing path.
7.	If both are confirmed, the pill is recorded as delivered. For multi-pill doses, steps 3 to 6 repeat until the programmed dose count is satisfied.
8.	If either is missing, the attempt fails: the motor reverses briefly (approximately 1,000 ms settle) to clear a potential obstruction, sensor state is reset, and the attempt repeats, up to three attempts.
9.	On success: the pill count is decremented, the event is timestamped and logged, a STATUS line is transmitted to the ESP32-S3, the compartment indicator turns green, an audible alert sounds, and the LCD and dashboard update.
10.	On persistent failure after three attempts: an explicit error is raised, STATUS is transmitted with result=fail, the compartment indicator turns red, and the pill count is not decremented.
The worst-case duration of a single dispense attempt sequence is approximately 35 seconds: three attempts at the 10 s piezo timeout plus reverse-settle intervals.
State 4b, Profile Management. Entered from the LCD menu via the button interface, or from the dashboard. The user creates or edits a medication profile: name (on-screen keyboard), total pill count, pills per dose, and up to four scheduled dose times per day stored as minutes since midnight. On confirmation the profile is written to RP2350 flash and mirrored to ESP32-S3 NVS.
State 4c, Settings. Time and date setting, display configuration, audio enable and volume, system information.
Return to Idle. On completion of any substate, modified state is persisted and the system returns to Idle.
4.5 Test Strategy and Governing Documents
Testing is governed by five signed test plans, submitted alongside this report, plus a supplementary Week 12 development verification record. Detailed procedures, pass criteria and signed results are recorded in those documents rather than reproduced here, keeping this report focused on design, evidence and analysis.
Table 4.3: Governing test documents
#	Document	Scope	Phases	Author
1	Integrated Electronics and Power Board Validation and Subsystem Test Plan	Visual inspection, continuity, power rails, stepper drive, sensor conditioning, battery runtime, UPS switchover	7	Alan
2	Mechanical Enclosure Validation and Subsystem Test Plan	Dimensional inspection, dovetail assembly, magnet retention, screen alignment, top assembly and container fit, drawer slide, panelling, board insertion	7	Blaise
3	Raspberry Pi Pico 2 Firmware Validation and Subsystem Test Plan	Boot and initialisation, sensor interrupts, stepper control, profile persistence, dispense engine, UART command interface, software RTC, integration	8	Alan / Blaise
4	ESP32-S3 Firmware Validation and Subsystem Test Plan	Build and flash, Wi-Fi access point, captive portal, JSON API, UART bridge, UI content, integration	7	Alan / Blaise
5	Functional Prototype Test Plan	First power-on, dashboard connectivity, sensor response, end-to-end dispense, physical use, power resilience, soak test; organised in two parts, Part A (cross-subsystem integration verification) and Part B (requirement validation)	7, in 2 parts	Alan / Blaise
6	Week 12 Development Verification Record (supplementary, not a signed test plan)	Networking, notifications, controller communication, dispensing logic, LCD, dashboard, motor control, diagnostics	(none)	Alan / Blaise

4.6 System-Level Test Coverage
The system-level plan (document 5 above) is organised in two parts, reflecting the distinction between building the system correctly and building the correct system.
Part A, Integration Verification, confirms correct interaction across subsystem boundaries on the assembled unit. Twelve tests cover: power-on and boot, inter-processor handshake, the command path from dashboard to motor, the end-to-end dispense cycle, battery switchover under load, rail stability under full mechanical load, fault detection and recovery, multi-slot independence, scheduled autonomous dispensing, profile persistence across power cycles, pickup confirmation, and logical per-station state independence.
Part B, System Validation, confirms the assembled system meets the requirements of Section 3. Twenty-four tests map one-to-one onto requirements, providing the traceability recorded in Table 3.3.
Results are reported in Section 9.1 and 9.2; the full procedures and signed records are in the standalone document.
This mirrors the approach used in Capstone II Assessments 1 and 2, which referenced the subsystem test plans rather than reproducing them, keeping the report readable and keeping each test plan a single authoritative record, rather than having procedures duplicated in two places that can drift apart.
4.7 Off-the-Shelf Component Selection
Table 4.4: Principal commercial off-the-shelf components and selection rationale
Component	Model	Key specifications	Role and selection rationale
Main controller	Raspberry Pi Pico 2 (RP2350)	Dual-core ARM Cortex-M33 @ 150 MHz; 520 KB SRAM; GPIO, PWM, I2C, UART, ADC; Pico SDK 2.2.0	Real-time execution authority. Selected for sufficient GPIO to serve three motor drivers and seven sensor inputs concurrently, a mature C SDK, on-board flash usable for profile persistence, and native USB serial for diagnostics without additional hardware.
UI controller	ESP32-S3 (Waveshare Pico form factor)	Dual-core Xtensa LX7 @ 240 MHz; 512 KB SRAM; Wi-Fi and BLE; SPI, I2C, I2S, UART; ESP-IDF v6.0	Interface and connectivity layer. Selected for integrated Wi-Fi enabling a self-hosted access point with no additional radio hardware, native I2S for the audio amplifier, and sufficient headroom for concurrent graphics and networking under FreeRTOS.
Motor driver	DRV8833 dual H-bridge module	2.7 to 10.8 V motor supply; 1.5 A per channel; overcurrent and thermal protection	Drives the 28BYJ-48 unipolar steppers; dual H-bridge topology suits four-phase drive, and integrated protection guards against stall. Five modules are fitted to serve three motors with spare capacity.
Stepper motor	28BYJ-48	5 V; integrated reduction gearbox; unipolar four-phase	One per compartment. Selected for deterministic open-loop angular positioning, the decisive advantage over the DC gear motor used in Capstone I, with adequate torque through the integrated gearbox at low cost and small envelope.
UPS module	HW-465A Type-C 15 W UPS	5 V USB-C in; 5 V/3 A out; approximately 96% efficiency; zero-delay automatic source switching; integrated overcharge, overdischarge, overcurrent and over-temperature protection	Primary power management. Consolidates charging, cell protection, boost conversion and power-path switching into a single qualified module, eliminating several discrete subsystems together with their failure modes and integration risk.
Battery cells	18650 Li-ion, 3300 mAh	3.7 V nominal; 1S2P (6600 mAh, 24.42 Wh)	Backup energy storage sized to the idle backup requirement (R-19). Rechargeable chemistry chosen over primary cells for both service life and environmental reasons (Section 10.3).
Logic regulator	LM317 adjustable linear	Adjustable; configured to 3.3 V by external divider	Derives the 3.3 V sensor supply rail. A linear topology was chosen deliberately over a switching regulator to avoid injecting switching noise onto the rail supplying the sensor signal chain, accepting the efficiency penalty in exchange for a quieter supply.
Display	ST7796S 4.0" TFT LCD	480 x 320; SPI; resistive touch overlay (XPT2046); 3.3 V logic	Primary on-device display. Size and resolution selected to permit large-format typography for the target user (G8). The touch layer is present but not used for navigation in the delivered system.
Audio amplifier	MAX98357A I2S	Digital I2S input; Class-D output; direct speaker drive	Audible alerting (R-09B). Digital I2S input avoids routing analogue audio across a board shared with motor drive currents. Powered from the 5 V rail, not the ESP32-S3 3.3 V output, see Section 7.5.
Piezoelectric sensor	Coin-type ceramic disk	Passive voltage generation on mechanical impact	Primary dispense confirmation, detects pill impact in the drawer. Conditioned through a resistive divider to the 3.3 V GPIO range.
IR beam-break	IR emitter/detector pair	Active-low on beam interruption; 3.3 to 5 V	Secondary dispense confirmation, detects pill transit through the dispensing path. Conditioned through a resistive divider.
Hall effect sensor	Omnipolar Hall effect sensor	Active-low on magnet detection; 3.3 V; low quiescent current	Drawer position detection, driving dose pickup confirmation (Section 1.5.1).

[PLACEHOLDER: add manufacturer part numbers and unit costs to Table 4.4 if you want it to double as the source for Appendix D (Bill of Materials). Datasheets go to Appendix E.]
