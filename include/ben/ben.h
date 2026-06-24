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

// Which director drives BEN. Mirrors the mod.toml `brain_mode` enum option order,
// since recomp_get_config_u32 returns the selected index.
typedef enum {
    BEN_BRAIN_AI = 0,       // "AI (Ollama)"  — native bridge brain (milestone 3)
    BEN_BRAIN_SCRIPTED = 1  // "Scripted"     — built-in policy, no LLM
} BenBrainMode;

extern BenBrainMode gBenBrainMode;

// Director: a policy that observes the game and emits BenCommands.
void ben_director_scripted_tick(PlayState* play);

// Executor: drains the command queue and runs powers. Game thread only.
// Takes `play` because some powers manipulate live game systems (e.g. messages).
void ben_executor_tick(PlayState* play);

#endif
