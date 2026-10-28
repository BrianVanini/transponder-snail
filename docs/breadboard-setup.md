# Breadboard Setup (current prototype)

How to rebuild the breadboard prototype as it stands now. Current firmware is the **ESP-NOW link test** (`firmware/main/main.c`) plus a DFPlayer ring test. Keep this file updated as parts are added.

## 1. Safety and breadboard basics

- **Unplug USB before you wire anything.** Plug it back in only after you've checked the wiring.
- Each numbered row has **two groups of 5 connected holes**: `a–e` and `f–j`. The center gap separates them, so `10a` connects to `10e` but **not** to `10f`.
- The **red + / blue − rails** run the long way down the board. Some boards split the rails halfway, so keep everything that uses a rail close together.
- Two parts are connected when their legs sit in the **same row group**.

```
        rail  a b c d e   f g h i j  rail
         - +                          + -
row 10   | |  o o o o o   o o o o o   | |
row 11   | |  o o o o o   o o o o o   | |
row 12   | |  o o o o o   o o o o o   | |
              \_______/   \_______/
              connected   connected
               (a–e)       (f–j)
```

**Common mistakes**
- Both legs of a part in the same row group. The part is shorted and does nothing.
- Off by one row. Count twice.
- Mixing up **IO32 and IO33**. They sit right next to each other on the ESP32.

## 2. ESP32 placement

The ESP32 DevKitC-32E is too wide for the breadboard, so it **stays off the breadboard**, on the table.

- Use **male-to-female jumper wires**: push the female end onto the ESP32 pin and the male end into the breadboard.
- Pin labels are printed next to each pin (`GND`, `IO32` or `32`, `IO33` or `33`).
- **Any GND pin works.**

## 3. Snail A (MAC `C8:85:41:2A:0B:98`, tape label **A**)

### Ground
ESP32 `GND` → blue **−** rail.

### PTT button (C&K D6R)
The button has 4 legs and is too narrow to straddle the center gap, so it goes on the `a–e` side and covers two rows (for example, rows 10 and 12, with legs in columns `b` and `d`).

The 4 legs form **two pairs that are connected inside the button**. Each row must hold one pair, so that pressing the button connects row 10 to row 12.

```
         a   b   c   d   e
row 10   o  [L]  o  [L]  o   <- IO32 wire goes in a free hole here (e.g. 10a)
row 11   o   |  BUTTON  |  o
row 12   o  [L]  o  [L]  o   <- wire from a free hole (e.g. 12e) to − rail
```

- `IO32` → a free hole in row 10.
- Row 12 → **−** rail.
- **If the button seems "always pressed"** (pressing it never prints `Calling B...`), the pairs are in the wrong orientation. Unplug USB, rotate the button 90° in place, and try again.

### Reed switch (hook)
- Put the two leads in **two different rows** on the `a–e` side (for example, rows 20 and 25).
- The glass breaks easily. Bend the leads gently, a few mm away from the glass body.
- `IO33` → row 20.
- Row 25 → **−** rail.
- Magnet near prints `Hook: down`. Magnet away prints `Hook: lifted`.

```
         a   b   c   d   e
row 20   o  [L]  o   o   o   <- reed lead 1 + IO33 wire in a free hole
          \  |
           (glass)
             |
row 25   o  [L]  o   o   o   <- reed lead 2 + wire to − rail
```

### Wiring summary (Snail A)

| ESP32 pin | Goes to | Via |
|---|---|---|
| GND | blue − rail | jumper |
| IO32 | row 10 | PTT button → row 12 → − rail |
| IO33 | row 20 | reed switch → row 25 → − rail |
| 5V | DFPlayer VCC | jumper (see section 7) |
| IO17 | DFPlayer RX | 1kΩ resistor |
| IO16 | DFPlayer TX | jumper |

No resistors are needed. The firmware turns on the ESP32's internal pull-ups.

## 4. Snail B (MAC `20:50:0D:E9:99:BC`, tape label **B**)

