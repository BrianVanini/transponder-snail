#include <stdio.h>
#include "call.h"

#define RING_TRACK           1       // mp3/0001.mp3 "puru puru"
#define CLICK_TRACK          2       // mp3/0002.mp3 pick-up click
#define RING_RESEND_MS       1000    // caller re-sends RING this often
#define CALL_GIVEUP_MS       30000   // caller gives up after this long without an answer
#define RING_TIMEOUT_MS      3000    // callee stops ringing if no RING heard for this long
#define RING_REPLAY_MS       8000    // re-trigger the ring if the DFPlayer never says "done"
                                     // (only until the first "done" frame is seen; after that
                                     // the ring loops at its natural length)
#define DONE_GUARD_MS        500     // ignore duplicate "done" frames right after a play

// Open design question, decided here: a RING arrives while our top is already lifted.
//   1 = auto-answer right away (default).
//   0 = ring anyway; answer with PTT (or by setting the top down and lifting it again).
#define AUTO_ANSWER_WHEN_LIFTED 1

static const char *STATE_NAMES[] = { "IDLE", "CALLING", "RINGING", "CONNECTED" };

static const call_io_t *io;
static int self, count;
static call_state_t state = CALL_IDLE;
static int peer = -1;              // active peer (CONNECTED) or caller (RINGING)
static bool lifted, talking;
static uint32_t now_ms;            // time since boot, advanced by call_tick()
static uint32_t state_ms;          // when we entered the current state
static uint32_t ring_ms;           // CALLING: last RING sent; RINGING: last RING heard
static uint32_t play_ms;           // last time the ring sound was (re)started
static bool dfp_seen_done;         // DFPlayer has reported a finished track at least once

// Print "[call] OLD -> NEW (note name)" and switch state.
static void go(call_state_t s, const char *note, int who)
{
    printf("[call] %s -> %s", STATE_NAMES[state], STATE_NAMES[s]);
    if (note && who >= 0) printf(" (%s %s)", note, io->name(who));
    else if (note) printf(" (%s)", note);
    printf("\n");
    state = s;
    state_ms = now_ms;
    talking = false;
    if (s == CALL_IDLE) peer = -1;
}

static void send_all_except(int skip, msg_type_t type)
{
    for (int i = 0; i < count; i++) {
        if (i != self && i != skip) io->send(i, type);
    }
}

static void ring_start(void)
{
    io->play(RING_TRACK);
    play_ms = now_ms;
}

// Pick up a call from 'from' (we were RINGING, IDLE+lifted, or in a simultaneous call).
static void answer(int from, const char *note)
{
    io->play(CLICK_TRACK);   // also cuts off the ring
    io->send(from, MSG_ANSWER);
    if (state == CALL_CALLING) send_all_except(from, MSG_HANGUP);  // stop other ringers
    peer = from;
    go(CALL_CONNECTED, note, from);
}

void call_init(const call_io_t *call_io, int self_idx, int num_snails, bool is_lifted)
{
    io = call_io;
    self = self_idx;
    count = num_snails;
    lifted = is_lifted;
}

void call_hook(bool is_lifted)
{
    lifted = is_lifted;
    if (state == CALL_RINGING && lifted) {
        answer(peer, "answered");
    } else if (state == CALL_CALLING && !lifted) {
        send_all_except(-1, MSG_HANGUP);
        go(CALL_IDLE, "hung up", -1);
    } else if (state == CALL_CONNECTED && !lifted) {
        io->send(peer, MSG_HANGUP);
        go(CALL_IDLE, "hung up", -1);
    }
    // IDLE: lifting the top alone does nothing.
}

