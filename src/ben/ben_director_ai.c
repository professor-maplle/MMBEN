#include "ben/ben.h"

// AI director: drives BEN through the native bridge, continuously. The loop is:
//   cool down (only while the player is in control) -> ask the bridge to think
//   about the current situation -> poll until an action arrives -> carry it out
//   -> back to the cooldown. So BEN keeps reacting as you play, not just once.
//
// Because the bridge is asynchronous, none of this blocks a frame. Pacing is a
// placeholder for M4b's escalating "mood" (he should grow bolder over time).

#define BEN_AI_FIRST_DELAY 120 // ~4s of control before BEN's first action
#define BEN_AI_COOLDOWN    600 // ~20s between actions thereafter

typedef enum {
    BEN_AI_COOLING, // waiting out the cooldown before the next thought
    BEN_AI_THINKING // asked the bridge; polling for the chosen action
} BenAiState;

static BenAiState sState = BEN_AI_COOLING;
static s32        sTimer;
static s32        sDelay = BEN_AI_FIRST_DELAY;

void ben_director_ai_tick(PlayState* play) {
    // Only act while the player is genuinely in control. We pause (rather than
    // reset) so a brief cutscene/textbox doesn't restart the whole wait.
    if (!ben_player_in_control(play)) {
        return;
    }

    if (sState == BEN_AI_COOLING) {
        sTimer++;
        if (sTimer < sDelay) {
            return;
        }
        char situation[256];
        ben_describe_situation(play, situation, sizeof(situation));
        ben_bridge_think(situation);
        recomp_printf("[BEN] (ai) think: %s\n", situation);
        sState = BEN_AI_THINKING;
        return;
    }

    // BEN_AI_THINKING: poll for the action the worker thread is preparing.
    char text[BEN_TEXT_MAX];
    int action = ben_bridge_poll(text, BEN_TEXT_MAX);
    if (action == BEN_ACTION_NONE) {
        return; // not ready yet
    }

    recomp_printf("[BEN] (ai) action=%d: %s\n", action, text);

    // Map the chosen action to a power. The toolkit grows here; unknown actions
    // are simply ignored.
    BenCommand cmd = { 0 };
    switch (action) {
        case BEN_ACTION_SAY:
            // BEN's usual voice: wait and bleed into the next textbox the player
            // opens (a sign, an NPC), rather than interrupting them.
            ben_hijack_set_pending(text);
            break;
        case BEN_ACTION_INTERRUPT:
            // Urgent: force BEN's own textbox on screen right now.
            cmd.power = BEN_POWER_TEXTBOX;
            ben_cmd_set_text(&cmd, text);
            ben_queue_push(&cmd);
            break;
        case BEN_ACTION_SPAWN_STATUE:
            cmd.power = BEN_POWER_SPAWN_STATUE;
            ben_queue_push(&cmd);
            break;
        default:
            break;
    }

    // Back to waiting for the next one.
    sTimer = 0;
    sDelay = BEN_AI_COOLDOWN;
    sState = BEN_AI_COOLING;
}
