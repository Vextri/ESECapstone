# 6. Engineering Methodology and Test Strategy
6.1 Test Strategy
Verification and validation followed a V-model strategy: requirements decompose downward into subsystem designs, and testing builds upward from component verification through subsystem verification and system integration to system validation against the original requirements. Each level of decomposition on the left of the V has a corresponding level of test on the right.
The strategy rests on one principle: no subsystem enters integration until it has independently passed its own test plan. A fault found during integration is expensive to isolate, because any of several subsystems could be responsible. The same fault found during standalone subsystem testing is cheap, because there is only one candidate. This principle also enabled parallel development by a two-person team, since neither member's testing was blocked by the other's progress.
[INSERT FIGURE: V-model test strategy diagram]
Left descending: User Requirements, System Requirements, System Design, Subsystem Design, Component Design.
Right ascending: Component Verification, Subsystem Verification, Integration Verification, System Validation.
System Validation (L4) validates the assembled system against both System and User Requirements jointly; a separate formal user-acceptance test level was not run. Informal usability feedback from self-use and brief classmate interaction was gathered qualitatively but is not part of this formal V-model.
Horizontal links joining each design level to its corresponding test level, annotated with which of the five test plans applies at each level.
6.2 Test Levels and Governing Documents
Table 6.1: Test levels, coverage and governing documents
Level	Scope	Governing document	Owner	Section
L1, Component	Individual sensors, motors, regulators and printed parts against their specifications	Board Test Plan (Phases 1 to 5); Mechanical Test Plan (Phase 1)	Alan / Blaise	Section 8.2, 8.3
L2, Subsystem	Each functional unit in isolation, exercised through its own interface	Pico 2 Firmware Test Plan; ESP32-S3 Firmware Test Plan; Mechanical Test Plan (Phases 2 to 7); Board Test Plan (Phases 6 to 7)	Alan / Blaise	Section 8.2 to 8.5
L3, Integration	Interaction across subsystem boundaries on the assembled unit	Functional Prototype Test Plan, Part A	Both	Section 9.1
L4, Validation	Assembled system against the requirements of Section 3	Functional Prototype Test Plan, Part B	Both	Section 9.2

6.3 Test Methodology
Sequential gating. Within every test plan, phases execute in order, and a phase must pass before the next begins. The Board Test Plan is the clearest illustration: visual inspection and continuity testing complete before power is applied for the first time. Probing 5 V-to-ground isolation before energizing the board is a deliberate protection against destroying an assembled board and a full day of work through a solder bridge that a two-minute check would have caught.
Negative testing. Test cases deliberately exercise failure paths, not only success paths: dispensing with no active profile (F-01), dispensing with insufficient pills (F-02), obstructed IR (F-04), obstructed piezo (F-05), invalid slot index (E-05), inactive slot (E-06), missing epoch (E-03), and connection timeout following loss of UART traffic (U-05). A system's behaviour on the failure path is where safety is determined, and in a medication device it warrants at least as much test attention as the success path.
Instrumented observation. The USB serial diagnostic console provides timestamped visibility into sensor events, interrupt counts, motor state, dispense outcomes and inter-processor traffic. Sensor counters can be reset (R), individually enabled or disabled (P, H, I), read as a snapshot (G), or monitored continuously in real time (V), with interrupt configuration dumped for diagnosis (C). This instrumentation is a designed feature, not an afterthought, and it is what makes claims about internal system behaviour verifiable rather than inferred.
Independent measurements. Where a claim is electrical, it is verified with an instrument rather than by observed behaviour: rail voltages with a calibrated multimeter, transient behaviour and signal integrity with an oscilloscope, current draw with a meter in series with the supply, digital protocol timing with a logic analyser. Where a claim is mechanical, it is verified with calipers. Where a claim is functional, it is verified by physical outcome, pills counted by hand in the drawer, not counted by the system that is itself under test.
Traceability. Every test carries a unique identifier; every requirement traces to at least one test (Table 3.3); every test records procedure, pass criteria, measured result and date.
6.4 Pass Criteria and Sign-off
Pass criteria are defined before execution and stated numerically wherever the quantity is measurable, a voltage band, a timing window, a count, a dimensional tolerance. Criteria such as "works correctly" are not used, because they cannot be failed.
Each test plan requires that any failure be investigated and resolved before the next phase proceeds, and carries an engineering sign-off block with date. Results are recorded in the test plan tables themselves, reproduced in Appendix G as the primary record; Sections 8 and 9 present the analysis of those results.
[SIGN-OFF NEEDED] All five test plans currently have unsigned sign-off blocks. Sign and date them before submission and include the signed versions in Appendix G. An unsigned test record is, formally, not a record.