- ESP32 `GND` → **−** rail.
- `IO32` → its own PTT button, wired the same way as A's but in **different rows** (for example, rows 40 and 42). The other row goes to the **−** rail.
- **No reed switch yet.** B prints `Hook: lifted` at boot because nothing is connected to IO33. That's expected.
- If both boards share one breadboard, both GNDs can go to the same − rail. Keep the button rows separate.

| ESP32 pin | Goes to | Via |
|---|---|---|
| GND | blue − rail | jumper |
| IO32 | row 40 | PTT button → row 42 → − rail |

## 5. Power and flashing

- **Each board needs its own USB cable** (a laptop or a phone charger works).
- To flash in VS Code:
  1. Open the `firmware/` folder.
  2. Pick the serial port with **detect**. It looks like `/dev/cu.usbserial-*` or `/dev/tty.usbserial-*`, and the number can change with each board or USB port.
  3. Target: `esp32`. Flash method: **UART**.
  4. Press the 🔥 **Build, Flash and Monitor** button.
  5. Press `Ctrl+]` to exit the monitor.
- If it gets stuck at `Connecting...`, **hold the BOOT button** on the ESP32 until it starts writing.

## 6. Expected output and quick test

At boot:
```
Snail MAC: C8:85:41:2A:0B:98
I am Snail A
```

Every 2 seconds (shown here on A):
```
ping from B, RSSI -45 dBm
  -> B: delivered
```
If the other board is off, you'll see `-> B: FAILED`. These are just ESP-NOW pings to the other board failing because it's off, so ignore them during single-board tests. If it says `-> A: FAILED`, you're looking at board B (check the `I am Snail …` line).

- **Press a button**: that board prints `Calling B...` (or `Calling A...`), and the other board prints `*** Incoming call from A! ***` (with RSSI).
- **Move the magnet** near Snail A's reed switch and away again: A prints `Hook: down` / `Hook: lifted`.
- `UNKNOWN SNAIL` means this board's MAC isn't in the `SNAILS[]` table in `main.c`.

**RSSI guide** (signal strength):

| RSSI | Meaning |
|---|---|
| about −40 dBm | very close |
| below about −80 dBm | weak |
| about −90 dBm | packets start dropping |

## 7. DFPlayer Mini + base speaker (Snail A) — tested, working

**Status:** tested on Snail A. Pressing PTT plays `0001.mp3` through the base speaker.

The DFPlayer Mini (DFR0299) plays MP3 files from a microSD card. Its built-in 3W amp drives the base speaker (Visaton K28WPC) directly, so no extra amp is needed. The firmware plays `/mp3/0001.mp3` when you press PTT and when a RING arrives.

### Placement
The DFPlayer is a 16-pin module (8 pins per side). Put it **straddling the center gap**, like a chip, with the **microSD slot facing outward** so you can still insert the card. For example, use rows 30–37, with the left pins in column `e` and the right pins in column `f`.

There are no female-to-female jumpers on hand, so the DFPlayer **stays in the breadboard** and every connection to it goes through its breadboard rows.

Pin 1 (VCC) is marked on the board. Looking at the module with pin 1 at the top left:

| Row | Left side (`a–e`) | Right side (`f–j`) |
|---|---|---|
| 1 | VCC | BUSY |
| 2 | RX | USB− |
| 3 | TX | USB+ |
| 4 | DAC_R | ADKEY_2 |
| 5 | DAC_L | ADKEY_1 |
| 6 | SPK_1 | IO_2 |
| 7 | GND | GND |
| 8 | SPK_2 | IO_1 |

**Check this against the silkscreen or the DFRobot wiki pinout before powering up.** If the module is flipped, VCC and GND end up in the wrong places.

### Finding 5V on the ESP32
On the DevKitC-32E the pin is labeled **`5V`** (not VIN). It's the **last pin on the same header as IO32/IO33**, right next to the USB connector. Labels may be printed on the back of the board.

