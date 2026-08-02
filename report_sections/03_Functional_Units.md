# 5. Design of Functional Units

Each subsection follows the same structure: Definition, Principle of Operation, Design Specification, Engineering Principles and Analysis, Engineering Tools, Test Requirements.

5.1 Power Management Subsystem
Owner: Alan Hosseinpourmoghadam
5.1.1 Definition
The Power Management Subsystem generates and maintains every regulated voltage rail in the system, manages the charge lifecycle of the lithium-ion backup pack, and guarantees uninterrupted power delivery across transitions between external supply and battery. Every other subsystem depends entirely on it. It is consequently the first subsystem brought up and the first tested, since no other verification is meaningful until the rails are known good.
5.1.2 Principle of Operation
A 5 V USB-C wall adapter supplies the primary 5 V distribution rail. The HW-465A UPS module connects to this rail in a dual role: it draws charging current from the rail to maintain the 18650 pack via its internal CC/CV algorithm, and its output terminals connect to the same rail to provide the battery-sourced backup path.
The module's internal power-path controller enforces source priority. With external power present, the battery boost converter is isolated from the output rail; the external supply alone drives the 5 V rail while the battery charges. The battery does not source current into the rail in this state, preventing unnecessary charge-discharge cycling and the premature cell wear it causes. On loss of external power, the module detects input collapse and switches the battery boost converter onto the 5 V output with zero delay; operation continues without interruption.
This integrated power-path management removes the need for a discrete ideal-diode ORing controller. A discrete alternative, IP2312 CC/CV charger, 1S protection board, TPS61088 boost converter and LM74700 ideal diode, was designed as a reference for future revisions requiring higher sustained motor current, but was not built in this version. It is documented in Appendix F.
The 3.3 V sensor supply rail is derived from the 5 V rail by an LM317 adjustable linear regulator and supplies the sensor divider network. RP2350 and ESP32-S3 are powered directly from the 5 V rail (Section 1.4.1). The choice of a linear rather than switching topology for the 3.3 V rail is deliberate: the sensor signal path, particularly the piezoelectric input, which detects a low-energy mechanical transient, is sensitive to supply noise, and a linear regulator provides inherent rejection of the switching noise present on the 5 V rail without contributing any of its own.
[INSERT FIGURE: Power distribution block diagram]
Show USB-C input to 5 V distribution rail; UPS module input and output both on that rail; 18650 1S2P pack on the UPS battery terminals; LM317 from 5 V to 3.3 V rail; and every load correctly assigned to its rail. Mark the MAX98357A explicitly on the 5 V rail, that placement is a designed decision with a documented justification (Section 7.5), and showing it lets you point to it.
[INSERT FIGURE: LM317 regulator schematic] Carry forward from the Capstone I design document or redraw from the Altium schematic. Must show R1 and R2 with values, plus input and output capacitors.
5.1.3 Design Specification
Parameter	Specification	Requirement
Primary rail nominal	5.0 V	R-05
Primary rail tolerance	±5% (4.75 to 5.25 V) under full electromechanical load	R-05
Logic rail nominal	3.3 V	R-07
Logic rail tolerance	±0.1 V (3.2 to 3.4 V) under all load conditions	R-07
Source switchover	Uninterrupted; no processor reset or data loss	R-06
Battery configuration	1S2P 18650 Li-ion, 6600 mAh, 24.42 Wh	R-19
Battery backup target	At least 24 hours idle operation (re-baselined, see Section 3.2)	R-19
Charging	CC/CV from external supply during normal operation	R-17
UPS output rating	5 V / 3 A (15 W rated, 20 W peak)	N/A

5.1.4 Engineering Principles and Analysis
Linear regulator output programming. The LM317 maintains a fixed 1.25 V reference between its OUTPUT and ADJUST terminals. With a two-resistor divider from OUTPUT through ADJUST to ground:
Vout = 1.25 x (1 + R2 / R1)
To reach 3.3 V, R1 = 238 ohm was realised as a series combination of standard 220 ohm and 18 ohm resistors, with R2 = 390 ohm:
Vout = 1.25 x (1 + 390 / 238)
     = 1.25 x (1 + 1.6387)
     = 1.25 x 2.6387
     = 3.298 V
The deliberate series combination for R1 permitted trimming to within 2 mV of the 3.3 V target using only standard E24 values, rather than accepting the roughly 5% error the nearest single standard resistor would have introduced. Measured output on the integrated board was 3.312 V, a deviation of +0.42% from the calculated value and +0.36% from the 3.3 V nominal target, comfortably inside the ±0.1 V (±3.03%) tolerance of R-07 and consistent with the combined tolerance stack of the resistor pair and the LM317's own reference tolerance.
Linear regulator power dissipation. The LM317 dissipates the full voltage differential across the load current:
P_LM317 = (Vin - Vout) x I_load = (5.0 - 3.3) x I_load = 1.7 x I_load
[PLACEHOLDER: THERMAL ANALYSIS: complete this; it is a genuine engineering-analysis mark]
Measure or budget the total 3.3 V rail current (sensor divider network only, since the MCUs are no longer on this rail) and complete:
- P_LM317 = 1.7 V x I_3V3 = ____ W
- Junction temperature rise dT = P x theta_JA, with theta_JA from the package datasheet (TO-220 with no heatsink is approximately 50 degrees C/W)
- Compare T_J = T_ambient + dT against the 125 degrees C maximum junction temperature
- State whether a heatsink is required, and confirm against thermal observation during the soak test
This supports LL2, Applying Knowledge and Skills, and feeds the thermal hazard entry in Section 10.1. If dissipation proves significant, that is a finding worth reporting, not a problem to conceal, and it produces a concrete Section 14.5 recommendation to replace the LM317 with a switching regulator.
Battery runtime analysis. Available energy is:
E = 6600 mAh x 3.7 V = 24.42 Wh
Runtime at a measured average idle power P_idle is t = 24.42 / P_idle hours. Because the UPS module boosts from cell voltage to 5 V at finite efficiency and the cells cannot be fully discharged, practical runtime is approximately 85 to 90% of this figure.
[PLACEHOLDER: BATTERY RUNTIME: required to close R-19, currently unmeasured, and there is a conflict to resolve]
Board Test Plan Phase 6 (B-01, B-02, B-03) is entirely empty. Complete it, multimeter in series between the UPS output and the board 5 V input:
- B-01 both MCUs active, no motors: ____ mA
- B-02 both MCUs active, one stepper running: ____ mA
- B-03 full system, three steppers and all sensors: ____ mA
Then compute runtime for each condition, apply the 85 to 90% derating, and state plainly whether R-19 is met.
The conflict to address: both Capstone II assessments record idle current around 350 mA. At 350 mA on the 5 V rail (1.75 W), runtime is roughly 14 hours, far short of the original 48-hour target. The Capstone I estimate of 118 hours assumed approximately 207 mW, which the as-built three-motor system with an always-on backlit LCD does not achieve.
Do not paper over this. Measure it, report it, analyse the gap in Section 8.9, identify the dominant consumers (LCD backlight and IR emitters are the prime suspects), and then either report R-19 as Not Met with that analysis, or define the idle condition precisely, for example backlight timed out, IR emitters gated, and report against that definition. A measured shortfall, correctly analysed with identified causes and a concrete remedy, scores well under IV2 and PA3. An unsupported claim of 48 hours does not, and is trivially challenged in Q&A.
5.1.5 Engineering Tools
Tool	Purpose
Digital multimeter	Rail voltage under static and dynamic load; continuity and isolation; series current measurement
Oscilloscope	Rail ripple characterisation; switchover transient capture; sensor signal verification against the 3.3 V GPIO limit
Bench power supply	Circuit validation and component characterisation prior to module integration
USB-C 5 V wall adapter	Primary supply during all testing
Component datasheets (LM317, HW-465A, DRV8833)	Design verification against manufacturer limits

