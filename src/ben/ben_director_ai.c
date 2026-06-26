#include "ben/ben.h"

// AI director: drives BEN through the native bridge, continuously. BEN THINKS on
// a cadence regardless of game state - even during conversations and cutscenes -
// so he always has a fresh line ready to bleed into the next textbox (including
// cutscene dialogue). Only his *intrusive* actions (forcing his own box, spawning
// a statue) are gated to real gameplay, so he never breaks a cutscene with them.

#define BEN_AI_FIRST_DELAY 120 // ~4s before BEN's first thought
#define BEN_AI_COOLDOWN    600 // ~20s between thoughts thereafter

typedef enum {
    BEN_AI_COOLING, // waiting out the cooldown before the next thought
    BEN_AI_THINKING // asked the bridge; polling for the chosen action
} BenAiState;

static BenAiState sState = BEN_AI_COOLING;
static s32        sTimer;
static s32        sDelay = BEN_AI_FIRST_DELAY;

void ben_director_ai_tick(PlayState* play) {
    if (sState == BEN_AI_COOLING) {
        // The cooldown advances no matter what the player is doing.
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

    BenCommand cmd = { 0 };
    switch (action) {
        case BEN_ACTION_SAY:
            // Passive: wait and bleed into the next textbox the game opens - an
            // NPC, a sign, or cutscene dialogue. Safe in any state.
            ben_hijack_set_pending(text);
            break;
        case BEN_ACTION_INTERRUPT:
            // Intrusive: only force BEN's own box during free gameplay, so it
            // never interrupts a cutscene or an active conversation.
            if (ben_player_in_control(play)) {
                cmd.power = BEN_POWER_TEXTBOX;
                ben_cmd_set_text(&cmd, text);
                ben_queue_push(&cmd);
            }
            break;
        case BEN_ACTION_SPAWN_STATUE:
            // Intrusive: only place the avatar during free gameplay. text holds
            // BEN's chosen placement keyword.
            if (ben_player_in_control(play)) {
                cmd.power = BEN_POWER_SPAWN_STATUE;
                ben_cmd_set_text(&cmd, text);
                ben_queue_push(&cmd);
            }
            break;
        case BEN_ACTION_REVEAL_STATUE:
            // Intrusive: only reveal during free gameplay (spawns the beam).
            if (ben_player_in_control(play)) {
                cmd.power = BEN_POWER_REVEAL_STATUE;
                ben_queue_push(&cmd);
            }
            break;
        default:
            break;
    }

    sTimer = 0;
    sDelay = BEN_AI_COOLDOWN;
    sState = BEN_AI_COOLING;
}
