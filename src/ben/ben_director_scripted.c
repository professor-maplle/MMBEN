#include "ben/ben.h"

// The scripted director: a deterministic policy written against BEN's instruction
// set. This is the fallback brain (and the only one until the Ollama bridge
// exists). It will grow into triggers + an escalating "mood" + weighted
// set-pieces; for milestone 1 it does the smallest possible thing that proves the
// whole spine: a short while after you take control, BEN notices you and speaks.

#define BEN_GREET_DELAY 90 // ~3s of gameplay before BEN's first words

static bool sGreeted;
static s32  sFrames;

// Copy a C string into a command's fixed inline buffer (no libc dependency).
static void ben_cmd_set_text(BenCommand* cmd, const char* text) {
    u32 i = 0;
    while (text[i] != '\0' && i < BEN_TEXT_MAX - 1) {
        cmd->text[i] = text[i];
        i++;
    }
    cmd->text[i] = '\0';
}

void ben_director_scripted_tick(PlayState* play) {
    (void)play; // milestone 1 reads no game state yet

    if (sGreeted) {
        return;
    }

    sFrames++;
    if (sFrames >= BEN_GREET_DELAY) {
        BenCommand cmd = { 0 };
        cmd.power = BEN_POWER_SPEAK;
        cmd.ix = 0; // 0 = let the executor apply its default display duration
        ben_cmd_set_text(&cmd, "i know you're there.");

        ben_queue_push(&cmd);
        sGreeted = true;
    }
}