# 7. Implementation: Documentary Evidence of Prototyping and Testing
Photography standards:
- Plain, uncluttered background (a sheet of white or matte black card)
- Even, diffuse lighting; no harsh phone flash, no shadow across the subject
- In focus, correctly exposed, landscape orientation, high resolution
- No cluttered lab benches, no visible food, no unrelated hardware in frame
- Every figure captioned, numbered, and referenced in the body text before it appears
7.1 Development Timeline
Table 7.1: Capstone II development timeline
Period	Milestone	Evidence
Wks 1-3	Requirements update; architecture revision to three-compartment stepper design	Updated User Requirements and Project Plan
Wks 4-6	Functional unit development and standalone testing; prototype station build	Assessment 1; subsystem test plans
Wk 6	Full system wired on prototype station; all units functional in isolation	Assessment 1, Figure 1
Wks 7-9	Integration onto perfboard control board; enclosure Revision 1; PCB schematic capture and layout	Assessment 2
Wk 10	Enclosure Revision 2 printed and tested; audio amplifier GPIO correction; LCD header; PCB Revision 3 with 5-way button support, polygon pours, submitted to JLCPCB	W10 status report
Wk 11	Full three-station system test; ribbon cable harness for LCD; redesigned drawer; V3 right-half with button mini-housing; PCB received 20 July, components 21 July; PCB assembly begun	W11 status report
Wk 12	PCB fully soldered, tested and integrated as production board for second unit; keyed click connectors; dedicated LED board; full black reprint; new notification features; two communication reliability defects diagnosed and fixed; full end-to-end regression; code pushed to GitHub	W12 status report
Wk 13	Final assembly of the production unit (Unit 2) completed; long-duration reliability trial and final demonstration in progress	[PENDING] Photographs and dispense-reliability results to be added once the reliability trial and final demonstration are complete