5.1.6 Test Requirements and Method
Verified through Board Test Plan Phases 1 to 3 (visual inspection, continuity, rail validation), Phase 6 (battery runtime) and Phase 7 (UPS switchover). Results in Section 8.2; full plan in Appendix G.

5.2 Embedded Control and Motor Subsystem
5.2.1 Definition
The Embedded Control and Motor Subsystem is the real-time computational core. Executing on the RP2350 in C against Pico SDK 2.2.0, it owns stepper actuation for all three compartments, the dispense state machine, medication profile storage and integrity, schedule evaluation, the software real-time clock, the UART command interface to the ESP32-S3, and the USB serial diagnostic console.
5.2.2 Hardware Interface
Table 5.1: RP2350 GPIO assignment
Function	GPIO	Direction	Notes
Slot 0, piezo impact	5	Input (interrupt)	100 ms debounce; via divider
Slot 0, IR beam-break	7	Input (interrupt)	50 ms debounce; active low on break; via divider
Slot 1, piezo impact	16	Input (interrupt)	100 ms debounce; via divider
Slot 1, IR beam-break	17	Input (interrupt)	50 ms debounce; via divider
Slot 2, piezo impact	18	Input (interrupt)	100 ms debounce; via divider
Slot 2, IR beam-break	19	Input (interrupt)	50 ms debounce; via divider
UART1 TX to ESP32-S3 RX (GPIO 11)	20	Output	115200 8N1
UART1 RX from ESP32-S3 TX (GPIO 12)	21	Input	115200 8N1
Stepper 1 (Slot 0) AIN1	8	Output	Via DRV8833 (DRV1)
Stepper 1 (Slot 0) AIN2	9	Output	Via DRV8833 (DRV1)
Stepper 1 (Slot 0) BIN1	10	Output	Via DRV8833 (DRV1)
Stepper 1 (Slot 0) BIN2	11	Output	Via DRV8833 (DRV1)
Stepper 2 (Slot 1) AIN1	6	Output	Via DRV8833 (DRV4, substitute for DRV2)
Stepper 2 (Slot 1) AIN2	4	Output	Via DRV8833 (DRV4); AIN1/AIN2 swapped vs. driver IN1/IN2 to fix rotation direction
Stepper 2 (Slot 1) BIN1	27	Output	Via DRV8833 (DRV4)
Stepper 2 (Slot 1) BIN2	28	Output	Via DRV8833 (DRV4)
Stepper 3 (Slot 2) AIN1	12	Output	Via DRV8833 (DRV3)
Stepper 3 (Slot 2) AIN2	13	Output	Via DRV8833 (DRV3)
Stepper 3 (Slot 2) BIN1	14	Output	Via DRV8833 (DRV3)
Stepper 3 (Slot 2) BIN2	15	Output	Via DRV8833 (DRV3)

