#include "ben/ben.h"

// The executor is the single choke point between BEN's directors and the game.
// It runs once per frame on the game thread, drains every queued command, and
// dispatches each to the matching power. Because every effect flows through here,
// it's also where guardrails live: clamping parameters and guaranteeing that
// anything which obstructs the player eventually releases on its own.

#define BEN_SPEAK_DEFAULT_FRAMES 150 // ~5s
#define BEN_SPEAK_MAX_FRAMES     600 // hard cap so a line never lingers forever

// Frames remaining before BEN's current line is auto-hidden (0 = nothing shown).
static s32 sSpeakTimer;

void ben_executor_tick(void) {
    BenCommand cmd;

    while (ben_queue_pop(&cmd)) {
        switch (cmd.power) {
            case BEN_POWER_SPEAK: {
                s32 duration = cmd.ix;
                if (duration <= 0) {
                    duration = BEN_SPEAK_DEFAULT_FRAMES;
                } else if (duration > BEN_SPEAK_MAX_FRAMES) {
                    duration = BEN_SPEAK_MAX_FRAMES;
                }
                ben_power_speak(cmd.text);
                sSpeakTimer = duration;
                break;
            }
            case BEN_POWER_HUSH:
                ben_power_hush();
                sSpeakTimer = 0;
                break;
            default:
                break;
        }
    }

    // Upkeep: tick down the active line and hide it when its time is up. This is
    // the simplest instance of the "obstruction always releases" guardrail that
    // every timed power (freeze, lock, ...) will rely on later.
    if (sSpeakTimer > 0) {
        sSpeakTimer--;
        if (sSpeakTimer == 0) {
            ben_power_hush();
        }
    }
}
