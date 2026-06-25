#ifndef BEN_ACTIONS_H
#define BEN_ACTIONS_H

// Shared action contract between the mod (MIPS C) and the native bridge (C++).
//
// The LLM chooses an action by name (e.g. "say"); the bridge maps the name to a
// code and delivers it via ben_bridge_poll; the mod maps the code to a power and
// queues a BenCommand. Keeping these codes in one header keeps both sides in sync
// as BEN's toolkit grows.
//
// Pure #defines only (no types), so it is safe to include from both C and C++.

#define BEN_ACTION_NONE         0 // nothing ready yet (poll returns this when idle)
#define BEN_ACTION_SAY          1 // a line that waits to hijack the next game textbox
#define BEN_ACTION_SPAWN_STATUE 2 // make BEN's Elegy statue appear by the player
#define BEN_ACTION_INTERRUPT    3 // urgent: force BEN's textbox on screen right now

#endif