Header order, starting from the antenna end:
```
3V3, EN, VP, VN, 34, 35, 32, 33, 25, 26, 27, 14, 12, GND, 13, D2, D3, CMD, 5V
                                                                         ^ USB end
```
Fallback: **3V3 also works** (the DFPlayer runs on 3.2–5V), just quieter.

### Wiring
- **VCC** → ESP32 **5V** pin (it has power whenever USB is plugged in).
- **GND** → blue **−** rail.
- **RX** ← **1kΩ resistor** (brown-black-red) ← ESP32 `IO17`. Put the resistor between the DFPlayer RX row and an empty row, then run the IO17 wire to that empty row.
- **TX** → ESP32 `IO16` (plain jumper).
- **SPK_1** and **SPK_2** → the two speaker tabs. **Never connect a speaker wire to GND.**

```
ESP32 IO17 --[1kΩ]--> DFPlayer RX
ESP32 IO16 <--------- DFPlayer TX
ESP32 5V   ---------> DFPlayer VCC
− rail     ---------- DFPlayer GND
DFPlayer SPK_1 ---- speaker tab
DFPlayer SPK_2 ---- speaker tab
```

### How the speaker connection works
The speaker is **passive** (a coil and a magnet). It gets no data. The DFPlayer decodes the MP3, and its built-in amp pushes current out **SPK_1 → through the speaker coil → back into SPK_2**. The breadboard row just extends the DFPlayer's pin, so a jumper in the same 5-hole group as SPK_1 is connected to SPK_1.

**No-solder test:** put a male-to-male jumper in the SPK_1 row and another in the SPK_2 row. Press or tape each metal tip flat onto one speaker tab (the tabs are on the back and may be marked +/−; polarity doesn't matter for this test). **The two tips must not touch each other.** Alligator clips are better if you have them.

### microSD card checklist
- Exactly **`mp3/0001.mp3`** at the card root: 4 digits, folder named `mp3`.
- Format is **FAT32** (or FAT16), **not exFAT**.
- It's a real MP3, not a renamed `.m4a` or `.wav`.
- macOS adds hidden `._*` files and `.Spotlight-V100` / `.fseventsd` folders that can confuse the DFPlayer. Clean them, then eject:
  ```
  dot_clean -m /Volumes/<CARD>
  rm /Volumes/<CARD>/mp3/._*
  ```
- **Insert the card before powering up.** The DFPlayer reads it once at boot. The firmware waits 1.5 s for that before it sends any commands.

### Expected
At boot you may see `DFPlayer: got 0x3F` (card online). Pressing PTT prints `DFPlayer: play 0001`, and 0001.mp3 plays from the speaker. Receiving a RING from B plays it too.

Volume is set in firmware: `DFP_VOLUME` in `main.c` (0–30, default 20).

### DFPlayer reply codes
The log prints replies as `DFPlayer: got 0x.. (param ..)`.

| Code | Param | Meaning |
|---|---|---|
| 0x3F | – | Card online / initialized (good) |
| 0x40 | 1 | Busy / not ready yet |
| 0x40 | 3 | Serial receive error |
| 0x40 | 4 | Checksum error |
| 0x40 | 5 | Track number out of range |
| 0x40 | 6 | Track not found |
| 0x40 | 7 | Insertion error |
| 0x40 | 8 | SD card read failed |

"Prints `play 0001` but silent" together with `0x40 param 6` means **the file wasn't found**, not a volume problem. Go through the card checklist above.

### Troubleshooting (no sound)
- Check the log for a `0x40` code (table above).
- Speaker wires are firmly on SPK_1 and SPK_2, and the tips don't touch.
- TX/RX are not swapped: ESP32 **IO17** goes to DFPlayer **RX** through the resistor.
- VCC is on **5V** (3V3 works but is quieter).
- Give it about 2 s after power-up.

## 8. Not wired yet

- ICS-43434 mic and MAX98357A amp (their header pins need to be soldered first)
- Top speaker
- Power slide switch
