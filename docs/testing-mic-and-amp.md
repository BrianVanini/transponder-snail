# Testing the mic and amp (AUDIO_TEST modes)

How to check the ICS-43434 mic and the MAX98357A amp + top speaker on the breadboard, one at a time, before any voice goes over the radio. Wiring is in [breadboard-setup.md section 8](breadboard-setup.md#8-mic-and-amp-after-headers-are-soldered).

The code is in `firmware/main/audio.c` (the drivers) and the bottom of `firmware/main/main.c` (the tests).

## Turning a test on

1. Open `firmware/main/main.c` and find:
   ```c
   #define AUDIO_TEST    0
   ```
2. Change `0` to the mode you want (table below), save.
3. Build, flash and monitor as usual (VS Code: the flame "Build, Flash and Monitor" button, or `idf.py -p /dev/cu.usbserial-XXXX flash monitor`).
4. **When you're done, set it back to `0`** and flash again. While a test mode is on, the board is *not* a snail: no radio, no calls, no DFPlayer.

| Mode | Name | Needs | What you do |
|---|---|---|---|
| 0 | normal | — | normal snail firmware |
| 1 | BEEP | amp + top speaker, PTT | hold PTT → 1 kHz tone |
| 2 | MIC METER | mic | talk/tap near the mic, watch the numbers |
| 3 | LOOPBACK | mic + amp + speaker, PTT | hold PTT → hear yourself |

Volume: `TEST_VOLUME` in `main.c` (0–100 %, default 25). Start low. The top speaker is small.

Every mode first prints the pin map it is using, e.g.:
```
Audio test mode 1
  Mic (I2S0): BCLK=IO26  WS/LRCL=IO25  DOUT->IO35   3V->3V3 (NOT 5V), SEL unconnected
  Amp (I2S1): BCLK=IO18  LRC=IO19  DIN=IO21   Vin->5V, GAIN/SD unconnected
  PTT: IO32   Sample rate 16000 Hz   Volume 25%
```
Compare it with your wires.

## Mode 1: BEEP (test the amp first)

Hold PTT: the monitor prints `Beep on` and the top speaker plays a steady, clean 1 kHz tone (a "test tone" beep). Release: `Beep off`, and the tone stops within about 0.1 s.

**No sound:**
- Amp `Vin` → ESP32 **5V**, amp `GND` → ESP32 GND.
- `BCLK` → IO18, `LRC` → IO19, `DIN` → IO21. Swapped BCLK/LRC = silence or garbage.
- Both speaker wires screwed tight into the amp's `+`/`−` terminals (never to GND).
- `SD` must be unconnected (SD to GND turns the amp off).
- `Beep on` doesn't print? That's the PTT button (IO32), not the amp.

**Crackle, or sound that cuts in and out:** the headers are loose (see "Press-fit caveat" below). Press the amp board down into the breadboard.

## Mode 2: MIC METER

Five times a second it prints a line like:
```
mic peak   812 rms    95 |##########     |
```
- **peak**: the loudest sample in the last 0.2 s. **rms**: the average loudness.
- The bar has one `#` per doubling of the peak, so it moves a lot even for small changes.
- Quiet room: a few `#` (a small, changing number, not exactly 0). Talking close to the mic hole (bottom of the board): the bar jumps up. A tap or clap near it: close to full.

Hints printed at the end of the line:

| Hint | Meaning | Check |
|---|---|---|
| `mic: no data (check DOUT->IO35, 3V, GND, BCLK, WS)` | every sample was exactly 0 | `DOUT` → IO35, mic `3V` → **3V3**, GND, `BCLK` → IO26, `LRCL` → IO25 |
| `mic: stuck line ...` | every sample was the same value (often -1 = data line stuck high) | `DOUT` wire, GND |
| `mic: data in the other slot ...` | the mic is talking in the right slot instead of the left | tie `SEL` to GND, or set `MIC_SLOT` to 1 in `audio.c` |
| `dropouts 3/20 (loose contact? ...)` | some of the 20 blocks in this line had no data | a wire or header pin is making on/off contact |

If the numbers never drop near 0 even in a quiet room, or they're stuck at `32767`, the data is probably misaligned. Note it and tell the firmware side; `MIC_GAIN_SHIFT` in `audio.c` sets how much the mic is boosted (each +1 = twice as loud).

## Mode 3: LOOPBACK

Hold PTT and talk: you hear yourself through the top speaker, with a tiny delay. Release: silence.

> ⚠️ **Feedback squeal.** If the speaker can "hear itself" through the mic, it builds into a loud squeal. Point the speaker away from the mic, keep them at least a hand apart, and keep `TEST_VOLUME` low. Release PTT to stop it at once.

Do mode 1 and mode 2 first, so you know each half works on its own.

## Press-fit caveat (unsoldered headers)

If the header pins are only pushed through the breakout's holes and not soldered, contact is **intermittent**: it works when pressed, fails when not. Symptoms:
- mic meter flips between normal numbers and `no data`, or shows `dropouts N/20`
- crackle, pops, or the beep cutting in and out

Use the mic meter as a contact tester: watch the output while pressing the breakout down and gently wiggling it. If the hints come and go with your finger, it's the contact, not the code. The real fix is soldering the headers (section 8 of the breadboard guide).

## Troubleshooting summary

| Symptom | Likely cause |
|---|---|
| No sound in mode 1 | amp Vin/GND, BCLK/LRC/DIN wires, speaker not in the terminal, SD grounded |
| Mic meter: `no data` | DOUT → IO35, mic 3V → 3V3, GND |
| Crackle / dropouts | loose press-fit contact: press the board down, solder the headers |
| Squeal in mode 3 | feedback: move the speaker away from the mic, lower `TEST_VOLUME` |
| Nothing prints at all | wrong serial port, or the board is stuck rebooting (look for a panic message) |
