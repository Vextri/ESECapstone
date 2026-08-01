================================================================================
  PILL DISPENSER - MASTER README
  Raspberry Pi Pico / RP2350 | Capstone Project
================================================================================

--------------------------------------------------------------------------------
PINOUT SUMMARY
--------------------------------------------------------------------------------

  Three bipolar stepper motors now drive dispensing, one per slot, each on
  its own DRV8833. There is no DC motor in the current build. Full detail
  (including sensors and free GPIOs) lives in GPIO_PINOUT.txt, summary below:

  GPIO   | Role                          | Driver / Component
  -------+-------------------------------+------------------------------
  GPIO8  | Stepper 1 AIN1 (Coil A+)      | DRV8833 - Motor 1 / Slot 0
  GPIO9  | Stepper 1 AIN2 (Coil A-)      | DRV8833 - Motor 1 / Slot 0
  GPIO10 | Stepper 1 BIN1 (Coil B+)      | DRV8833 - Motor 1 / Slot 0
  GPIO11 | Stepper 1 BIN2 (Coil B-)      | DRV8833 - Motor 1 / Slot 0
  GPIO6  | Stepper 2 AIN1 (Coil A+)      | DRV8833 - Motor 2 / Slot 1
  GPIO4  | Stepper 2 AIN2 (Coil A-)      | DRV8833 - Motor 2 / Slot 1
  GPIO27 | Stepper 2 BIN1 (Coil B+)      | DRV8833 - Motor 2 / Slot 1
  GPIO28 | Stepper 2 BIN2 (Coil B-)      | DRV8833 - Motor 2 / Slot 1
  GPIO12 | Stepper 3 AIN1 (Coil A+)      | DRV8833 - Motor 3 / Slot 2
  GPIO13 | Stepper 3 AIN2 (Coil A-)      | DRV8833 - Motor 3 / Slot 2
  GPIO14 | Stepper 3 BIN1 (Coil B+)      | DRV8833 - Motor 3 / Slot 2
  GPIO15 | Stepper 3 BIN2 (Coil B-)      | DRV8833 - Motor 3 / Slot 2
  GPIO5  | Piezo sensor input            | Piezo impact sensor (Slot 0)
  GPIO7  | IR sensor input               | IR beam-break sensor (Slot 0)

  Notes:
  - The board has 4 stepper driver positions (DRV1-4), one per motor
    connector, but only 3 slots. Normal layout is driver-number-matches-
    slot-number (DRV1->Slot0, DRV2->Slot1, DRV3->Slot2). Slot 1 currently
    runs on DRV4 (GPIO4/6/27/28) instead of DRV2 (GPIO0-3), since that
    DRV2 unit had issues, DRV4 was the spare position and got wired in as
    a substitute. GPIO0-3 are unused as a result. Motor 2's AIN1/AIN2 are
    swapped in software relative to the driver's physical IN1/IN2
    labeling to correct its rotation direction. GPIO6 (DRV4's IN2) was
    used for early sensor testing at one point but has not been a real
    hall-effect input for a long time, it's simply a driver line now.
  - Each motor now has its own step delay rather than one shared rate:
    Motor 1 = 5600us/step, Motor 2 = 5600us/step, Motor 3 = 2000us/step
    (STEPPER_STEP_DELAY_US_MOTOR_1/2/3 in stepper_control.h).
  - Sensors use interrupt-driven edge detection with debounce.
  - Hall-effect drawer sensors (3, one per slot per the PCB layout) are
    wired to the ESP32 (GPIO33/34/35), not the Pico.

--------------------------------------------------------------------------------
HARDWARE OVERVIEW
--------------------------------------------------------------------------------

  [Pico] -- GPIO8-11      --> [DRV1] --> [Stepper Motor 1 / Slot 0]
  [Pico] -- GPIO4,6,27,28 --> [DRV4] --> [Stepper Motor 2 / Slot 1] (substitute for DRV2)
  [Pico] -- GPIO12-15     --> [DRV3] --> [Stepper Motor 3 / Slot 2]
  [Pico] <-- GPIO5 --- [Piezo Sensor]  (pull-up, falling edge trigger)
  [Pico] <-- GPIO7 --- [IR Beam-Break] (pull-up, falling/rising trigger)
  [Pico] <-> UART1 (GPIO20/21) <-> [ESP32-S3] (see ESPCode/Server/README.txt)

--------------------------------------------------------------------------------
CODE FLOW
--------------------------------------------------------------------------------

1. STARTUP (Main.c -> main())
   |
   +-- stdio_init_all()            - Enable USB serial console
   +-- dispenser_init()            - Init motor PWM, clear all profiles
   +-- stepper_init()              - Init GPIO8-11 as outputs, all LOW
   +-- sensor_interrupts_init()    - Configure GPIO5-7 as inputs with IRQs
   +-- piezo_set_callback()        - Print "PILL DROP DETECTED"
   +-- hall_effect_set_callback()  - Print + call motor_stop()
   +-- ir_set_callback()           - Print beam state
   +-- Load profiles               - Slot 0: Vitamin D (15 pills, 2/dose)
   |                                 Slot 1: Aspirin   (20 pills, 1/dose)
   +-- dispenser_switch_to_profile(0) - Activate Vitamin D
   +-- Print command menu
   |
   +-- Enter main loop

2. MAIN LOOP (Main.c)
   |
   +-- getchar_timeout_us(0)       - Non-blocking keyboard read
   |     |
   |     +-- PICO_ERROR_TIMEOUT    - No input, continue
   |     +-- Any valid key         - Execute switch/case (see Commands below)
   |
   +-- stepper_task()              - Check if step interval elapsed, advance
   |                                 one full-step on DRV8833 #2 if due
   +-- sleep_ms(1)                 - Yield CPU
   +-- Repeat

3. SENSOR INTERRUPTS (sensor_interrupts.c)
   |
   +-- Each sensor fires a GPIO IRQ on edge detect
   +-- Debounce filter (Piezo=100ms, Hall=10ms, IR=50ms) drops glitches
   +-- Increments per-sensor counter (piezo_count, hall_count, ir_count)
   +-- Fires registered callback if set and sensor is enabled

4. SENSOR-BASED DISPENSING (pill_dispenser.c -> dispenser_execute_dose_sensor_based())
   |
   +-- Validate active profile + pill count
   +-- For each pill in the dose (up to pills_per_dose):
   |     |
   |     +-- dispenser_dispense_single_pill_sensor_based(timeout=10000ms)
   |           |
   |           +-- Enable piezo + IR if not already enabled
   |           +-- Up to 3 attempts:
   |                 |
   |                 +-- Reset piezo_count and ir_count to zero
   |                 +-- motor_forward()     <- disc starts spinning
   |                 +-- Poll every 5ms:
   |                 |     piezo_count > 0? -> motor_stop(), break
   |                 |     elapsed > timeout? -> motor_stop(), ABORT (hardware fault)
   |                 |
   |                 +-- sleep_ms(300)       <- allow IR IRQ time to register
   |                 +-- Check ir_count > 0
   |                       YES -> pill confirmed, return true
   |                       NO  -> retry (up to 3x), then return false
   |
   +-- On success: decrement pills_remaining, pause 1s between pills
   +-- On failure: stop dispensing, report incomplete dose

5. TIMED DISPENSING (pill_dispenser.c -> dispenser_execute_dose())
   |
   +-- Validate active profile + pill count
   +-- For each pill:
         motor_forward() -> sleep(dispense_time_ms) -> motor_stop()
         pills_remaining--
         sleep_ms(500) between pills

6. STEPPER TASK (stepper_control.c -> stepper_task())
   |
   +-- Called every loop iteration (~1ms cycle)
   +-- If STEPPER_IDLE: return immediately
   +-- If elapsed time >= STEPPER_STEP_DELAY_US (3000us):
         Advance step index (+1 forward, +3 mod4 backward)
         Write full-step pattern to GPIO8-11
         Schedule next step time

--------------------------------------------------------------------------------
KEYBOARD COMMANDS
--------------------------------------------------------------------------------

  Key | Action
  ----+-------------------------------------------------------------
  D   | Dispense dose (timed, uses dispense_time_ms from profile)
  F   | Dispense dose (sensor-based: piezo stops motor, IR confirms)
  S   | Show active profile status (pills remaining, doses left)
  L   | List all profile slots
  B   | Simulate button press (runs sensor-based dose)
  1   | Switch to profile slot 0 (Vitamin D)
  2   | Switch to profile slot 1 (Aspirin)
  P   | Toggle piezo sensor enable/disable
  H   | Toggle hall effect sensor enable/disable
  I   | Toggle IR sensor enable/disable
  R   | Reset all sensor counts to zero
  T   | Print sensor enable/count status
  G   | Read raw GPIO states for GPIO5, 6, 7
  V   | Continuous 200ms GPIO monitor (any key to stop)
  C   | Debug interrupt configuration
  W   | Manual motor forward (DC disc motor)
  X   | Manual motor stop   (DC disc motor)
  A   | Stepper forward (continuous, non-blocking)
  Z   | Stepper backward (continuous, non-blocking)
  E   | Stepper stop + print step count
  Q   | Shutdown - stop motor, exit

--------------------------------------------------------------------------------
PROFILE SYSTEM
--------------------------------------------------------------------------------

  - Up to 5 profile slots (MAX_PROFILES)
  - Each profile stores:
      medication_name    (up to 31 chars)
      pills_remaining    (uint8)
      pills_per_dose     (uint8)
      dispense_time_ms   (used only by timed 'D' command)
      is_active          (slot occupied flag)
  - Only one profile is "current" at a time (current_profile_slot)
  - Slot 0 = Vitamin D, Slot 1 = Aspirin (loaded at startup)

--------------------------------------------------------------------------------
STEPPER MOTOR DRIVE PATTERN (Full-Step Bipolar)
--------------------------------------------------------------------------------

  Step | AIN1 | AIN2 | BIN1 | BIN2 | State
  -----+------+------+------+------+----------
    0  |  1   |  0   |  1   |  0   | A+ B+
    1  |  0   |  1   |  1   |  0   | A- B+
    2  |  0   |  1   |  0   |  1   | A- B-
    3  |  1   |  0   |  0   |  1   | A+ B-

  Forward:  0 -> 1 -> 2 -> 3 -> 0 ...
  Backward: 0 -> 3 -> 2 -> 1 -> 0 ...
  Step rate is per-motor now, not a single shared value, see the notes
  under PINOUT SUMMARY above.

--------------------------------------------------------------------------------
FILE STRUCTURE
--------------------------------------------------------------------------------

  Main.c                - Entry point, main loop, keyboard handler
  motor_control.c/.h    - DC motor PWM via DRV8833 (GPIO1, GPIO2)
  stepper_control.c/.h  - Bipolar stepper via DRV8833 (GPIO8-11)
  sensor_interrupts.c/.h- IRQ setup + debounce for piezo/hall/IR
  pill_dispenser.c/.h   - Profile management + dispensing logic
  CMakeLists.txt        - Build configuration (Pico SDK)
  build/                - Compiled output (Main.uf2, Main.elf)

================================================================================
