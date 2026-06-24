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

// TEXTBOX: speak through the game's own dialogue box by hijacking the message
// system — BEN wearing a native function. The player dismisses it with A/B like
// any textbox. No-op if a message is already on screen.
void ben_power_textbox(PlayState* play, const char* text);

// SPAWN_STATUE: make BEN's avatar — the Elegy of Emptiness statue of the player's
// current form — appear where the player stands, exactly as the game creates it.
void ben_power_spawn_statue(PlayState* play);

#endif