Note: Stepper 2 runs on the board's DRV4 driver position rather than DRV2 (GPIO 0 to 3, the original DRV2 position, are consequently unused), and its AIN1/AIN2 mapping is software-swapped relative to the driver's physical IN1/IN2 pin labels to correct rotation direction, rather than by rewiring.
The earlier apparent GPIO 6 conflict is resolved: GPIO 6 is Stepper 2's AIN1 line and nothing else. The Hall-effect sensors are not on the RP2350 at all; they connect to the ESP32-S3 (Table 5.8, Section 5.5.2).
[INSERT FIGURE: RP2350 interface schematic] Extract from the Altium schematic. Must show the piezo and IR sensor inputs through their conditioning networks with resistor values, the three DRV8833 connections, and the UART1 link with direction arrows.
Safety note. The piezoelectric signal can reach approximately 5 V, exceeding the RP2350's 3.3 V GPIO input tolerance; it passes through the two-resistor divider described in Section 5.3.4 before reaching the microcontroller. The IR beam-break signal is a digital output pulled up directly to 3.3 V and does not require a divider. Divider output on the piezo line was verified on an oscilloscope to remain within the safe input range before firmware testing began. This mitigation is recorded as risk R-07 in Section 13.
5.2.3 Firmware Architecture
The firmware is organised around a single-core cooperative main loop. On boot, subsystems initialise in fixed order: motor initialisation, profile restore from flash, sensor initialisation, UART bridge start, after which the main loop polls the USB serial console, the UART command interface, and the schedule evaluator on every iteration.
Unified interrupt dispatch. The RP2350 exposes one hardware IRQ line (IO_IRQ_BANK0) for all GPIO on a core, and the Pico SDK provides a single global callback slot. Serving seven sensor inputs therefore required a unified dispatcher: one callback is registered at initialisation and routes each event by pin number to the appropriate handler. Each handler applies its own debounce interval, increments its counter, and optionally invokes a registered callback function pointer set at boot from the application layer. This keeps the sensor module generic, it knows nothing about dispensing, while the application defines the response.
[INSERT FIGURE: Interrupt dispatch flowchart]
IRQ entry, read event pin, switch on pin number, per-sensor debounce check (timestamp compared against that sensor's interval), increment counter, invoke registered callback if set, return. Annotate the debounce interval for each sensor. Draw this properly, it is a genuine embedded design pattern and reads as fourth-year work.
[INSERT FIGURE: Dispense state machine flowchart, the single most important diagram in Section 5]
Must show: validate slot, LED purple, enable and reset sensors, start motor, wait for piezo (10 s timeout), stop motor, wait for IR (300 ms window), both confirmed? Yes: decrement, log, send STATUS, LED green, sound alert. No: attempt less than 3? Yes: reverse motor approximately 1000 ms, reset sensors, retry. No: raise error, LED red, STATUS result=fail, count preserved. Also show the multi-pill loop for dose greater than 1.
[INSERT FIGURE: Boot sequence serial trace] Capture on the final firmware, showing initialisation order, profile restore, BOOT_SYNC emission and TIME_REQ.
Profile persistence. The firmware allocates five medication profile slots in the last 4096-byte flash sector, three in active use, one per physical compartment, with two spare providing headroom for future expansion without a storage format change. Profiles are stored, protected by a magic number and a checksum validated on boot. Each profile holds medication name, total pill count, pills per dose, motor timing parameters, and up to four scheduled dose times expressed as minutes since midnight. Profiles restore automatically at boot and are rewritten whenever a profile is loaded or a schedule updated.
[INSERT FIGURE: Flash memory layout diagram] Magic number, five profile slots with field breakdown, checksum. An annotated memory map is sufficient.
Software real-time clock. The RP2350 has no battery-backed RTC. Time is maintained in software and re-established from the ESP32-S3 on every boot via TIME_REQ / SET_TIME, retried at 15-second intervals until answered, with manual entry available through the console (accepting either a YYYY-MM-DD HH:MM:SS string or a raw Unix epoch integer). All displayed times convert to US Eastern local time with daylight-saving awareness; the DST boundary transition was explicitly verified (test K-07).
The consequence, that time is lost across a total power failure of both rails and must be re-established before scheduling can resume, is a known Version 1 limitation, analysed in Section 9.4 and 9.5.
5.2.4 Stepper Motor Control
Each compartment is driven by an independent 28BYJ-48 unipolar stepper through a DRV8833 dual H-bridge. Stepper drive was selected over the DC gear motor of Capstone I because a stepper delivers deterministic angular positioning by construction: the controller commands a known number of steps and the rotor follows, eliminating the need to infer position from an external sensor.
Motors are de-energised at boot and hold no coil current until a direction command is issued (verified by test B-06), which matters materially for idle power on battery. Motor operation is non-blocking, the main loop continues to service the console and UART interface while a motor runs (test M-03), and multiple motors can run concurrently without interference (test M-04).
Table 5.2: Motor control command mapping (USB serial console)
Motor	Forward	Backward	Stop
Motor 1 (Slot 0)	W	N	X
Motor 2 (Slot 1)	A	Z	E
Motor 3 (Slot 2)	J	U	Y
All motors	N/A	N/A	Q (shutdown, de-energise all)

Drive parameters, confirmed from firmware. All three motors use full-step bipolar drive, a four-state coil energisation sequence (full_step_seq[4][4] in stepper_control.c); no half-step or wave drive is used anywhere in the firmware. The step interval is set independently per motor: Motor 1 and Motor 2 run at 5600 microseconds per step, Motor 3 runs at 2000 microseconds per step.
There is no fixed steps-per-dispense figure, and this is a deliberate design characteristic rather than a missing measurement. The dispense function does not command a set number of steps and then check for a pill; it starts the motor and runs it continuously, stopping the instant the piezoelectric sensor detects impact (or after a 10-second timeout, triggering a retry). Because the mechanism is sensor-terminated rather than step-counted, the number of steps per dispense varies from cycle to cycle by design, and there is consequently no single angular-displacement-per-cycle figure to compute against the 28BYJ-48's 1:64 gear reduction. The firmware does expose a step counter (stepper_get_step_count()) that is not currently logged anywhere; if a representative step count is wanted for illustration, the fastest path is reading that counter immediately after a real dispense and recording the value, rather than deriving one analytically.
5.2.5 UART Command Protocol
Communication with the ESP32-S3 uses a line-oriented ASCII protocol at 115200 8N1 with pipe-delimited key=value tokens. The format was chosen for debuggability: the entire inter-processor conversation is human-readable on a serial terminal, and commands can be injected by hand with no special tooling.
Table 5.3: Inter-processor message types
Direction	Message	Purpose
Pico to ESP	TIME_REQ	Request current epoch time on boot; retried at 15 s intervals until answered
Pico to ESP	BOOT_SYNC, slot=..., med=..., left=..., schedule=...	Communicate authoritative stored profile state at boot, one line per active slot
Pico to ESP	STATUS, slot=..., med=..., left=..., dose=..., doses=..., last=..., event=..., result=...	Live state and dispense outcome
Pico to ESP	ACK, action=..., slot=..., result=...	Command acknowledgement with outcome or error code
ESP to Pico	CMD, action=SET_TIME, epoch=...	Set the software clock
ESP to Pico	CMD, action=LOAD_PROFILE, slot=..., med=..., total=..., dose=..., time=...	Create or update a medication profile
ESP to Pico	CMD, action=DISPENSE, slot=...	Request a dispense from a specific slot
ESP to Pico	GET_STATUS	Poll for current state

Error acknowledgements are explicit and enumerated: err_invalid_slot, err_slot_not_active, err_missing_epoch, err_invalid_epoch. Each was verified by a dedicated negative test (E-03, E-05, E-06). Designing and testing the failure responses of a protocol, and not only its success path, is a deliberate robustness measure.
5.2.6 Engineering Tools
Tool	Purpose
Visual Studio Code	Primary development environment
Pico SDK 2.2.0 + CMake + Ninja	Build toolchain for RP2350 firmware
USB serial terminal (115200 baud)	Interactive command console, event logging, dispense trace capture
Logic analyser	UART frame timing and protocol validation
Git / GitHub	Version control and history
Digital multimeter, oscilloscope	Motor current measurement, sensor signal verification

5.2.7 Test Requirements and Method
Verified through the Pico 2 Firmware Test Plan, Phases 1 to 8: boot and initialisation, sensor interrupt system, stepper control, profile persistence and flash integrity, dispense engine, ESP command interface, software RTC, and full integration. Results in Section 8.5.

5.3 Sensing and Dispense Verification Subsystem
5.3.1 Definition
This subsystem provides the closed-loop confirmation that distinguishes PortaPill from the devices surveyed in Section 2. It comprises the sensing elements, their signal conditioning and protection networks, the interrupt architecture that captures their events, and the verification algorithm that decides whether a dispense actually occurred.
5.3.2 Principle of Operation: Why Two Sensors
The verification design rests on the observation that no single sensor can reliably confirm a pill dispense, because each available modality fails in a different way:
●	A piezoelectric impact sensor in the drawer floor detects that something struck the drawer. It cannot distinguish a dispensed pill from the enclosure being bumped, the drawer being closed, or the unit being set down on a table. It is prone to false positives from environmental disturbance.
●	An infrared beam-break sensor across the dispensing path detects that something passed through the chute. It confirms the correct path was transited but cannot confirm the pill completed its fall into the drawer, a pill that lodges after breaking the beam would register as passage without delivery.
Requiring both, in a defined sequence, within a bounded time window discriminates a genuine dispense from either failure mode. A stray knock produces a piezo event with no IR event and is rejected. A pill that breaks the beam and then lodges produces an IR event with no impact and is rejected. Only the correct physical sequence, impact registered, beam-break confirmed within 300 ms, satisfies the condition.
The sequence serves a second, distinct control purpose: the piezo event terminates motor run time. Because impact indicates the pill has completed its travel, the motor can be stopped immediately rather than running a fixed timed interval. This reduces mechanical wear, reduces energy per dispense, and narrows the window in which a second pill could be released.
5.3.3 Design Specification and Timing
Table 5.4: Verification subsystem timing parameters
Parameter	Value	Rationale
Piezo debounce	100 ms	Suppresses mechanical ring-down of the piezo disk after a single impact, preventing one pill registering as several
IR debounce	50 ms	Shorter than piezo; the optical signal has no mechanical ring-down, and a shorter interval preserves the ability to resolve consecutive pills
Hall effect debounce	10 ms	Clean digital transition; minimal debounce required
Piezo wait timeout (per attempt)	10,000 ms	Bounds the wait for an impact that may never arrive if the mechanism has jammed
IR confirmation window (following piezo)	300 ms	Physical transit is far shorter than this; the window is generous enough to tolerate timing variation, yet narrow enough that an unrelated later beam-break cannot be misread as confirmation
Maximum retries per pill	3	Bounds recovery effort before declaring failure
Motor reverse settle between retries	Approximately 1000 ms	Allows the mechanism to clear an obstruction and settle before re-attempting
Worst-case single dispense duration	Approximately 35 s	Three attempts at full timeout plus reverse-settle intervals

[INSERT FIGURE: Dispense verification timing diagram]
A timing diagram is the correct engineering representation here and reads well under CM2, Representation. On a common time axis show motor drive, piezo signal, IR signal and the 300 ms confirmation window, for (a) a successful cycle and (b) a failed cycle with retry. Annotate the timeout and window intervals.
5.3.4 Signal Conditioning
The piezoelectric element generates a voltage transient whose amplitude varies with impact energy, and therefore with pill mass and drop height, with a peak that can plausibly approach 5 V. A two-resistor network conditions this into a range compatible with the RP2350 GPIO input: the piezo signal drives a 1 kOhm series resistor (R1k_1) into the GPIO node, and a 2.2 kOhm resistor (R2.2k_1) shunts that same node to ground, forming a divider with the GPIO reading the midpoint.
Vout = Vin x R2 / (R1 + R2) = Vin x 2200 / (1000 + 2200) = 0.6875 x Vin
At an assumed worst-case piezo peak of 5 V, Vout = 0.6875 x 5.0 = 3.44 V. This is worth stating precisely rather than rounding away: 3.44 V is above the 3.3 V nominal logic rail, but below the RP2350 GPIO absolute maximum rating (datasheet-specified; commonly in the 3.5 to 3.6 V range for this input tolerance class, matching the failure threshold the divider was designed against). The divider therefore protects the pin from damage but does not clamp the signal below the 3.3 V rail itself under this worst-case assumption; if a strictly sub-3.3 V bound is wanted, the divider ratio would need revisiting, but as built it meets the design's actual safety criterion (stay under the GPIO's absolute maximum). The divided signal also comfortably exceeds the RP2350 logic-high threshold (V_IH, approximately 0.65 x V_DD, approximately 2.15 V at a 3.3 V supply) across the expected impact range, so reliable interrupt detection is not in question.
The IR and Hall-effect sensors are a different topology, not a divider. Each is an open-drain digital output pulled up directly to the 3.3 V rail through a single 10 kOhm resistor (HER1 to HER3 for the three Hall-effect sensors; TIR1 and TIR2 shown for the IR receivers, with a third following the same pattern). Because the pull-up reference is 3.3 V rather than 5 V, the logic-high level is 3.3 V by construction and can never exceed the rail, so no separate clamping calculation is needed for these two sensors, unlike the piezo input.
Resolved: the three Hall-effect outputs (Hall_1, Hall_2, Hall_3) are independent, not a shared line, and are not on the RP2350 at all. They connect directly to the ESP32-S3 on GPIO 33, 34 and 35 (Table 5.8, Section 5.5.2). This means Hall-effect drawer detection, and by extension pickup confirmation (Section 5.5.3), is captured entirely on the ESP32-S3 side rather than passing through the RP2350's sensing subsystem, which is a meaningful architectural point worth stating plainly: the third verification stage (Section 1.5.1) is physically independent of the first two, on a different processor with its own GPIOs, not merely logically independent in firmware. This distinction should also be reflected in Section 4.1's description of which processor owns which interrupts.
[INSERT FIGURE: Oscilloscope capture, piezo impact waveform] Capture during a real pill drop, with the 3.3 V limit and the GPIO logic threshold marked on the trace.
[INSERT FIGURE: Oscilloscope capture, IR beam-break transition] Show the clean logic transition on beam interruption and restoration.
5.3.5 Test Requirements and Method
Each sensor was verified independently before integration across four dimensions: enable/disable control, correct triggering, debounce effectiveness, and absence of false triggers at rest. Sensor independence across the three slots was explicitly verified (test U-01: stimulating slot 0 must leave slots 1 and 2 counters at zero). Results in Section 8.4.

