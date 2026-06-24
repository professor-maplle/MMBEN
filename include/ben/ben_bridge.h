#ifndef BEN_BRIDGE_H
#define BEN_BRIDGE_H

#include "modding.h"

// Imports from BEN's native bridge library (native/ben_bridge.cpp), which is
// declared in mod.toml's `native_libraries`. The "." dependency resolves against
// this mod's own exports (its code exports, then its native library exports) by
// function name, so each name here must match an exported native function.

// Round-trip probe: returns a sentinel, proving the native side is loaded.
RECOMP_IMPORT(".", int ben_bridge_ping(void));

// Point the bridge at an Ollama server + model (from mod config). Call once at
// startup before think().
RECOMP_IMPORT(".", void ben_bridge_configure(const char* endpoint, const char* model));

// Begin "thinking" about a situation in the background (non-blocking). The reply
// is retrieved later via ben_bridge_poll. In M2 the bridge returns a canned line
// after a short delay; M3 routes this through the local Ollama LLM.
RECOMP_IMPORT(".", void ben_bridge_think(const char* situation));

// If an action is ready, copy its text into `out` (NUL-terminated) and return the
// action code (BEN_ACTION_*, see ben_actions.h); otherwise return BEN_ACTION_NONE.
// Call once per frame.
RECOMP_IMPORT(".", int ben_bridge_poll(char* out, int max_len));

#endif
