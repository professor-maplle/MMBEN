#include "ben/ben.h"

// Top-level wiring for the BEN mod: bring BEN to life at boot, then give it a
// heartbeat every gameplay frame. The heartbeat is deliberately tiny — pick a
// director, let it queue intentions, let the executor carry them out — so the
// interesting logic stays in the directors and powers, not here.

BenBrainMode gBenBrainMode = BEN_BRAIN_AI;

RECOMP_CALLBACK("*", recomp_on_init)
void ben_on_init(void) {
    ben_queue_init();
    ben_powers_init(); // builds the recompui overlay
    recomp_printf("[BEN] awake.\n");
}

// BEN's heartbeat. Play_UpdateMain runs once per frame during gameplay.
RECOMP_HOOK("Play_UpdateMain")
void ben_on_play_update(PlayState* play) {
    // Read config on the first gameplay frame rather than in recomp_on_init:
    // config reads are only proven safe once the game is running.
    static bool sConfigRead = false;
    if (!sConfigRead) {
        // brain_mode enum: 0 = "AI (Ollama)", 1 = "Scripted" (see mod.toml).
        gBenBrainMode = (BenBrainMode)recomp_get_config_u32("brain_mode");

        // Point the native bridge at the configured Ollama server + model.
        char* endpoint = recomp_get_config_string("ollama_endpoint");
        char* model = recomp_get_config_string("ollama_model");
        if (endpoint != NULL && model != NULL) {
            ben_bridge_configure(endpoint, model);
        }
        if (endpoint != NULL) {
            recomp_free_config_string(endpoint);
        }
        if (model != NULL) {
            recomp_free_config_string(model);
        }

        int pong = ben_bridge_ping();
        recomp_printf("[BEN] brain_mode=%d, bridge ping -> %d\n", (s32)gBenBrainMode, pong);
        sConfigRead = true;
    }

    // Decide who is driving. AI mode goes through the native bridge (canned reply
    // in M2, the local Ollama LLM in M3); Scripted mode uses the built-in policy.
    switch (gBenBrainMode) {
        case BEN_BRAIN_AI:
            ben_director_ai_tick(play);
            break;
        case BEN_BRAIN_SCRIPTED:
        default:
            ben_director_scripted_tick(play);
            break;
    }

    // Carry out whatever the director queued this frame.
    ben_executor_tick(play);
}
