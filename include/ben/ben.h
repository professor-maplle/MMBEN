#ifndef BEN_H
#define BEN_H

// Umbrella include for every BEN translation unit. Pull this in first; it brings
// the decomp + recomp APIs (and therefore the base types) before BEN's own
// headers, which depend on them.
#include "modding.h"
#include "global.h"
#include "recomputils.h"
#include "recompconfig.h"
#include "recompui.h"

#include "ben_command.h"
#include "ben_powers.h"
#include "ben_bridge.h"
#include "ben_actions.h"

// Which director drives BEN. Mirrors the mod.toml `brain_mode` enum option order,
// since recomp_get_config_u32 returns the selected index.
typedef enum {
    BEN_BRAIN_AI = 0,       // "AI (Ollama)"  — native bridge brain (milestone 3)
    BEN_BRAIN_SCRIPTED = 1  // "Scripted"     — built-in policy, no LLM
} BenBrainMode;

extern BenBrainMode gBenBrainMode;

// Senses: reads of live game state that directors gate on. The seed of BEN's
// perception layer. True only when the player is genuinely roaming the world
// (no cutscene/intro, scene transition, or active message).
bool ben_player_in_control(PlayState* play);

// Compose a compact, human-readable snapshot of the current game state (day,
// time, location, form, health) into `out` for BEN to reason about.
void ben_describe_situation(PlayState* play, char* out, u32 max);

// Directors: policies that observe the game and emit BenCommands. The scripted
// one is the deterministic fallback; the AI one drives BEN via the native bridge.
void ben_director_scripted_tick(PlayState* play);
void ben_director_ai_tick(PlayState* play);

// Executor: drains the command queue and runs powers. Game thread only.
// Takes `play` because some powers manipulate live game systems (e.g. messages).
void ben_executor_tick(PlayState* play);

#endif