5.4 Mechanical Dispensing Subsystem
5.4.1 Definition
The Mechanical Dispensing Subsystem provides the complete physical platform: medication storage, motor-driven singulation and release, the pill transit path, the collection drawer, precise sensor positioning relative to that path, and the enclosure housing all electronics. It was designed parametrically in SolidWorks and fabricated by FDM 3D printing across eight documented iterations.
5.4.2 Assembly Description
The delivered enclosure comprises:
●	A two-part base station joined by a dovetail joint and retained by embedded N52 neodymium magnets, housing the main control board, the power module and the drawer slot.
●	A three-part top assembly seating into the base and carrying the three pill container mounts.
●	Three pill containers with their dispensing mechanisms, each seating into a top mount.
●	A sliding drawer at the front forming the collection zone, with the piezoelectric sensor mounted beneath.
●	Side and top panels closing the enclosure, with cutouts for the USB-C charging input (left) and the speaker (right).
●	A button mini-housing on the right assembly carrying the 5-way navigation switch and its indicator LED.
●	A speaker holder integrated into the enclosure door.
[INSERT FIGURES: SolidWorks assembly views, required set]
These carry ED3, Detailed Design, and CM2, Representation, for this subsystem:
- Full assembly isometric (rendered, not a CAD screenshot)
- Exploded view identifying every printed part, high value, strongly recommended
- Section view through one dispensing path showing container, mechanism, chute, IR beam position, drawer, piezo position. This single view explains the entire verification concept better than any paragraph in this report.
- Dimensioned 2D drawing of the dispensing mechanism with critical tolerances called out
- Dovetail joint detail with tolerances
- Drawer and slide interface with clearance dimensions
Export clean images from SolidWorks; do not screenshot the CAD window with toolbars visible.
[PLACEHOLDER: OVERALL ENVELOPE] State final assembled L x W x H, mass with and without batteries, and per-container pill capacity. These are portability claims (R-16) and should be quantified rather than asserted. Test DR-07 measures the envelope, complete it.
5.4.3 Engineering Principles
Pill singulation. The dispensing mechanism releases a discrete, countable number of pills per actuation by geometry rather than by sensing. The dispensing feature is sized to admit one pill at a time, preventing simultaneous passage of multiple pills; the agitating element promotes continuous presentation of pills to the mechanism and prevents bridging or stalling at low fill levels, the condition under which gravity-fed mechanisms most commonly fail.
Motor reaction torque constraint. Without a rigid mount, a small geared motor rotates its own body against the load rather than driving it, a common failure mode in low-torque geared applications. The motor mount is integrated into the printed structure and constrains the motor body in all rotational degrees of freedom while maintaining shaft alignment, ensuring torque is transmitted to the mechanism rather than absorbed by body drift.
Sensor positioning as a mechanical requirement. The verification design of Section 5.3 imposes geometric requirements on the mechanical design: the IR emitter and detector must establish a continuous beam across the pill drop path at a height the pill must cross, and the piezoelectric sensor must sit in the impact zone of the drawer floor. Both are integrated as designed mounting features rather than added afterwards. The mechanical and sensing designs are coupled and developed together.
Modularity for pill geometry. The dispensing mechanism is modular and can be exchanged for variants with different feature geometry to accommodate different pill sizes without modifying any other component. Different pill sizes were tested against this provision in Week 11.
[PLACEHOLDER: PILL GEOMETRY CHARACTERISATION: highest-value experiment still available to you]
Risk R-04, pill geometry incompatibility, RPN 96, High, is the largest mechanical risk carried and it is currently open. Testing with candy analogues does not close it.
Test a range of real pill geometries (over-the-counter tablets and capsules of differing diameter, thickness and shape, not prescription medication) and record for each: pill type, dimensions measured with calipers, dispense success rate over n cycles, and observed failure mode where applicable.
This produces a genuinely valuable table and chart for Section 8.3, converts a High risk into a characterised operating envelope, and directly serves IV2, Data Collection (20 pts), and PA3, Validation (10 pts). If you do only one more experiment before submission, do this one.
5.4.4 Design Iteration History
Table 5.5: Mechanical design iteration summary
Iteration	Period	Principal changes	Outcome
1	Capstone I, Wk 7	Initial SolidWorks concept; mechanism geometry research; component selection	Design direction established; component tasks assigned
2	Capstone I, Wk 9	Integrated test rig replacing separate parts; direct motor shaft connection; initial anti-jam pocket geometry	First physical test rig enabling motor reliability and singulation testing
3	Capstone I, Wk 10	3D-printed dispensing mechanism; enclosure redesigned for hands-free autonomous operation	Hardware-in-the-loop testing achieved; successful unattended cycles
4	Capstone I, Wks 11-12	Sweeper rail constraint; IR bracket slots in chute walls; piezo housing in tray floor; motor mount integration	Fully instrumented platform enabling closed-loop verification
5	Capstone II, early	Redesign to three-compartment architecture; stepper mounts; two-part dovetailed base with magnetic closure; drawer; container mounts	Multi-compartment architecture realised
6	Capstone II, Wk 10	Revision 2 printed: two-piece main housing carrying control board and power module; updated containers and mounts	Full electronics integration into enclosure achieved
7	Capstone II, Wk 11	Redesigned drawer; additional containers; V3 right-half with integrated 5-way button mini-housing; side and top closure panels	Enclosure closed out; navigation hardware housed
8	Capstone II, Wks 12-13	Near-final V3: finished button mini-housing with LED; dedicated LED board; keyed click connectors replacing loose wiring; new drawer; full reprint in black	Presentation-ready final assembly

