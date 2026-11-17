# Transponder Snail (Den Den Mushi)

A pair of One Piece "Den Den Mushi" snail phones that talk directly to each other like walkie-talkies. Personal class project (no external requirements). 3D-printed shells, always-on, wall-powered.

## Architecture (per snail)

- **MCU:** ESP32 DevKitC-32E, firmware in **ESP-IDF** (VS Code extension), C.
- **Link:** **ESP-NOW** — direct ESP32-to-ESP32 packets addressed by MAC over the 2.4GHz radio. No router, internet, phone number, or service cost. Design should not hardcode "only two devices" — 3–4 snails may be added later (MAC list or broadcast).
- **Base:** ESP32, DFPlayer Mini + microSD, base speaker, reed switch (hook), push-to-talk button, power slide switch, MAX98357A amp.
- **Top piece** (lifts off like a handset): ICS-43434 I2S mic, top speaker, magnet that trips the reed switch.
- **Base-to-top cable:** ~1–2 ft, 8-conductor coiled cord. 7 wires used: mic VCC, GND, BCLK, WS, DOUT + speaker +/− (amp stays in the base). Use twisted pairs for I2S lines.

**Pin map** (same on every snail; I2S pins chosen, not yet wired):

| GPIO | Use |
|---|---|
| IO32 | PTT button (to GND, internal pull-up) |
| IO33 | Hook reed switch (to GND, internal pull-up) |
| IO17 / IO16 | UART2 TX (→ 1kΩ → DFPlayer RX) / RX (← DFPlayer TX) |
| IO26 / IO25 / IO35 | Mic I2S0 BCLK / WS / DOUT (IO35 input-only) |
| IO18 / IO19 / IO21 | Amp I2S1 BCLK / LRC / DIN |
| 5V | DFPlayer VCC, amp Vin |
| 3V3 | Mic 3V (never 5V) |

## Behavior

- Top lifted → reed switch reports off-hook. Hold PTT to talk (half-duplex).
- Voice path: mic → I2S → ESP32 → ESP-NOW → other ESP32 → I2S → MAX98357A → top speaker.
- Incoming transmission while top is down → play "puru puru" ring via DFPlayer on the base speaker.
- Lift top → stop ring, play click, route voice audio. Set top down → hang up (optional hang-up sound).
- Small ESP-NOW control messages for ring / answer / hang-up, separate from audio packets.
- If the receiving snail's top is already lifted when a call arrives, it **auto-answers** (`AUTO_ANSWER_WHEN_LIFTED` in `call.c`; set 0 to ring instead).

Call flow (`firmware/main/call.c`, states IDLE / CALLING / RINGING / CONNECTED):
- Lift top, press PTT → RING is broadcast to every snail in `SNAILS[]` (re-sent every 1 s, gives up after 30 s) → CALLING.
- A snail with its top down rings (track 1, repeated) → RINGING; it stops after ~3 s with no RING.
- Lifting the top answers (click, MSG_ANSWER) → CONNECTED. The caller connects to the first answer and sends HANGUP to everyone else, so other ringers stop.
- Busy snails reply MSG_BUSY. If two snails call each other at once, the lower `SNAILS[]` index answers.
- In CONNECTED, PTT sends TALK_START / TALK_STOP (no audio yet). Setting the top down sends HANGUP → IDLE on both sides.
- Keep-alive: while CONNECTED each snail sends MSG_ALIVE every 1 s; if nothing is heard from the peer for 5 s (e.g. it lost power), it hangs up → IDLE (`ALIVE_SEND_MS` / `ALIVE_TIMEOUT_MS` in `call.c`). Both snails must run firmware with this, or the call drops after 5 s.

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
- I2S: mic on I2S0, amp on I2S1 (independent, half-duplex anyway). Strapping pins (0/2/5/12/15), flash pins (6–11) and IO14 (boot PWM) are avoided. Mic 3V → 3V3 only; SEL unconnected = left channel. Amp Vin on 5V (louder); GAIN and SD unconnected (9 dB, (L+R)/2 mono). Wiring: `docs/breadboard-setup.md` section 8.
- ICS-43434 driver: 64 BCLK per WS frame (32-bit slots, stereo), 24-bit data in left slot. Valid fs ≈ 6.25–18.75 kHz (low-power) or 23–51.6 kHz; use 16 kHz, avoid 18.75–23 kHz.
- DFPlayer: FAT16/FAT32 card, files named like `0001.mp3`, 1kΩ resistor between ESP32 TX and DFPlayer RX. Drives the base speaker directly (built-in amp). Tracks: `RING_TRACK` = `mp3/0001.mp3`, `CLICK_TRACK` = `mp3/0002.mp3`. `0x40 param 6` = track not found; macOS hidden `._` files on the card can cause it.
- MAX98357A output goes straight to the speaker — never to another amp or to ground.
- Reed switch: read with internal pull-up. Rare-earth magnets may trigger from too far away. A MagSafe charger was only a weak test magnet; measure reed trigger distance with a ~10 mm neodymium disc before fixing shell wall thickness.
- **Range (solved):** ESP-NOW tested fine through the concrete wall between the two sides of the house and several interior walls, so range is no longer a risk. If it ever gets worse: raise TX power → U.FL-antenna ESP32 boards → route over home WiFi.

## Repo layout

- `firmware/` — ESP-IDF project (open this folder in VS Code). `main/main.c` is the entry point (`app_main`).
- Board shows up on macOS as `/dev/cu.usbserial-*`; target chip is `esp32`.
- `firmware/main/call.c` — call state machine; `main.c` is wiring (inputs, radio, DFPlayer).
- `docs/testing-call-state-machine.md` — two-board test plan for the call flow.
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
6. ESP-NOW between two boards (control messages, then audio). Link verified on hardware: RING + pings work in both directions (Snail A ↔ B) through several interior walls. Control messages done (RING / ANSWER / HANGUP / BUSY / TALK_START / TALK_STOP); audio still to do.
7. Full call state machine (idle / calling / ringing / connected / hang-up). Implemented in `call.c` and verified on hardware with two boards (ring, answer, talk, hang up, timeout, auto-answer, simultaneous call), using jumper wires on IO33 as the hook.

Test each subsystem on a breadboard before anything goes in a shell.

## Out of scope / rejected

GSM/SIM (monthly cost), Bluetooth HFP to a phone (BT401 etc. — just mirrors a phone), Pi + SIP, gutted FRS walkie-talkies, PAM8403 amp (replaced by MAX98357A). Rotary dial / keypad dropped for now — possible stretch goal.

## Developer context

The builder is new to electronics and embedded work. Explain wiring and code steps clearly, and prefer well-documented, beginner-friendly approaches.