7.2 Mechanical Fabrication
All mechanical components were designed in SolidWorks and fabricated by FDM 3D printing across eight iterations (Table 5.5). The final assembly was reprinted in black filament to produce a consistent, presentation-ready appearance.
[INSERT FIGURES: Mechanical fabrication evidence]
- Printed components laid out before assembly (an "exploded photograph" of the real parts, visually striking and evidences the breadth of custom mechanical work)
- Dovetail joint detail, assembled
- Dispensing mechanism, close-up
- Drawer, removed and installed
- Final assembled enclosure: front, top, left side, right side
- Enclosure open, showing internal layout of board, power module and wiring
7.3 Electronics, Perfboard Build
The first integrated build consolidated all subsystems onto a soldered perfboard (Section 5.6.2). It validated the complete electrical architecture, passed the full Board Test Plan, and remains intact as a reference build and demonstration spare.
[INSERT FIGURES: Perfboard build, top, bottom, installed in enclosure, and the button/LED mini board]
7.4 Electronics, Custom PCB
The custom two-layer PCB was designed in Altium Designer, fabricated by JLCPCB, and hand-assembled. It became the production board for the second unit.
The PCB resolved a specific physical problem the perfboard build had created. Assessment 2 recorded that pill drop site 3 was unusable due to wire congestion obstructing the dispensing area, and that the button harness ran directly across the pill exit chute, occasionally deflecting pills as they fell. Both were consequences of point-to-point wiring in a confined enclosure. Moving the button and LED control lines onto the PCB, together with keyed click connectors and a routed harness, shortened these runs and cleared the drop zone.
This is worth stating plainly because it illustrates something real about the design process: the PCB was not merely a tidier version of the perfboard, it was the solution to a mechanical interference problem that only became visible once the electronics were installed inside the enclosure. Integration surfaced a defect that neither the electronic design nor the mechanical design would have revealed in isolation.
[INSERT FIGURES: PCB build evidence, see the required set in Section 5.6.3]
Drop site 3 is confirmed resolved on the PCB build: the rerouted harness and PCB-mounted button/LED control lines cleared the wiring congestion that made the site unusable in Assessment 2. [INSERT FIGURE: photograph of the cleared drop path]
7.5 Firmware Implementation and Defect Resolution
Firmware for both processors was developed in C, version-controlled in Git, and published to a project GitHub repository. Three substantive defects were diagnosed and resolved during integration, each documented below because the diagnostic process is itself engineering evidence.
7.5.1 ESP32-S3 Subsystem Instability, Audio Amplifier Power Coupling
Symptom. During dispense commands, the LCD touchscreen would crash and reset. The fault was reproducible and correlated with dispense activity.
First hypothesis, and why it was wrong. The display was the obvious suspect, being the component that visibly failed. It was moved from the ESP32-S3 3.3 V output to the 5 V rail. The fault persisted, which was the key diagnostic result, because it eliminated the display as the root cause and redirected the investigation to the shared supply.
Root cause. The MAX98357A I2S audio amplifier, powered from the ESP32-S3's own 3.3 V output, drew current transients that the ESP32's internal regulator could not supply cleanly. A Class-D amplifier presents a highly dynamic load. The resulting supply fluctuation propagated across everything sharing that rail, including the display, and the display, being the most visible dependent, was the symptom rather than the cause.
Resolution. The audio amplifier was moved to the 5 V rail, isolating its dynamic current demand from the ESP32-S3's internal regulator. Subsystem stability was fully restored with no further crashes or glitches observed.
Engineering lesson. The component that fails visibly is not necessarily the component at fault. Where several loads share a supply, a fault in one can present as a failure in another, and the correct diagnostic move is to reason about the shared resource rather than to replace the visible symptom. The negative result, moving the display and finding the fault unchanged, was more informative than the positive one.
[INSERT FIGURE, OPTIONAL BUT HIGH VALUE] An oscilloscope capture of the 3.3 V rail during audio activity, before and after the change, would turn this from a narrative into measured evidence. If you still have a spare MAX98357A you can reproduce the fault condition on the bench in a few minutes and capture it. Two annotated traces would materially strengthen both IV2 and PA3.
7.5.2 Inter-Processor Command Corruption, Unsynchronised Concurrent Writes
Symptom. Intermittent dispense commands that were silently discarded. The user issued a dispense; nothing happened; no error was raised at either end. The intermittency and the absence of any error made this the most difficult defect encountered in the project.
Isolation. The fault was invisible to conventional debugging: the assembled enclosure leaves no room to attach a serial cable to the ESP32-S3, so the normal diagnostic channel was physically unavailable. This constraint directly motivated the construction of the in-device remote log viewer described in Section 7.5.7. With network-accessible logging in place, the fault was captured live: two separate commands were observed merging into a single corrupted message.
Root cause. Two independent execution contexts were writing to the UART transmit path concurrently, without synchronisation. When their writes interleaved, the two message lines were interwoven into one malformed line, which the receiving parser could not match against any valid command, so it discarded it. The dispense command was lost, and because the message was discarded rather than rejected, no error was generated.
Resolution. All outgoing messages were serialised, so that a complete message is transmitted atomically and no second write can begin mid-line.
Engineering significance. This defect and the ESP32-S3 shared-state mutex (Section 5.5.5) are the same class of fault, unsynchronised concurrent access to a shared resource, appearing in two different places. Recognising the pattern is the transferable lesson: any resource reachable from more than one execution context requires explicit synchronisation, whether it is a data structure in memory or a serial transmit line. The failure mode is also characteristic of the class: intermittent, load-dependent, and silent.
[INSERT FIGURE: Captured log evidence of the merged command] You have this capture. Include it with the two interleaved commands annotated, a real fault captured in a real log is far stronger evidence than a description of one.
7.5.3 Clock Synchronisation Collision During Dispensing
Symptom. Intermittent stuck dispenses, distinct from those in Section 7.5.2.
Root cause. The ESP32-S3 performs background clock synchronisation with the RP2350 on a periodic basis. The RP2350's dispense routine, however, is occupied for the duration of a dispense cycle, up to approximately 35 seconds in the worst case, and cannot service incoming messages during it. A synchronisation message arriving inside that window was therefore transmitted into a receiver that could not accept it, leaving the exchange incomplete and the dispense sequence stalled.
Resolution. Background clock synchronisation is now suppressed while a dispense is in progress, removing the collision window entirely. Synchronisation resumes on completion.
Engineering significance. This is a genuine concurrency defect, and it was only reachable under integration, neither processor exhibits it in isolation, because it arises from the timing relationship between two independently correct behaviours. That property is precisely the justification for maintaining integration as a distinct test level in the V-model of Section 6.1: a class of defect exists that subsystem testing cannot find by construction, however thorough that testing is.
Verification: the fix was exercised over 15 dispense cycles with background clock synchronisation active throughout, with no recurrence of the stall observed.
7.5.4 Socket Exhaustion Under Multi-Client Load
Symptom: The dashboard became unreachable within seconds of a second device connecting.
Isolation: The remote log viewer captured ENFILE errors, indicating the operating system had no file descriptors available; these appeared within seconds of a second device joining.
Root cause: Each connected browser holds several concurrent sockets rather than one. The configured system socket budget of 10 was sized against an implicit single-client assumption and was exhausted almost immediately by a second client. Measured peak usage across two devices was 7 concurrent sockets.
Resolution: The socket budget was raised from 10 to 16, and stale-connection eviction was enabled so that connections no longer live are released rather than held. Re-tested: the error no longer reproduces, and two simultaneous devices are served cleanly with no accept() failures.
Engineering significance: The original limit was not obviously wrong, it was wrong against an assumption that had never been stated. This is characteristic of resource-budget defects, and the general lesson is that an implicit single-user assumption in a networked device becomes a hard failure the first time a second user appears. For a device that a patient and a caregiver may both reasonably want to view, single-client operation was never the real requirement, and it took a second device to expose that the design had assumed it.
7.5.5 Cross-Station State Corruption
Two related defects were found in which per-station state was not properly isolated:
-	Reminder tracking. A pending pickup reminder for one station could be wrongly cancelled after a different station dispensed. The consequence is clinically significant: the reminder for an uncollected dose would be suppressed, defeating the escalation mechanism precisely when it was needed.
-	Pickup state. A failed dispense at one station could clear another station's genuine pending-pickup state.
Both were resolved by tracking pickup and reminder state independently per station. Drawer pickup confirmation was additionally changed to always attempt confirmation on drawer opening, rather than depending on a flag that could be stale.
Engineering significance. Both faults share a root pattern with test U-01, which verifies sensor independence across slots (Section 8.4). Physical sensor independence had been explicitly verified; logical state independence had not, and it turned out not to hold. The lesson is that multi-instance independence must be verified at every layer that holds per-instance state, not only at the hardware layer where it is most visible.
A validation test for logical per-station state independence has been added to Part A of the Functional Prototype Test Plan (INT-12), alongside the existing U-01 sensor independence test.
7.5.6 Blocking Notification Send, Device-Wide Freeze
Symptom: Sending a push notification froze the entire device for several seconds. The LCD stopped responding, the dashboard stopped serving, and, most seriously, dispensing was blocked for the duration.
Root cause: The notification send was performed synchronously on a path that also carried time-critical work, so the network operation's latency propagated into every other function.
Resolution: Notification sending was moved to a background context. The freeze no longer occurs.
Engineering significance: This defect deserves particular emphasis in the report because of what it blocked. A notification is a convenience feature; dispensing is the device's safety-relevant core function. Allowing a convenience feature to block a safety-relevant one is an architectural error rather than merely a performance one, and it is the same class of concern that motivated the dual-microcontroller partition in the first place (Section 4.1). The partition protects the RP2350's dispensing loop from the ESP32-S3's network work, but it cannot protect the ESP32-S3's own time-sensitive responsibilities from a blocking call made on the wrong context. This is a genuinely useful point to make explicitly: the architecture correctly isolated the two processors, and this defect showed that isolation between processors does not remove the need for isolation within one.
7.5.7 Remote Diagnostic Log Access
The assembled enclosure leaves no room to attach a serial cable to the ESP32-S3, which removed the primary diagnostic channel at exactly the point in the project when integration defects were emerging. In response, an in-device log viewer accessible over the network was built, together with connection event logging that records every device join and leave and every dashboard connection open and close, with address and memory state.
This tooling directly enabled the diagnosis of the command corruption defect (Section 7.5.2) and the socket exhaustion defect (Section 7.5.4), neither of which was reachable by other means once the unit was assembled.
Engineering significance: Building diagnostic infrastructure is not incidental work; it is what made the remaining defects findable. It is also a design-for-serviceability lesson learned the hard way: the enclosure design removed a diagnostic capability that the electronics design depended on, and neither discipline would have caught that in isolation. This is the second instance in the project of a defect that appeared only at the mechanical-electronic boundary, the first being the wiring obstruction of drop site 3 (Section 7.4).
7.5.8 Firmware Feature Completion
The final firmware revision added: low-pill-count alerts at 1 and 0 pills remaining; a dose-dispensed notification for scheduled dispense events; clearer on-screen slot naming; an on-screen keyboard for medication name editing; a digit-based schedule editor; and clearer button-hint text. The web dashboard's appearance and its dispense and notification features were also revised.
[INSERT FIGURE: GitHub repository] A screenshot of the repository showing commit history evidences version control practice and sustained, distributed development effort. Include the repository URL in Appendix C.
7.6 Final Integrated Assembly
Two complete units were produced:
●	Unit 1, Perfboard build. Soldered perfboard control board with header-mounted modules and simple push buttons. Fully validated; retained as the reference build and demonstration spare.
●	Unit 2, PCB build. Custom Altium-designed PCB with the 5-way navigation switch, keyed click connectors and organised harness routing, installed in the final black V3 enclosure. This is the primary deliverable.
Retaining Unit 1 in working order was a deliberate decision: it provided a known-good reference against which to compare during PCB bring-up, so any fault appearing on the new board could be isolated to the board rather than to the firmware or the mechanism.
[INSERT FIGURES: Final assembly]
- Both units side by side (evidences the progression)
- Unit 2 exterior, front three-quarter, powered on with the LCD showing live status, this is the hero image, put it on the cover page too
- Unit 2 with the enclosure open, showing internal integration
- Detail of the front panel: LEDs, navigation switch, drawer
- The drawer open with dispensed pills present
7.7 Demonstration Video
[PLACEHOLDER: REQUIRED DELIVERABLE, not yet produced]
The guideline requires a project video (Section 5.5), and the final presentation requires a 2 to 3 minute elevator pitch video covering: the problem, the solution with its innovative aspects, and future direction including commercialisation potential.
Insert the link here and in Appendix C. A suggested structure for the demonstration footage:
1. Problem framing (15 s), the pharmacy container and the pill organiser
2. The device, powered on, idle screen (15 s)
3. Profile configuration on the LCD via the buttons (30 s)
4. A scheduled dispense executing autonomously, the money shot, show the LED go purple then green, the pill drop, the audible alert, and the count decrement (45 s)
5. A failed dispense with retry and red error, deliberately obstruct the path. Showing the failure handling working is more persuasive than showing only success (30 s)
6. The web dashboard on a phone, with the phone's mobile data visibly disabled to prove offline operation (20 s)
7. Battery switchover, unplug it mid-operation and show it keeps running (15 s)
Film in landscape, with the device well lit and the audio alert audible.