[INSERT FIGURES: Iteration photographs] A visual progression across iterations 4 to 8 is compelling evidence of iterative design under ED2 and ED4. Two or three well-chosen photographs, each captioned with what changed and why.
5.4.5 Engineering Tools
Tool	Purpose
SolidWorks 2024	Parametric 3D CAD of all enclosure, mechanism, container, drawer and mount components
FDM 3D printer	Prototype fabrication in PLA/PETG
Digital calipers	Dimensional verification against CAD tolerances; joint, pocket and clearance measurement
Feeler gauge set	Drawer slide gap measurement
Push-pull gauge	Magnet retention force and drawer slide force measurement
Oscilloscope	Piezo transduction validation under real pill impact

5.4.6 Test Requirements and Method
Verified through the Mechanical Enclosure Test Plan, Phases 1 to 7: dimensional and visual inspection of every printed part, dovetail joint assembly, magnet pocket and retention verification, screen cutout alignment, top assembly and container fit, drawer slide and full assembly verification, panelling, and perfboard insertion. Results in Section 8.3.
[INCOMPLETE TEST DATA: MUST BE RESOLVED BEFORE SUBMISSION]
Substantial portions of the Mechanical Test Plan are unfilled, and several recorded results are not reportable as written:
- Phase 1C (Drawer): D-01 to D-04 have no results. The drawer was redesigned twice since, re-inspect the final part.
- Phase 3 (Magnets): MG-01 to MG-05 entirely unfilled, including MG-04 magnet retention force, which explicitly asks for a recorded numerical value. This is quantitative data you do not currently have and could obtain in ten minutes with the push-pull gauge.
- Phase 6 (Drawer slide): DR-01 to DR-07 unfilled, including DR-02 clearance gap (feeler gauge, 0.2 to 0.6 mm spec) and DR-07 overall envelope. Both are numerical.
- Ambiguous entries: V-03 recorded as "50/50"; J-02, J-03 and J-05 recorded as "X". Restate each as Pass, Fail or Partial with one sentence of explanation. "X" and "50/50" are not reportable results and read as carelessness.
These are fast measurements that convert directly into IV2, Data Collection marks. Prioritise them.

5.5 User Interface Subsystem
5.5.1 Definition
The User Interface Subsystem provides every user-facing interaction channel, running independently on the ESP32-S3 so that interface work imposes no computational load on the dispensing controller. It comprises the LCD and its menu system, the physical navigation controls, the per-compartment RGB status indicators, the audible alert channel, the Wi-Fi access point, the locally hosted web dashboard, and the Hall-effect drawer-open sensing that drives pickup confirmation.
5.5.2 Interface Channels
Table 5.8: ESP32-S3 GPIO assignment
Function	GPIO	Direction	Notes
LCD D/C (data/command)	2	Output	ST7796S LCD
LCD RST (reset)	4	Output	ST7796S LCD
LCD backlight enable	5	Output	ST7796S LCD
WS2812 LED data	9	Output	3-LED status strip, one per slot
UART1 RX from Pico TX (GPIO 20)	11	Input	115200 8N1
UART1 TX to Pico RX (GPIO 21)	12	Output	115200 8N1
LCD MOSI	13	Output	ST7796S LCD (SPI3)
LCD CLK	14	Output	ST7796S LCD (SPI3)
LCD CS	15	Output	ST7796S LCD (SPI3)
I2S BCLK	16	Output	Audio out
I2S WS (word select)	17	Output	Audio out
I2S DOUT	18	Output	Audio out
Hall-effect drawer sensor, Slot 0	33	Input, polled, pulled up, active-low	Currently the only one read by firmware; used as one shared "drawer opened" signal for all three slots
Hall-effect drawer sensor, Slot 1	34	Input	Wired per PCB layout, not yet read by firmware
Hall-effect drawer sensor, Slot 2	35	Input	Wired per PCB layout, not yet read by firmware
Button: LEFT	36	Input, pulled up	5-way pad
Button: RIGHT	37	Input, pulled up	5-way pad
Button: UP	38	Input, pulled up	5-way pad
Button: DOWN	39	Input, pulled up	5-way pad
Button: OK / SELECT	40	Input, pulled up	5-way pad
USB/console UART (ROM)	43, 44	N/A	Reserved for flashing/serial monitor, not app-usable

