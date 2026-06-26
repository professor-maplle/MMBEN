#include "ben/ben.h"
#include "overlays/actors/ovl_En_Torch2/z_en_torch2.h" // EnTorch2 (BEN's statue) struct

// BEN's voice: a passive, full-screen overlay with a single line of text anchored
// near the bottom of the screen. It never captures input, so the game keeps
// playing underneath while BEN speaks.
//
// recompui rules learned the hard way on this runtime:
//   * Building the context is done once at init, inside open/close.
//   * Mutating a resource (e.g. set_text) REQUIRES the context to be open, or it
//     traps — so every text change is wrapped in open/close.
//   * show_context is safe from the game thread, but hide_context is NOT (it
//     segfaults). So we show once and "hide" by blanking the text instead; an
//     empty, input-transparent overlay is invisible.

static RecompuiContext sCtx = RECOMPUI_NULL_CONTEXT;
static RecompuiResource sLabel = RECOMPUI_NULL_RESOURCE;
static bool sContextShown; // true after the first show; we never hide

void ben_powers_init(void) {
    RecompuiColor textColor = { 198, 16, 16, 255 }; // a sickly, drowned red

    sCtx = recompui_create_context();
    recompui_open_context(sCtx);

    RecompuiResource root = recompui_context_root(sCtx);

    // Cover the whole screen so we can anchor the text against the edges.
    recompui_set_position(root, POSITION_ABSOLUTE);
    recompui_set_top(root, 0.0f, UNIT_DP);
    recompui_set_left(root, 0.0f, UNIT_DP);
    recompui_set_right(root, 0.0f, UNIT_DP);
    recompui_set_bottom(root, 0.0f, UNIT_DP);
    recompui_set_width_auto(root);
    recompui_set_height_auto(root);

    // Bottom-centered, with breathing room from the screen edge.
    recompui_set_display(root, DISPLAY_FLEX);
    recompui_set_flex_direction(root, FLEX_DIRECTION_COLUMN);
    recompui_set_justify_content(root, JUSTIFY_CONTENT_FLEX_END);
    recompui_set_align_items(root, ALIGN_ITEMS_CENTER);
    recompui_set_padding_bottom(root, 96.0f, UNIT_DP);

    sLabel = recompui_create_label(sCtx, root, "", LABELSTYLE_LARGE);
    recompui_set_color(sLabel, &textColor);
    recompui_set_font_size(sLabel, 56.0f, UNIT_DP);
    recompui_set_text_align(sLabel, TEXT_ALIGN_CENTER);

    recompui_close_context(sCtx);

    // BEN's voice must never steal control from the player.
    recompui_set_context_captures_input(sCtx, 0);
    recompui_set_context_captures_mouse(sCtx, 0);

    sContextShown = false;
}

// Set the overlay's line. Mutating a resource requires the context open.
static void ben_set_line(const char* text) {
    recompui_open_context(sCtx);
    recompui_set_text(sLabel, text);
    recompui_close_context(sCtx);
}

void ben_power_speak(const char* text) {
    if (sCtx == RECOMPUI_NULL_CONTEXT) {
        return; // overlay not built yet
    }
    ben_set_line(text);

    // Reveal the overlay once; from then on it stays shown and we just swap text.
    if (!sContextShown) {
        recompui_show_context(sCtx);
        sContextShown = true;
    }
    recomp_printf("[BEN] speak: %s\n", text);
}

void ben_power_hush(void) {
    if (sCtx == RECOMPUI_NULL_CONTEXT || !sContextShown) {
        return;
    }
    ben_set_line(""); // blank the line; the (now-empty) overlay stays shown
    recomp_printf("[BEN] hush\n");
}

// --- TEXTBOX: BEN wears the game's native dialogue system --------------------
//
// BEN_TEXT_ID (in ben_powers.h) is an unused id below the 0x4E20 credits cutoff,
// so Message_OpenText takes the English (NES) path and Message_FindMessageNES
// harmlessly falls back to the first table entry (valid DMA). We overwrite that
// loaded message with BEN's own bytes before it gets decoded.
//
// MM NES message format: 11-byte header, ASCII body, 0xBF (MESSAGE_END) terminator.
#define BEN_MSG_END     0xBF
#define BEN_MSG_NEWLINE 0x11 // MM NES newline control code
// The renderer tracks only 3 lines (msgCtx->unk11F1A[3]); a 4th line overflows
// it and crashes, and an unbroken line runs off the box. So we word-wrap BEN's
// text into at most BEN_MAX_LINES lines of about BEN_WRAP_WIDTH characters. The
// working single line "i know you're there." (~20 chars) sets the safe width.
#define BEN_WRAP_WIDTH  28
#define BEN_MAX_LINES   3

