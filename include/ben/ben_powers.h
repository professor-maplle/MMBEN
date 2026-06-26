#ifndef BEN_POWERS_H
#define BEN_POWERS_H

// "BEN's hands": the only code that actually manipulates the game. Each power is
// invoked by the executor in response to a BenCommand, never directly by a
// director. New abilities (move the statue, lock the player, glitch the screen)
// are added here as the mod grows.

// Build BEN's on-screen voice overlay. Call once, from recomp_on_init.
void ben_powers_init(void);

// SPEAK: set the dialogue text and reveal the overlay. The executor hides it
// again when the command's display timer elapses.
void ben_power_speak(const char* text);

// HUSH: hide the dialogue overlay immediately.
void ben_power_hush(void);

// Sentinel text id BEN uses for his own textboxes (an unused id below the credits
// cutoff). Shared so the hijack hook can tell BEN's own boxes from the game's.
#define BEN_TEXT_ID 0x3FFF

// Overwrite the loaded message's text with BEN's words.
//   keep_header = false: write a fresh black-box header too (BEN's own box).
//   keep_header = true:  leave the loaded 11-byte header untouched and replace
//     only the words, so a hijacked NPC/sign behaves exactly like its real line
//     and its state machine ends cleanly (no freeze).
void ben_write_message(PlayState* play, const char* text, bool keep_header);

// TEXTBOX: force BEN's own dialogue box on screen now (reserved for urgent lines).
// No-op if a message is already up.
void ben_power_textbox(PlayState* play, const char* text);

// SPAWN_STATUE: place BEN's avatar — a single Human Elegy of Emptiness statue —
// somewhere relative to the player (`place`: "far"/"behind"/"near"/"here").
// Relocates the one existing statue rather than spawning more. Silent (no anim).
void ben_power_spawn_statue(PlayState* play, const char* place);

// REVEAL_STATUE: BEN appears at the player in the Elegy of Emptiness beam of light
// — the "be seen" mode. Same effect the player's Elegy song now triggers.
void ben_power_reveal_statue(PlayState* play);

#endif