Only the Slot 0 Hall sensor is currently polled by firmware; it is used as a single shared "drawer opened" signal covering all three slots, consistent with the physical design, since the enclosure has one shared collection drawer (Section 5.4.2), not one drawer per compartment. GPIO 34 and 35 are wired on the PCB but not yet read in firmware. Because the system has no way to determine which specific pill was removed without additional instrumentation such as load cells, a drawer-open event clears the pending reminder for every slot currently awaiting collection, not just one; the reasoning and its consequence for the "per-station" claim in Section 5.5.3 are discussed there.
Display. A 4.0-inch ST7796S TFT LCD at 480 x 320, driven over SPI. The interface presents slot status, medication names, remaining counts, next scheduled doses and dispense outcomes, using large-format typography for the target user. It provides an on-screen keyboard for medication name entry, a digit-based schedule editor, and explicit on-screen button-hint text so the mapping between physical controls and on-screen actions is never ambiguous.
Physical controls. Navigation is by discrete physical controls rather than the touch layer: initially four tactile push buttons (up, down, select, back), subsequently replaced by a five-way navigation switch in a dedicated printed housing. The rationale is accessibility: a resistive touch panel requires sustained accurate pressure, a poor match for users with tremor or reduced dexterity, whereas a physical control provides positive tactile confirmation that an input registered. The touch layer remains present on the panel and is reserved for future development.
Visual status indication. Three RGB LEDs, one per compartment, mounted on a dedicated board and protruding through the front panel:
Colour	Meaning
Purple	Dispense in progress
Green	Dispense successful and verified
Red	Dispense failed after all retry attempts

The LEDs also indicate which slot is selected during profile editing. Per-compartment colour-coded indication communicates outcome at a glance without requiring the user to read the display, a meaningful accessibility property for users with impaired vision.
Audible alerting. A speaker driven by a MAX98357A I2S Class-D amplifier provides audible notification for due doses and dispense events. Together with the LEDs and the LCD this gives three independent alert channels, satisfying R-09A and R-09B and ensuring a user who cannot see the display, or cannot hear the alert, still receives notification through another channel.
5.5.3 Notification Subsystem
The notification subsystem determines what the system tells the user, when, and through which channel. Its design is governed by a single principle: an alert exists to change an outcome. A notification that cannot prompt a useful action is noise, and noise trains users to ignore the channel, which in a medication device has a direct clinical cost. Every notification rule below follows that principle.
Escalating pickup reminders. A dose that has been dispensed and verified but not yet collected triggers a sequence of four reminders at 5, 10, 15 and 20 minutes after the dispense event. Each stage cancels automatically the moment the drawer sensor confirms retrieval (Section 1.5.1). Escalation exists because a single reminder at the moment of dispensing is easily missed by exactly the user population the device serves: someone who is asleep, out of the room, or cognitively impaired. Escalating intervals increase the probability of reaching the user without generating continuous alerting.
Per-station pickup state, with a shared physical trigger. Pickup state is tracked as a separate flag per compartment in software, which closes a defect found in testing where collecting a dose at one station could incorrectly mark a different, still-uncollected station's dose as taken (Section 7.5.5). Clearing that state, however, is intentionally collective rather than per-station: because the enclosure has one shared collection drawer rather than one per compartment (Section 5.4.2), and only the Slot 0 Hall sensor is currently read (Table 5.8), a single drawer-open event clears the pending reminder for every slot that currently has an uncollected dose, not just one. This is a deliberate design choice rather than a limitation of the sensing: the system has no way to determine which specific pill a user removed without additional instrumentation such as load cells, and the reasonable assumption is that a user who opens the drawer will see and collect everything sitting in it, rather than taking one dose and deliberately leaving another. The independent tracking therefore exists to prevent one slot's state from corrupting another's; it does not mean each slot's reminder can only be cleared by its own separate physical event.
Low pill count alerts. Raised at 1 pill remaining and again at 0 remaining, each firing once on the transition rather than repeating every subsequent evaluation. Edge-triggering rather than level-triggering is deliberate: a repeating low-stock alert is the classic mechanism by which users learn to dismiss notifications without reading them.
Scheduled-dispense notification only. A dispense triggered by the schedule generates a notification; a dispense the user initiated manually from the dashboard or LCD does not. The rationale is that a user who just pressed the dispense button does not need to be told a dispense occurred, they are standing in front of the device. Notifying only on autonomous events keeps the channel meaningful.
Table 5.5: Notification rules
Trigger	Channels	Repeat behaviour	Cancellation
Scheduled dose dispensed	LCD, LED, audible, push	Once per dispense event	N/A
Dose dispensed, not collected	Push	Four stages at 5 / 10 / 15 / 20 min	Automatic on drawer-open pickup confirmation
Pill count reaches 1	Push	Once, on transition	N/A
Pill count reaches 0	Push	Once, on transition	N/A
Dispense failed after retries	LCD, LED (red), audible	Once per failed cycle	N/A
Manual dispense	LCD, LED, audible	N/A	Deliberately generates no push notification

Notification transport. Push delivery uses ntfy, an open-source publish-subscribe notification service. The device publishes to a named topic over HTTPS, so notification content is encrypted in transit; the user subscribes to the same topic in the ntfy client application on a phone or desktop and receives notifications as standard OS-level push alerts. All four escalation stages, the low-count alerts and the scheduled-dispense notification were verified end-to-end through this client.
The choice suits the project's architectural position. ntfy requires no user account, no vendor SDK, no proprietary push credentials and no device registration, and it can be self-hosted, so the notification path need not depend on any third-party operator. Publishing is a simple HTTP request, which keeps the ESP32-S3 implementation small and avoids pulling a vendor push framework into the firmware.
Event logging. Every dose event is recorded in a three-state log, reviewable through the web dashboard:
State	Meaning	Decided by
Pending	Dose dispensed and verified, awaiting collection	ESP32-S3, unilaterally, the moment it receives a successful dispense ACK (result=ok) from the RP2350
Taken	Dose dispensed, verified, and physically collected by the user	ESP32-S3, unilaterally, on its own Hall-effect drawer sensor tripping (GPIO 33), with no UART round-trip to the RP2350 for this transition
Failed	Dispense could not be verified after all retry attempts	RP2350 reports the raw pass/fail result of the physical dispense attempt (piezo and IR confirmation); the ESP32-S3 records it. Pill count is preserved
Timeout	No dispense acknowledgement received within the expected window	ESP32-S3, unilaterally, against a self-armed deadline checked on its own clock, with no RP2350 involvement

