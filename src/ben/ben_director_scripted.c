#include "ben/ben.h"

// Scripted director: deterministic fallback brain (used when brain_mode is
// "Scripted", or whenever the AI bridge isn't driving). It will grow into
// triggers + an escalating "mood" + weighted set-pieces; for now it greets once
// the player has had sustained real control for a few seconds.

#define BEN_GREET_CONTROL_FRAMES 120 // ~4s of sustained control before greeting

static bool sGreeted;
static s32  sControlFrames;

void ben_director_scripted_tick(PlayState* play) {
    if (sGreeted) {
        return;
    }

    // Only sustained, genuine control counts; cutscene/transition/message resets.
    if (!ben_player_in_control(play)) {
        sControlFrames = 0;
        return;
    }

    sControlFrames++;
    if (sControlFrames < BEN_GREET_CONTROL_FRAMES) {
        return;
    }

    BenCommand cmd = { 0 };
    cmd.power = BEN_POWER_TEXTBOX;
    ben_cmd_set_text(&cmd, "i know you're there.");

    ben_queue_push(&cmd);
    sGreeted = true;
}
