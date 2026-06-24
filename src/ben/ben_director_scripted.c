#include "ben/ben.h"

// The scripted director: a deterministic policy written against BEN's instruction
// set. This is the fallback brain (and the only one until the Ollama bridge
// exists). It will grow into triggers + an escalating "mood" + weighted
// set-pieces; for now it does the smallest meaningful thing: once the player has
// been in real control of the world for a few seconds, BEN notices them and
// speaks through a native textbox.

#define BEN_GREET_CONTROL_FRAMES 120 // ~4s of sustained player control before greeting

static bool sGreeted;
static s32  sControlFrames;

// The seed of BEN's "senses": is the player actually roaming the world right now,
// as opposed to a cutscene (incl. the intro), a scene transition, a textbox, or
// otherwise out of direct control? Scene-agnostic on purpose.
static bool ben_player_in_control(PlayState* play) {
    return (play->msgCtx.msgMode == MSGMODE_NONE)        // no message on screen
        && (play->csCtx.state == CS_STATE_IDLE)          // no cutscene playing
        && (play->transitionTrigger == TRANS_TRIGGER_OFF) // not mid scene transition
        && !Player_InCsMode(play);                        // player not locked by a cs
}

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
    if (sGreeted) {
        return;
    }

    // Only sustained, genuine control counts: any cutscene/transition/message
    // resets the timer, so BEN won't intrude until the player is truly free.
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