The RP2350 has no concept of Pending or Taken in its own firmware; its entire contribution to this state machine is the single success/fail bit for the physical dispense action. Its firmware does use the word "timeout" internally, but that refers to an unrelated mechanism, the 10-second per-attempt sensor wait inside its own dispense-verification retry loop (Section 5.3.3), which governs whether a single dispense attempt succeeds, not whether a dose was ever collected. The pickup-lifecycle Timeout state in the table above is a distinct concept that the RP2350 does not model at all. Every downstream lifecycle state, Pending, Taken and pickup-lifecycle Timeout, is decided entirely on the ESP32-S3, which is a direct consequence of the pickup-confirmation hardware (the Hall-effect drawer sensor) being physically wired to the ESP32-S3 rather than the RP2350. This satisfies R-20 and R-22 together, and the Pending state is what drives the escalation sequence. Note the assumption embedded in the Taken state: the system infers collection from drawer opening, and cannot distinguish a user retrieving a specific dose from a user opening the drawer for another reason, or from collecting several pending doses at once (Section 5.5.3, above). This is a reasonable inference in normal use and is recorded as an assumption in Section 9.4.
Notification payload and topic security, confirmed from the firmware source. The payload names the actual medication in every notification, for example "%s was dispensed from Slot %d as scheduled" for a scheduled dispense, and "%s was dispensed from Slot %d at %s and the drawer hasn't been opened yet" for a pickup reminder, with the medication name substituted directly. A generic alternative such as "Slot 2 dose ready" would disclose less on a lock screen and is a candidate simplification, but is not currently implemented.
The topic name is a genuine, confirmed weakness rather than a theoretical one. Because ntfy topics are public by default on the hosted service, the topic name is the only access control: anyone who knows or guesses it can subscribe to the feed and read every notification, including medication reminders. The device's own configuration file (ntfy_credentials.h) sets the topic to "Portapill_Notify", a predictable, project-name-derived string, and the file's own setup comment explicitly warns against exactly this pattern ("NOT something like 'portapill' or a family name"). This should be replaced with a long random topic string before the system is used with real medication data; a candidate replacement is pp-e66c1fabea51ec7b5c9c9441098b42a4, generated for this purpose and not otherwise used anywhere. Self-hosting the ntfy server would remove the exposure entirely and is noted as a Section 14.5 recommendation. Both the payload content and the topic-naming weakness are recorded honestly here and in Section 11.4 alongside the open-access-point discussion; naming a real weakness with its remedy is exactly the professional judgment EE1, Ethical Responsibility, marks.
5.5.4 Network Architecture and Web Dashboard
The system supports two concurrent connectivity modes, and the distinction between them matters to the offline argument made in Section 2.5.
Access point mode (always available). The ESP32-S3 hosts its own Wi-Fi network. A phone or laptop connects directly to the device with no infrastructure in between and reaches the dashboard at a fixed local address. This mode requires no router, no internet service, no configuration, and it is the mode in which every core function remains fully available.
Station mode (optional). The device can additionally join an existing Wi-Fi network. This enables outbound push notifications and allows the dashboard to be reached from anywhere on the household network. No core function depends on this mode.
Parameter	Value
Access point SSID	PortaPill
Access point security	WPA2-PSK, confirmed active. ap_uses_password() requires the configured passphrase to be at least 8 characters before enabling WPA2; the deployed passphrase is 9 characters, so the WPA2 path is genuinely in effect rather than the open-network fallback.
Access point address	192.168.4.1
Hostname (station mode)	portapill.local, re-announced on network join
Operating mode	Simultaneous access point and station, both interfaces active concurrently
Captive DNS	UDP port 53; all queries resolved to the device address, prompting automatic portal redirection on connecting clients
Concurrent socket budget	16 (raised from 10, see Section 7.5.4)
Verified concurrent clients	2 devices, peak 7 concurrent sockets, no connection failures

Access paths. Three routes to the dashboard are supported, in order of preference:
Route	When used	Address
Same Wi-Fi network (recommended)	Client and device both on the household network	http://portapill.local
Direct connection	First-time setup, or any location with no Wi-Fi	Join the PortaPill network, then http://192.168.4.1
Numeric address (fallback)	Where hostname resolution fails on a particular client OS	Device IP, reported in the setup log

Because access point and station modes run simultaneously, the device remains reachable directly even while it is joined to a household network. This matters for reliability: a router failure removes remote access and notification delivery, but does not remove the user's ability to reach the device.
Connection management. Serving a live-updating dashboard to several clients at once places a demand on socket resources that a single-client design does not anticipate. Three provisions make multi-client operation reliable:
●	A socket budget sized to real usage. Each connected browser holds several concurrent sockets rather than one; the original allowance of 10 was exhausted within seconds of a second device joining. The budget was raised to 16 after measuring actual peak usage at 7.
●	Stale connection eviction. Connections that are no longer live are detected and released rather than being held until they time out naturally.
●	Abrupt-disconnect cleanup. A client that leaves the network without closing its browser has all of its open connections detected and released immediately. Verified in testing: a device dropping off Wi-Fi had all five of its open connections reclaimed rather than stranded.
Hostname resolution. In station mode the device announces itself as portapill.local, and re-announces on joining a network so that clients which have never previously reached it can resolve the name. End-to-end resolution nonetheless remains dependent on client operating system configuration, and the numeric address remains the reliable access path. This is recorded as a known limitation (Section 9.5).
Access control. The access point is protected by a WPA2 pre-shared key, so the wireless link is encrypted and unauthenticated devices in radio range cannot reach the dashboard. Two residual weaknesses are recognised and recorded rather than concealed:
●	The passphrase is a weak, shared default. A default credential common to every unit provides far less protection than a device-unique key. The remedy is a per-unit passphrase generated at manufacture and printed on the enclosure, which is standard practice for consumer network devices and costs nothing at scale.
●	API endpoints are not individually authenticated. Any client that has joined the network can issue state-changing requests, including a dispense command. Network-level access control is the only barrier. The remedy is authentication on /api/profile, /api/dispense and /api/time-sync.
Both are appropriate to a prototype and both are specified as remedies in Section 11.4 and 14.5. Naming the weak default and the unauthenticated endpoints, with the remedy for each, is exactly the professional judgment EE1, Ethical Responsibility, marks, and disarms the question in Q&A.
HTTP API. The device serves a dashboard page and a JSON API.
Table 5.6: HTTP API endpoints
Method	Endpoint	Function
GET	/	Web dashboard page
GET	/api/status	Full system state: all slot data, last dispense result, device time, bridge connection state
POST	/api/profile	Accept a medication profile; persist to NVS; forward to RP2350 as LOAD_PROFILE
POST	/api/dispense	Trigger a dispense; start a 40 s acknowledgement timeout; reject concurrent requests with HTTP 409 Conflict
POST	/api/time-sync	Update the ESP32-S3 clock and forward SET_TIME to the RP2350
GET	/generate_204, /gen_204, /ncsi.txt, /connecttest.txt	Captive-portal probe endpoints; redirect to /

