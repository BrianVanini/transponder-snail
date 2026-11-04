# Testing the Call State Machine (two boards)

How to check the call logic in `firmware/main/call.c` with Snail A and Snail B on the breadboard. There's no voice audio yet: "talking" only prints messages. Each test below says what to do and which lines each board should print.

## 1. Setup

### Hook on both boards: reed switch or a jumper wire
The firmware reads the hook on **IO33**:
- IO33 connected to GND = **top DOWN** (on hook)
- IO33 floating (nothing connected) = **top LIFTED** (off hook)

**Snail A** has a reed switch: magnet near = down, magnet away = lifted.

**Snail B has no reed switch**, so use a jumper wire as a fake hook:
- Jumper from **IO33 → − rail (GND)** = top **DOWN**
- **Pull the jumper out** = top **LIFTED**

A jumper works on A too if the magnet is awkward. Each change prints `Hook: down` or `Hook: lifted`.

### DFPlayer
- Only Snail A has a DFPlayer. Snail B still prints `DFPlayer: play 0001` and so on; it just makes no sound. That's fine.
- A's microSD card needs **`mp3/0001.mp3`** (ring) and **`mp3/0002.mp3`** (click). Clean the card with `dot_clean` as in `breadboard-setup.md` section 7.

### Flash both boards from this branch
```
git switch call-state-machine                 # on the laptop that already has it
git fetch && git switch call-state-machine    # teammate's laptop
```
Then in VS Code, open `firmware/` and **Build, Flash and Monitor** each board. Do this one board at a time, picking each board's own `/dev/cu.usbserial-*` port. Keep both monitors open side by side.

### Reading the logs
- Every state change prints one line: `[call] OLD -> NEW (reason)`.
- Every 5 s you'll also see `ping from B, RSSI -45 dBm` and `-> B: delivered`. These are range diagnostics, so ignore them here. To hide them, set `DEBUG_PINGS` to `0` in `main.c`.
- `-> B: FAILED` means a packet didn't arrive. A few are fine, but lots of them means range or power problems.

Start every test with **both tops DOWN** and both boards in IDLE. After a reboot, no `[call]` line has printed yet, which means IDLE.

## 2. Test cases

### 1. Call A → B and answer
1. On A: lift the top, then press and release PTT.
2. On B: lift the top (pull the jumper).

| When | Snail A prints | Snail B prints |
|---|---|---|
| A lifts | `Hook: lifted` | |
| A presses PTT | `[call] IDLE -> CALLING (ringing everyone)` | `DFPlayer: play 0001`<br>`[call] IDLE -> RINGING (from A)` |
| while waiting | | `DFPlayer: play 0001` again every ~8 s (no DFPlayer, so the fallback timer replays it) |
| B lifts | `DFPlayer: play 0002`<br>`[call] CALLING -> CONNECTED (answered by B)` | `Hook: lifted`<br>`DFPlayer: play 0002`<br>`[call] RINGING -> CONNECTED (answered A)` |

Try it the other way too (B calls, A answers). A's ring should repeat each time the track ends.

### 2. Talk toggle (while connected)
| When | Snail A prints | Snail B prints |
|---|---|---|
| A holds PTT | `Talking...` | `A is talking` |
| A releases PTT | `Listening...` | `A stopped` |

It works the same from B.

### 3. Hang up (try from each side)
B puts its top down (jumper back in):

| Snail A prints | Snail B prints |
|---|---|
| `Other side hung up`<br>`[call] CONNECTED -> IDLE (hung up by B)` | `Hook: down`<br>`[call] CONNECTED -> IDLE (hung up)` |

Reconnect (test 1), then hang up from A. The lines are the same with A and B swapped.

### 4. Caller gives up (30 s timeout)
A lifts and presses PTT. B **leaves the top down** and lets it ring.

