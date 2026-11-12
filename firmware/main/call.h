#pragma once

#include <stdbool.h>
#include <stdint.h>

// Call state machine: IDLE / CALLING / RINGING / CONNECTED.
// main.c feeds it events (hook, PTT, radio messages, DFPlayer "track done", time)
// and it calls back through call_io_t to send messages and play sounds.

// Over-the-air message types (shared with the radio code in main.c).
typedef enum {
    MSG_RING = 1,     // "pick up!" (caller re-sends every second)
    MSG_PING,         // range/RSSI diagnostics, not used by the call logic
    MSG_ANSWER,       // callee picked up
    MSG_HANGUP,       // call is over (or "stop ringing, someone else answered")
    MSG_BUSY,         // reply to a RING while we're in another call
    MSG_TALK_START,   // PTT pressed while connected
    MSG_TALK_STOP,    // PTT released while connected
} msg_type_t;

typedef enum {
    CALL_IDLE,
    CALL_CALLING,     // we rang everyone, waiting for an answer
    CALL_RINGING,     // someone is ringing us
    CALL_CONNECTED,
} call_state_t;

// Peers are identified by their index in SNAILS[] (main.c).
typedef struct {
    void (*send)(int peer, msg_type_t type);
    void (*play)(uint16_t track);   // plays /mp3/000n.mp3
    void (*stop)(void);
    const char *(*name)(int peer);
} call_io_t;

void call_init(const call_io_t *io, int self_idx, int num_snails, bool lifted);
void call_hook(bool lifted);                 // debounced hook change
void call_ptt(bool pressed);                 // debounced PTT change
void call_msg(int from, msg_type_t type);    // from = SNAILS[] index, -1 if unknown
void call_track_done(void);                  // DFPlayer finished a track
void call_tick(int ms);                      // call every loop pass with elapsed ms