void ben_write_message(PlayState* play, const char* text, bool keep_header) {
    MessageContext* msgCtx = &play->msgCtx;
    Font* font = &msgCtx->font;
    char* buf = font->msgBuf.schar;
    s32 i;
    s32 j;

    // The message format is an 11-byte header followed by the body. For a hijack
    // we keep the loaded header exactly as-is (box type, nextTextId, end behavior)
    // so the conversation behaves like the original and the NPC never freezes;
    // for BEN's own box we write a fresh black-box header. Either way the body
    // starts at byte 11.
    if (keep_header) {
        i = 11;
    } else {
        i = 0;
        buf[i++] = 0x00; buf[i++] = 0x00;            // unk11F08 -> textBoxType 0 (black)
        buf[i++] = (char)0xFE;                        // itemId: none
        buf[i++] = (char)0xFF; buf[i++] = (char)0xFF; // nextTextId = 0xFFFF (no follow-up)
        buf[i++] = 0x00; buf[i++] = 0x00;            // unk1206C
        buf[i++] = 0x00; buf[i++] = 0x00;       // unk12070
        buf[i++] = 0x00; buf[i++] = 0x00;       // unk12074
    }

    // ASCII body (the NES font is ASCII-indexed), word-wrapped into lines so it
    // fits the box and stays within the renderer's 3-line limit.
    {
        s32 line_len = 0;
        s32 lines = 1;
        s32 cap = (s32)sizeof(font->msgBuf.schar) - 2; // leave room for END + NUL

        j = 0;
        while (text[j] != '\0' && i < cap) {
            // Measure the next word [j, k).
            s32 k = j;
            while (text[k] != '\0' && text[k] != ' ') {
                k++;
            }
            s32 word_len = k - j;

            if (line_len > 0 && line_len + 1 + word_len > BEN_WRAP_WIDTH) {
                // Word won't fit on this line: break to a new one, or stop if we
                // have used all the lines the box can hold.
                if (lines >= BEN_MAX_LINES) {
                    break;
                }
                buf[i++] = (char)BEN_MSG_NEWLINE;
                lines++;
                line_len = 0;
            } else if (line_len > 0) {
                buf[i++] = ' ';
                line_len++;
            }

            // Emit the word (a single over-long word is hard-truncated to fit).
            for (s32 w = 0; w < word_len && i < cap; w++) {
                buf[i++] = text[j + w];
                line_len++;
            }

            j = k;
            while (text[j] == ' ') {
                j++;
            }
        }
    }
    buf[i++] = (char)BEN_MSG_END;

    msgCtx->msgLength = i;
    msgCtx->msgBufPos = 0;
    msgCtx->decodedTextLen = 0;

    if (!keep_header) {
        // Re-parse our fresh header (mirrors Message_OpenText) so the black box is
        // chosen. For a hijack we leave the original parse alone so the NPC's flow
        // is byte-for-byte unchanged.
        msgCtx->unk11F08 = (((u8)buf[0]) << 8) | (u8)buf[1];
        msgCtx->unk11F18 = (msgCtx->unk11F08 & 0xF000) >> 12;
        msgCtx->textBoxType = (msgCtx->unk11F08 & 0xF00) >> 8;
        msgCtx->textBoxPos = (msgCtx->unk11F08 & 0xF0) >> 4;
        msgCtx->unk11F0C = msgCtx->unk11F08 & 0xF;
        msgCtx->itemId = 0xFE;
    }
}

void ben_power_textbox(PlayState* play, const char* text) {
    // Only intrude when nothing else is on screen.
    if (play->msgCtx.msgMode != MSGMODE_NONE) {
        return;
    }
    // Load a real message (safe DMA + message-system setup), then overwrite it
    // with BEN's own fresh black box.
    Message_StartTextbox(play, BEN_TEXT_ID, NULL);
    ben_write_message(play, text, false);
    recomp_printf("[BEN] interrupt: %s\n", text);
}