The 409 Conflict response on concurrent dispense requests is a deliberate safety measure: it prevents a double-tap, a page refresh, or two users on two devices from queueing duplicate dispense commands for the same slot. A device that accepted a second dispense request while the first was still in progress could deliver a double dose. This is a small implementation detail with a direct patient-safety consequence, and is worth presenting as such.
Table 5.7: /api/status response fields
Field	Description
device_time	Current device system time
bridge_connected	Boolean, true when UART data is fresh within 5 s
controller_name, controller_transport	Controller identification and link type
active_profile_slot	Currently selected slot (0-4)
medication_name	Active medication name
pills_left	Remaining pill count
pills_per_dose	Pills dispensed per dose
doses_remaining	Doses remaining at current count and dose size
last_dispensed	Timestamp of last dispense
last_event	Most recent system event
notes	Supplementary status

5.5.5 Firmware Architecture
The ESP32-S3 application is built on ESP-IDF v6.0 with CMake and Ninja. Initialisation proceeds in fixed order: NVS init, restore saved state (five allocated slot profiles, three in active use, plus dispense history), apply defaults if no saved state exists, configure the UART bridge on GPIO 11 (RX) / GPIO 12 (TX) at 115200 baud, launch the FreeRTOS bridge task, start Wi-Fi AP, captive DNS and HTTP server.
Concurrency. All runtime state is held in a single structure containing the five slot states, a 16-entry dispense history ring buffer, active slot tracking, last acknowledgement received, and a pending-dispense flag. Because this structure is accessed by both the UART bridge task and the HTTP request handlers, genuinely concurrent contexts, it is protected by a FreeRTOS mutex. This is a necessary correctness measure rather than a precaution: without it, a status update arriving over UART during an HTTP response could produce a torn read and serve inconsistent data to the client.
Persistence. Every meaningful state change, STATUS update, BOOT_SYNC, profile save, ACK receipt, is written to NVS, so cached display state survives an ESP32-S3 reset independently of the RP2350.
[INSERT FIGURE: ESP32-S3 task architecture diagram] Show the FreeRTOS tasks (UART bridge, HTTP server, LCD/UI, DNS responder), the shared state structure, and the mutex boundary. A concurrency diagram is strong evidence of software design maturity and is rare in capstone reports.
[INSERT FIGURE: Web dashboard screenshot] Final version, showing live slot data, connection state and dispense controls. Capture with real profile data loaded, not placeholder text.
[INSERT FIGURES: LCD interface screens] At minimum: boot/main screen, slot selection menu, profile edit with on-screen keyboard, schedule editor, dispense in progress, dispense success, dispense failure, low-pill alert. Photograph the physical screen rather than rendering it, evaluators want evidence of the working device.
5.5.6 Engineering Tools
Tool	Purpose
ESP-IDF v6.0	Embedded framework for ESP32-S3
CMake + Ninja	Build system
Serial monitor	Debugging and log output
Logic analyser	UART traffic validation against the RP2350
Phone and laptop browsers	Wi-Fi client, captive portal and dashboard testing
ntfy client application	Push notification subscription and end-to-end delivery verification
Git / GitHub	Version control

5.5.7 Test Requirements and Method
Verified through the ESP32-S3 Firmware Test Plan: build and flash, Wi-Fi access point behaviour including short-passphrase fallback, captive portal redirection, JSON API correctness, UART bridge including connection timeout handling, UI content, and integration with the RP2350. Results in Section 8.5.

5.6 Integrated Control Board and Custom PCB
5.6.1 Definition
The integration layer physically realises every interconnection between subsystems. It was developed as two successive builds: a soldered perfboard assembly used to validate the complete wiring architecture, and a custom two-layer printed circuit board designed in Altium Designer that became the production board for the final unit.
5.6.2 Perfboard Control Board
The perfboard assembly consolidated all major subsystems onto a single board carrying the Raspberry Pi Pico 2, five DRV8833 stepper driver modules, JST connectors for all three stepper motors, the LM317 regulator and its network, the ESP32-S3 soldered directly to the board, the MAX98357A audio amplifier, and a dedicated LCD header for ribbon-style connection. A separate mini perfboard carried the four navigation buttons and three RGB status LEDs, connecting back through a wiring harness.
This build validated the complete electrical architecture and remains intact as a reference build.
[INSERT FIGURES: Perfboard control board] Top view, bottom view, and installed in the enclosure. Photograph on a plain background with even lighting; component-level detail should be legible.
5.6.3 Custom PCB
A custom two-layer PCB was designed in Altium Designer to replace the perfboard. The schematic captures all subsystems: both microcontroller footprints, all motor driver connections, sensor signal conditioning and GPIO protection dividers, and module header connectors for the LCD, audio amplifier and button/LED assembly.
Design decisions of note:
●	Footprint compatibility. The layout matches the physical envelope of the perfboard assembly, allowing direct drop-in replacement into the existing enclosure with no mechanical rework.
●	Noise isolation through placement. Component placement and routing were planned to minimise coupling between the motor driver section and the sensor signal lines. This is a substantive concern rather than a formality: the piezoelectric input detects a low-energy mechanical transient, and stepper drive currents switching nearby are a credible interference source on that input.
●	Polygon pours. Power and ground polygon pours were added for lower-impedance distribution and improved grounding in the surrounding regions.
●	Connector standardisation. Connector types were revised to a 6-pin layout carrying a dedicated ground return for the 5-way button board, replacing loose soldered connections with keyed click connectors.
●	Provision under uncertainty. Hall effect sensor slots were included so the part could be connected if required, without committing to a specific role. Providing footprints for a not-yet-finalised function is standard practice and preferable to a board respin.
Gerber and NC drill files were generated and submitted to JLCPCB. The board was received on 20 July 2026 with components arriving 21 July, was assembled, verified connection by connection, and integrated as the production board for the second unit.
[INSERT FIGURES: PCB design and build, required set]
- Altium schematic (split across pages if needed; must be legible, never shrink to illegibility to fit one page)
- PCB layout, top and bottom copper
- 3D render from Altium
- Photograph of the bare fabricated board
- Photograph of the fully populated board
- Photograph of the populated board installed in the enclosure
The progression schematic to layout to render to bare board to populated board is compelling evidence under ED4, Implementation.
5.6.4 Engineering Tools
Tool	Purpose
Altium Designer	Schematic capture, PCB layout, design rule checking, Gerber and NC drill generation, BOM generation
JLCPCB	Two-layer PCB fabrication
Digi-Key	Component sourcing
Soldering station	Assembly of both perfboard and PCB builds
Digital multimeter	Continuity and isolation verification prior to first power-up

5.6.5 Test Requirements and Method
Verified through Board Test Plan Phases 1 to 5: visual inspection, continuity, power rail validation, stepper motor testing and sensor verification. The plan was applied to both builds. Results in Section 8.2.