| When | Snail A prints | Snail B prints |
|---|---|---|
| start | `[call] IDLE -> CALLING (ringing everyone)` | `[call] IDLE -> RINGING (from A)` |
| ~30 s later | `No answer after 30 s, giving up`<br>`[call] CALLING -> IDLE (no answer)` | `DFPlayer: stop`<br>`[call] RINGING -> IDLE (hung up by A)` |

If A's HANGUP packet is lost, B stops by itself about 3 s later with `[call] RINGING -> IDLE (caller gone)`.

### 5. Caller hangs up while B is ringing
A calls (lift, PTT). While B rings, A puts the top down.

| Snail A prints | Snail B prints |
|---|---|
| `Hook: down`<br>`[call] CALLING -> IDLE (hung up)` | `DFPlayer: stop`<br>`[call] RINGING -> IDLE (hung up by A)` |

You can also unplug A while B rings. B should print `[call] RINGING -> IDLE (caller gone)` within ~3 s.

### 6. B already lifted → auto-answer
Pull B's jumper (top lifted) **before** A calls. Then A lifts and presses PTT.

| Snail A prints | Snail B prints |
|---|---|
| `[call] IDLE -> CALLING (ringing everyone)`<br>`DFPlayer: play 0002`<br>`[call] CALLING -> CONNECTED (answered by B)` | `DFPlayer: play 0002`<br>`[call] IDLE -> CONNECTED (auto-answered A)` |

B does not ring. To make a lifted snail ring instead, set `AUTO_ANSWER_WHEN_LIFTED` to `0` in `call.c`.

### 7. Calling a busy snail / both calling at once
A real "busy" reply needs a **third** snail. With only two boards, the closest test is both snails calling each other at the same moment:
1. Lift both tops.
2. Press PTT on both boards within about half a second of each other.

The lower `SNAILS[]` index (A) answers:

| Snail A prints | Snail B prints |
|---|---|
| `[call] IDLE -> CALLING (ringing everyone)`<br>`DFPlayer: play 0002`<br>`[call] CALLING -> CONNECTED (both called, answering B)` | `[call] IDLE -> CALLING (ringing everyone)`<br>`DFPlayer: play 0002`<br>`[call] CALLING -> CONNECTED (answered by A)` |

If one press lands clearly first, the other board auto-answers instead (test 6). Both outcomes end with the two boards CONNECTED to each other.

Also check: pressing PTT with the top **down** prints `(lift the top first to make a call)` and nothing else happens.

With a third snail (add its row to `SNAILS[]`): while A and B are connected, C calls. A and B ignore the call, reply BUSY, and C prints `A is busy` / `B is busy`. C gives up after 30 s.

## 3. Troubleshooting

- **Nothing happens when I press PTT:** check that the top is lifted. You should have seen `Hook: lifted`. On B, the jumper must be **out**.
- **B auto-answers when it should ring:** B's top reads as lifted. Put the IO33 → GND jumper in and confirm `Hook: down`.
- **B never prints `RINGING`:** look on A for `-> B: FAILED`. Both boards must run this branch, because older firmware ignores the new messages. Check the RSSI on the ping lines.
- **The ring plays once and stops on A:** it should replay when the DFPlayer reports the track finished (`DFPlayer: got 0x3D`). If no `0x3D` ever arrives, check the DFPlayer TX → IO16 wire. Until the first `0x3D` arrives, the fallback replays every 8 s anyway.
- **The ring gets cut off:** with a DFPlayer the ring loops at its natural length. The timed fallback (8 s, `RING_REPLAY_MS` in `call.c`) only applies until the DFPlayer reports its first finished track, or on boards without a DFPlayer. So only the very first ring can be cut off, and only if the file is longer than 8 s. Shorten it, or raise `RING_REPLAY_MS`.
- **No click on answer:** `mp3/0002.mp3` is missing. Look for `DFPlayer: got 0x40 (param 6)`.
- **`UNKNOWN SNAIL`:** this board's MAC isn't in `SNAILS[]` in `main.c`.
- **Stuck in CONNECTED after the other board was unplugged:** expected for now (there's no keep-alive). Put the top down to hang up.
