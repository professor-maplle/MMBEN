#ifndef BEN_COMMAND_H
#define BEN_COMMAND_H

// BEN's instruction set.
//
// A `BenCommand` is one atomic thing BEN can do to the game. Every "director"
// (the scripted policy now; the Ollama brain via the native bridge later) speaks
// only in BenCommands, and the executor is the only code that turns them into
// real in-game effects. This shared vocabulary is what keeps scripted mode and
// AI mode from diverging: same instruction set, different author.
//
// Types (s32/f32/u32/bool) come from the decomp headers; include "ben/ben.h"
// (which pulls in global.h) before this header.

#define BEN_TEXT_MAX 160

typedef enum {
    BEN_POWER_NONE = 0,
    BEN_POWER_SPEAK,        // overlay line of dialogue. text = line, ix = frames to show
    BEN_POWER_TEXTBOX,      // speak through the game's native dialogue box. text = line
    BEN_POWER_HUSH,         // hide the dialogue overlay
    BEN_POWER_SPAWN_STATUE, // spawn BEN's Elegy of Emptiness statue by the player
    BEN_POWER_COUNT
} BenPowerId;

// A single instruction. Kept fixed-size and trivially copyable on purpose: the
// native bridge will eventually push these straight into game RAM across the
// MIPS boundary, so it can hold no pointers into host memory.
typedef struct {
    BenPowerId power;
    s32        ix;               // generic integer param (e.g. duration in frames)
    f32        fx;               // generic float param (reserved: intensity, etc.)
    char       text[BEN_TEXT_MAX]; // inline text payload (SPEAK)
} BenCommand;

// Single-producer / single-consumer ring buffer.
// Milestone 1: producer (scripted director) and consumer (executor) both run on
// the game thread, so no locking is needed. When the native bridge becomes a
// second producer (milestone 2) this gains atomic head/tail indices.
void ben_queue_init(void);

// Queue a command. Returns false (and drops the command) if the queue is full.
bool ben_queue_push(const BenCommand* cmd);

// Pop the oldest command into `out`. Returns false if the queue is empty.
bool ben_queue_pop(BenCommand* out);

// Copy a C string into a command's fixed inline text buffer (truncates to fit,
// always NUL-terminated). No libc dependency.
void ben_cmd_set_text(BenCommand* cmd, const char* text);

#endif