void call_ptt(bool pressed)
{
    switch (state) {
    case CALL_IDLE:
        if (!pressed) break;
        if (!lifted) {
            printf("(lift the top first to make a call)\n");
            break;
        }
        send_all_except(-1, MSG_RING);
        ring_ms = now_ms;
        go(CALL_CALLING, "ringing everyone", -1);
        break;
    case CALL_RINGING:
        if (pressed && lifted) answer(peer, "answered");  // only reachable if AUTO_ANSWER_WHEN_LIFTED is 0
        break;
    case CALL_CONNECTED:
        if (pressed) {
            printf("Talking...\n");
            io->send(peer, MSG_TALK_START);
            talking = true;
            // TODO(step 3/6): start streaming mic -> I2S -> ESP-NOW audio packets to 'peer' here.
        } else if (talking) {
            printf("Listening...\n");
            io->send(peer, MSG_TALK_STOP);
            talking = false;
            // TODO: stop the mic stream here.
        }
        break;
    case CALL_CALLING:
        break;
    }
}

static void on_ring(int from)
{
    switch (state) {
    case CALL_IDLE:
        if (lifted && AUTO_ANSWER_WHEN_LIFTED) {
            answer(from, "auto-answered");
        } else {
            peer = from;
            ring_ms = now_ms;
            ring_start();
            go(CALL_RINGING, "from", from);
        }
        break;
    case CALL_CALLING:
        // Both sides called at once. Lower SNAILS[] index answers; the other keeps calling
        // and connects when that ANSWER arrives, so both end up CONNECTED to each other.
        if (self < from) answer(from, "both called, answering");
        break;
    case CALL_RINGING:
        if (from == peer) ring_ms = now_ms;   // caller still there, keep ringing
        else io->send(from, MSG_BUSY);
        break;
    case CALL_CONNECTED:
        if (from == peer) io->send(from, MSG_ANSWER);  // our ANSWER was lost, resend it
        else io->send(from, MSG_BUSY);
        break;
    }
}

void call_msg(int from, msg_type_t type)
{
    if (from < 0 || from == self) return;
    switch (type) {
    case MSG_RING:
        on_ring(from);
        break;
    case MSG_ANSWER:
        if (state == CALL_CALLING) {
            io->play(CLICK_TRACK);
            peer = from;
            send_all_except(from, MSG_HANGUP);   // anyone else still ringing can stop
            go(CALL_CONNECTED, "answered by", from);
        } else if (!(state == CALL_CONNECTED && from == peer)) {
            io->send(from, MSG_HANGUP);          // too late: we gave up or someone else answered
        }
        break;
    case MSG_HANGUP:
        if (state == CALL_RINGING && from == peer) {
            io->stop();
            go(CALL_IDLE, "hung up by", from);
        } else if (state == CALL_CONNECTED && from == peer) {
            printf("Other side hung up\n");
            go(CALL_IDLE, "hung up by", from);
        }
        break;
    case MSG_BUSY:
        if (state == CALL_CALLING) printf("%s is busy\n", io->name(from));
        break;
    case MSG_TALK_START:
        if (state == CALL_CONNECTED && from == peer) printf("%s is talking\n", io->name(from));
        break;
    case MSG_TALK_STOP:
        if (state == CALL_CONNECTED && from == peer) printf("%s stopped\n", io->name(from));
        break;
    default:
        break;
    }
}

void call_track_done(void)
{
    dfp_seen_done = true;
    if (state == CALL_RINGING && now_ms - play_ms >= DONE_GUARD_MS) ring_start();
}

void call_tick(int ms)
{
    now_ms += ms;
    if (state == CALL_CALLING) {
        if (now_ms - state_ms >= CALL_GIVEUP_MS) {
            printf("No answer after %d s, giving up\n", CALL_GIVEUP_MS / 1000);
            send_all_except(-1, MSG_HANGUP);
            go(CALL_IDLE, "no answer", -1);
        } else if (now_ms - ring_ms >= RING_RESEND_MS) {
            send_all_except(-1, MSG_RING);
            ring_ms = now_ms;
        }
    } else if (state == CALL_RINGING) {
        if (now_ms - ring_ms >= RING_TIMEOUT_MS) {
            io->stop();
            go(CALL_IDLE, "caller gone", -1);
        } else if (!dfp_seen_done && now_ms - play_ms >= RING_REPLAY_MS) {
            ring_start();   // fallback for no DFPlayer (until the first "done" frame)
        }
    }
}
