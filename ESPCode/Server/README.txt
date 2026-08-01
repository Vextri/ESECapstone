================================================================================
  PORTAPILL - ESP32-S3 SERVER README
  ESP32-S3 (ESP-IDF) | Capstone Project
================================================================================

--------------------------------------------------------------------------------
WHAT THIS BOARD DOES
--------------------------------------------------------------------------------

  The ESP32-S3 is the "front end" of PortaPill: it hosts the Wi-Fi network,
  the web dashboard, the on-device LCD menu, push notifications, and the
  clock, then relays commands and profile data to the Raspberry Pi Pico 2
  over a simple UART line protocol. The Pico owns the motors, the pill
  sensors, and the actual dispensing logic (see MCUCode/Main/README.txt for
  that side).

  In short: the Pico dispenses pills, the ESP is everything the user sees
  and touches.

--------------------------------------------------------------------------------
PINOUT SUMMARY
--------------------------------------------------------------------------------

  GPIO   | Role                        | Component
  -------+-----------------------------+---------------------------------
  GPIO2  | LCD D/C (data/command)      | ST7796 LCD
  GPIO4  | LCD RST (reset)             | ST7796 LCD
  GPIO5  | LCD backlight enable        | ST7796 LCD
  GPIO9  | WS2812 data                 | Status LED strip (3 LEDs)
  GPIO11 | UART1 RX (ESP <- Pico)      | Pico link
  GPIO12 | UART1 TX (ESP -> Pico)      | Pico link
  GPIO13 | LCD MOSI                    | ST7796 LCD (SPI3)
  GPIO14 | LCD CLK                     | ST7796 LCD (SPI3)
  GPIO15 | LCD CS                      | ST7796 LCD (SPI3)
  GPIO16 | I2S BCLK                    | Audio out (speaker/amp)
  GPIO17 | I2S WS (word select)        | Audio out (speaker/amp)
  GPIO18 | I2S DOUT                    | Audio out (speaker/amp)
  GPIO33 | Hall-effect signal (pulled up, active-low) | Drawer-open sensor (Slot 0)
  GPIO36 | Button: LEFT                | 5-way button pad
  GPIO37 | Button: RIGHT               | 5-way button pad
  GPIO38 | Button: UP                  | 5-way button pad
  GPIO39 | Button: DOWN                | 5-way button pad
  GPIO40 | Button: OK / SELECT         | 5-way button pad

  Notes:
  - The PCB allocates one hall-effect drawer sensor per slot (GPIO33/34/35),
    but firmware currently only reads GPIO33 and treats it as one shared
    "drawer opened" signal for all 3 slots, see drawer_sensor.c. GPIO34/35
    are wired but not yet read.
  - GPIO11/12 are reserved for the Pico link, this is why the LCD's SPI
    pins were moved off the more "obvious" GPIO choices, see the comment
    at the top of lcd_display.c.
  - GPIO43/44 are the ESP32-S3's own USB/console UART, used for flashing
    and the serial monitor. Don't repurpose these.
  - All 5 buttons and the LCD/RST/DC/BL lines use the ESP's internal
    pull-ups where applicable, buttons read LOW when pressed.

--------------------------------------------------------------------------------
UART LINK TO THE PICO
--------------------------------------------------------------------------------

  ESP GPIO12 (TX) -----> Pico RX
  ESP GPIO11 (RX) <----- Pico TX
  ESP GND          -----  Pico GND   (must be common)

  115200 baud, 8N1, no hardware flow control. Line-based text protocol,
  each message ends in '\n':

    ESP -> Pico   CMD|action=DISPENSE|slot=0
    ESP -> Pico   CMD|action=SET_TIME|epoch=1785500000
    ESP -> Pico   CMD|action=LOAD_PROFILE|slot=0|med=Aspirin|total=20|dose=1|time=3000|schedule=08:00,20:00
    Pico -> ESP   ACK|action=DISPENSE|slot=0|result=ok
    Pico -> ESP   STATUS|slot=0|med=Aspirin|left=19|dose=1|...
    Pico -> ESP   BOOT_SYNC|slot=0|med=Aspirin|left=19|...
    Pico -> ESP   TIME_REQ

  See uart_bridge.c for the full protocol handling on the ESP side.

