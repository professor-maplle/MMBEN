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

// SPAWN_STATUE: make BEN's avatar — the Elegy of Emptiness statue of the player's
// current form — appear where the player stands, exactly as the game creates it.
void ben_power_spawn_statue(PlayState* play);

#endif
