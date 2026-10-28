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
If the other board is off, you'll see `-> B: FAILED`.

- **Press a button**: that board prints `Calling B...` (or `Calling A...`), and the other board prints `*** Incoming call from A! ***` (with RSSI).
- **Move the magnet** near Snail A's reed switch and away again: A prints `Hook: down` / `Hook: lifted`.
- `UNKNOWN SNAIL` means this board's MAC isn't in the `SNAILS[]` table in `main.c`.

**RSSI guide** (signal strength):

| RSSI | Meaning |
|---|---|
| about −40 dBm | very close |
| below about −80 dBm | weak |
| about −90 dBm | packets start dropping |

## 7. DFPlayer Mini + base speaker (Snail A)

The DFPlayer Mini (DFR0299) plays MP3 files from a microSD card. Its built-in 3W amp drives the base speaker (Visaton K28WPC) directly, so no extra amp is needed. The firmware plays `/mp3/0001.mp3` when you press PTT and when a RING arrives.

### Placement
The DFPlayer is a 16-pin module (8 pins per side). Put it **straddling the center gap**, like a chip, with the **microSD slot facing outward** so you can still insert the card. For example, use rows 30–37, with the left pins in column `e` and the right pins in column `f`.

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

### Wiring
- **VCC** → ESP32 **5V** pin (labeled `5V` or `VIN`; it has power whenever USB is plugged in). Do **not** use 3V3.
- **GND** → blue **−** rail.
- **RX** ← **1kΩ resistor** (brown-black-red) ← ESP32 `IO17`. Put the resistor between the DFPlayer RX row and an empty row, then run the IO17 wire to that empty row.
- **TX** → ESP32 `IO16` (plain jumper).
- **SPK_1** and **SPK_2** → the two speaker pads (polarity doesn't matter for this test). **Never connect a speaker wire to GND.**

Speaker without soldering: use alligator clips, or strip a jumper end, wrap it tightly around each pad, and tape it down.

```
ESP32 IO17 --[1kΩ]--> DFPlayer RX
ESP32 IO16 <--------- DFPlayer TX
ESP32 5V   ---------> DFPlayer VCC
− rail     ---------- DFPlayer GND
DFPlayer SPK_1 ---- speaker pad
DFPlayer SPK_2 ---- speaker pad
```

### microSD card
- Format it as **FAT32** (FAT16 also works).
- Make a folder named `mp3` at the root and put a file named `0001.mp3` in it.
- **Insert the card before powering up.** The DFPlayer reads it once at boot. The firmware waits 1.5 s for that before it sends any commands.

### Expected
At boot you may see `DFPlayer: got 0x3F` (card online). Pressing PTT prints `DFPlayer: play 0001`, and 0001.mp3 plays from the speaker. Receiving a RING from B plays it too.

### Troubleshooting (no sound)
- Card: folder is `mp3` and the file is `0001.mp3` (not `1.mp3`). The card is FAT32.
- Speaker wires are firmly on SPK_1 and SPK_2.
- TX/RX are not swapped: ESP32 **IO17** goes to DFPlayer **RX** through the resistor.
- VCC is on **5V**, not 3.3V.
- Give it about 2 s after power-up. `DFPlayer: got 0x40` means the DFPlayer reported an error, usually a card problem.

## 8. Not wired yet

- ICS-43434 mic and MAX98357A amp (their header pins need to be soldered first)
- Top speaker
- Power slide switch