// BEN's avatar is a single Elegy of Emptiness statue (En_Torch2). We keep exactly
// one: rather than spawn a new one (which accumulates statues AND, on despawn,
// sets a respawn point because the Elegy statue is a warp point), we relocate the
// existing one the same way the game moves an Elegy shell. Always Human form.
static Actor* sBenStatue = NULL;

static void ben_place_statue(PlayState* play, f32 x, f32 y, f32 z, s16 yaw) {
    // Validate our tracked statue against the live actor list (it's gone after a
    // scene change), so we never touch a stale pointer.
    bool alive = false;
    if (sBenStatue != NULL) {
        Actor* a = play->actorCtx.actorLists[ACTORCAT_ITEMACTION].first;
        while (a != NULL) {
            if (a == sBenStatue) {
                alive = true;
                break;
            }
            a = a->next;
        }
        if (!alive) {
            sBenStatue = NULL;
        }
    }

    if (alive) {
        // Relocate the existing statue (mirrors the game's Elegy-shell move).
        EnTorch2* statue = (EnTorch2*)sBenStatue;
        sBenStatue->world.pos.x = x;
        sBenStatue->world.pos.y = y;
        sBenStatue->world.pos.z = z;
        sBenStatue->home.pos = sBenStatue->world.pos;
        sBenStatue->shape.rot.y = yaw;
        sBenStatue->home.rot.y = yaw;
        statue->state = 0;
        statue->framesUntilNextState = 20;
    } else {
        sBenStatue = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_TORCH2, x, y, z, 0, yaw, 0,
                                 PLAYER_FORM_HUMAN);
    }
}

// place: where BEN appears, relative to the player ("far" = watch from a distance
// [default], "behind" = loom behind, "near" = close ahead, "here" = at the player).
void ben_power_spawn_statue(PlayState* play, const char* place) {
    Player* player = GET_PLAYER(play);
    if (player == NULL) {
        return;
    }

    s16 pyaw = player->actor.shape.rot.y;
    f32 px = player->actor.world.pos.x;
    f32 py = player->actor.world.pos.y;
    f32 pz = player->actor.world.pos.z;
    f32 sn = Math_SinS(pyaw);
    f32 cs = Math_CosS(pyaw);

    f32 dist = 500.0f;       // far ahead by default — watching from a distance
    f32 dir = 1.0f;          // +1 ahead of the player, -1 behind
    s16 yaw = pyaw + 0x8000; // face back toward the player

    char c = (place != NULL) ? place[0] : 'f';
    if (c == 'b' || c == 'B') {        // behind
        dist = 120.0f;
        dir = -1.0f;
        yaw = pyaw;
    } else if (c == 'n' || c == 'N') { // near, ahead
        dist = 180.0f;
    } else if (c == 'h' || c == 'H') { // here, at the player
        dist = 0.0f;
        dir = 0.0f;
    }

    f32 x = px + (sn * dist * dir);
    f32 z = pz + (cs * dist * dir);

    // Y uses the player's height (no floor raycast yet) - fine on flatter ground.
    ben_place_statue(play, x, py, z, yaw);

    recomp_printf("[BEN] statue placed (%s) at %d,%d\n", (place != NULL) ? place : "far",
                  (s32)x, (s32)z);
}

// REVEAL: BEN appears at the player in the Elegy beam of light - "I am here."
// Relocates the single avatar to Link and spawns the Eff_Change beam, exactly as
// the real elegy does. Shared by BEN's autonomous reveal action and the player's
// Elegy song (the patch below).
void ben_power_reveal_statue(PlayState* play) {
    Player* player = GET_PLAYER(play);
    if (player == NULL) {
        return;
    }
    f32 x = player->actor.world.pos.x;
    f32 y = player->actor.world.pos.y;
    f32 z = player->actor.world.pos.z;
    s16 yaw = player->actor.shape.rot.y;

    ben_place_statue(play, x, y, z, yaw);
    Actor_Spawn(&play->actorCtx, play, ACTOR_EFF_CHANGE, x, y, z, 0, yaw, 0,
                (GET_PLAYER_FORM << 3) | player->transformation);

    recomp_printf("[BEN] reveal (elegy beam) at the player\n");
}

// The Elegy of Emptiness summons BEN: replace the game's elegy handler so the
// player's song triggers the very same reveal (one avatar to Link with the beam),
// rather than creating a separate per-form statue.
RECOMP_PATCH void func_80848640(PlayState* play, Player* this) {
    (void)this;
    ben_power_reveal_statue(play);
}
