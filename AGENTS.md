# Transponder Snail (Den Den Mushi)

A pair of One Piece "Den Den Mushi" snail phones that talk directly to each other like walkie-talkies. Personal class project (no external requirements). 3D-printed shells, always-on, wall-powered.

## Architecture (per snail)

- **MCU:** ESP32 DevKitC-32E, firmware in **ESP-IDF** (VS Code extension), C.
- **Link:** **ESP-NOW** — direct ESP32-to-ESP32 packets addressed by MAC over the 2.4GHz radio. No router, internet, phone number, or service cost. Design should not hardcode "only two devices" — 3–4 snails may be added later (MAC list or broadcast).
- **Base:** ESP32, DFPlayer Mini + microSD, base speaker, reed switch (hook), push-to-talk button, power slide switch, MAX98357A amp.
- **Top piece** (lifts off like a handset): ICS-43434 I2S mic, top speaker, magnet that trips the reed switch.
- **Base-to-top cable:** ~1–2 ft, 8-conductor coiled cord. 7 wires used: mic VCC, GND, BCLK, WS, DOUT + speaker +/− (amp stays in the base). Use twisted pairs for I2S lines.

## Behavior

- Top lifted → reed switch reports off-hook. Hold PTT to talk (half-duplex).
- Voice path: mic → I2S → ESP32 → ESP-NOW → other ESP32 → I2S → MAX98357A → top speaker.
- Incoming transmission while top is down → play "puru puru" ring via DFPlayer on the base speaker.
- Lift top → stop ring, play click, route voice audio. Set top down → hang up (optional hang-up sound).
- Small ESP-NOW control messages for ring / answer / hang-up, separate from audio packets.
- **Undecided:** what happens if the receiving snail's top is already lifted when a call arrives (play immediately vs. still ring).

## Parts (DigiKey, ordered and arrived)

| Part | DigiKey # | Qty |
|---|---|---|
| DFPlayer Mini (DFR0299) | 1738-1041-ND | 2 |
| Visaton K28WPC 8Ω speaker (base) | 2056-K28WPCBL-8OHM-ND | 2 |
| Soberton SP-4018 8Ω speaker (top) | 433-SP-4018-ND | 2 |
| Reed switch ORD 624/10-15 AT | 374-ORD624/10-15AT-ND | 4 |
| microSD 512MB (Adafruit) | – | 2 |
| ESP32-DevKitC-32E | 356-ESP32-DEVKITC32E | 2 |
| ICS-43434 I2S mic breakout (Adafruit 6049) | – | 2 |
| PTT tactile button C&K D6R00 F1 LFS | 108-D6R00F1LFS-ND | 4 |
| Slide switch L101011MS02Q | CKC5106-ND | 2 |
| MAX98357A I2S amp breakout (Adafruit 3006) | 1528-1696-ND | 2 |

Also on hand: RexQualis electronics kit (breadboard, jumpers, resistors, power module), rare-earth magnets, soldering iron and 3D printer (via a friend). Two STM32 boards (incl. WB55RG) are **not used** — WB55 is BLE-only.

## Hardware gotchas

- ICS-43434 is **3.3V only** and bottom-ported — the port hole faces the speaker's mouth.
- I2S pin choices on the ESP32 matter; check the mic/amp docs. Mic and amp can be on separate I2S peripherals.
- DFPlayer: FAT16/FAT32 card, files named like `0001.mp3`, 1kΩ resistor between ESP32 TX and DFPlayer RX. Drives the base speaker directly (built-in amp). `0x40 param 6` = track not found; macOS hidden `._` files on the card can cause it.
- MAX98357A output goes straight to the speaker — never to another amp or to ground.
- Reed switch: read with internal pull-up. Rare-earth magnets may trigger from too far away. A MagSafe charger was only a weak test magnet; measure reed trigger distance with a ~10 mm neodymium disc before fixing shell wall thickness.
- **Range risk:** a concrete wall separates the two sides of the house. Test ESP-NOW through it early. Fixes in order: raise TX power → U.FL-antenna ESP32 boards → route over home WiFi instead.

## Repo layout

- `firmware/` — ESP-IDF project (open this folder in VS Code). `main/main.c` is the entry point (`app_main`).
- Board shows up on macOS as `/dev/cu.usbserial-*`; target chip is `esp32`.
- `docs/breadboard-setup.md` — how to rebuild the current breadboard prototype (keep it updated as parts are added).

## Boards

- Snail A — `C8:85:41:2A:0B:98`
- Snail B — `20:50:0D:E9:99:BC`

Boards are labeled with tape. The MAC table (`SNAILS[]`) in `firmware/main/main.c` must match; a new snail needs a row added there.

## Firmware roadmap

Step 6 (ESP-NOW) is being done before 3–5 because the mic/amp headers still need soldering.

1. Toolchain check: build/flash/monitor hello world (`"Snail alive, count = N"`). ✅
2. GPIO: reed switch + PTT button. ✅
3. I2S mic capture (ICS-43434).
4. I2S speaker output (MAX98357A).
5. DFPlayer control over UART (ring, click). Playback verified on hardware (0001.mp3 via PTT, Snail A); real ring/click sound files still to add.
6. ESP-NOW between two boards (control messages, then audio).
7. Full call state machine (idle / ringing / connected / hang-up).

Test each subsystem on a breadboard before anything goes in a shell.

## Out of scope / rejected

GSM/SIM (monthly cost), Bluetooth HFP to a phone (BT401 etc. — just mirrors a phone), Pi + SIP, gutted FRS walkie-talkies, PAM8403 amp (replaced by MAX98357A). Rotary dial / keypad dropped for now — possible stretch goal.

## Developer context

The builder is new to electronics and embedded work. Explain wiring and code steps clearly, and prefer well-documented, beginner-friendly approaches.