--------------------------------------------------------------------------------
FILE STRUCTURE
--------------------------------------------------------------------------------

  main.c                 - Entry point, brings every subsystem online
  bridge_state.c/.h       - Shared dispenser state + flash (NVS) persistence
  uart_bridge.c/.h        - Line protocol to/from the Pico
  time_utils.c/.h         - Clock/timezone and schedule string parsing
  notify.c/.h             - ntfy.sh push notifications
  lcd_display.c/.h        - On-device LCD UI + 5-way button pad
  led_feedback.c/.h       - WS2812 per-slot status LEDs
  audio_feedback.c/.h     - I2S tone playback
  drawer_sensor.c/.h      - Hall-effect pickup-confirmation sensor
  wifi_ap.c/.h            - Wi-Fi access point + home Wi-Fi/NTP client
  captive_dns.c/.h        - Captive-portal DNS responder
  web_server.c/.h         - HTTP dashboard + JSON API
  debug_log.c/.h          - In-RAM log capture, readable over Wi-Fi
  wifi_credentials.h      - Real Wi-Fi network(s), gitignored (see below)
  ntfy_credentials.h      - Real ntfy.sh topic, gitignored (see below)
  Assets/                 - Logo image embedded into the firmware binary

--------------------------------------------------------------------------------
HOW TO CONNECT TO IT
--------------------------------------------------------------------------------

  Once flashed and running, the dashboard is reachable three ways:

  1. Same Wi-Fi network (recommended day-to-day):
       http://portapill.local
     or the IP address printed in the boot log ("Joined ... got IP: ...").

  2. Direct connection, no Wi-Fi required, useful for first-time setup:
       Join the "PortaPill" Wi-Fi network (password: 123456789)
       then open http://192.168.4.1

  3. If step 1 doesn't resolve on a given device, ask whoever set the
     board up for its current IP address and use that directly, mDNS
     name resolution can be unreliable on some networks/operating
     systems (see the troubleshooting notes at the bottom of this file).

--------------------------------------------------------------------------------
BUILDING AND FLASHING (SETUP FOR A NEW MACHINE)
--------------------------------------------------------------------------------

  Requirements: ESP-IDF v6.0.2 (or compatible v6.x), targeting esp32s3.

  1. Install ESP-IDF if you haven't already:
       https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/

  2. Open a terminal with the ESP-IDF environment activated (run the
     IDF "export" script for your platform, e.g. export.ps1/export.sh),
     or use the ESP-IDF terminal from VS Code's ESP-IDF extension.

  3. From this directory (ESPCode/Server):
       idf.py set-target esp32s3
       idf.py build

  4. Fill in your own credentials before your first real (non-AP-only)
     build:
       - Copy main/ntfy_credentials.example.h to main/ntfy_credentials.h
         and set a private, hard-to-guess NTFY_TOPIC (see the comment in
         that file for why this matters, ntfy topics are public by
         default).
       - Create main/wifi_credentials.h (see the format documented at the
         top of wifi_ap.h / inside notify.c's HOME_WIFI_CANDIDATES usage)
         with your real network name(s) and password(s). Without this
         file filled in with a real network, the ESP will still boot and
         serve its own "PortaPill" hotspot, it just won't be able to join
         a home network for internet access (NTP time sync, push
         notifications).
     Both files are gitignored and will never be committed.

  5. Flash and monitor:
       idf.py -p <PORT> flash monitor
     Replace <PORT> with your board's serial port (e.g. COM4 on Windows,
     /dev/ttyUSB0 on Linux). Press Ctrl+] to exit the monitor.

  6. First boot: the ESP starts its own "PortaPill" access point
     immediately regardless of Wi-Fi credentials. If wifi_credentials.h
     has real values, it also joins that network in the background for
     time sync. Connect using either method under "How to connect to it"
     above.

--------------------------------------------------------------------------------
TROUBLESHOOTING NOTES
--------------------------------------------------------------------------------

  - "portapill.local won't load on Windows": check the current network is
    set to Private (not Public) under Settings > Network & Internet, some
    hostname resolution is blocked on Public networks. Typing the full
    "http://" prefix explicitly can also matter, some browsers try HTTPS
    first by default and fail silently against this device's plain-HTTP
    server.
  - "The dashboard was working, now nothing connects at all": pull
    http://<device-ip>/api/debug-log from a browser, this is a live dump
    of the ESP's recent internal log, readable over Wi-Fi with no USB
    cable needed, and is the fastest way to see what's actually
    happening (Wi-Fi events, socket activity, Pico link status all get
    logged with a "[NET]"/"[LINK]" prefix for easy scanning).
  - "ESP <-> Pico: NOT CONNECTED" in the log: check the UART wiring above
    (TX/RX/GND), confirm both boards share a common ground, and confirm
    the Pico is actually powered and running its own firmware.

================================================================================
