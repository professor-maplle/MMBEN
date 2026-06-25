#include "ben/ben.h"

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

void ben_power_spawn_statue(PlayState* play) {
    Player* player = GET_PLAYER(play);
    if (player == NULL) {
        return;
    }

    // Spawn the Elegy of Emptiness statue (En_Torch2) of the player's current form
    // where they stand, facing as they face — exactly how the game makes it
    // (z_player.c func_80848640), minus the respawn-point setup and transform
    // beam. En_Torch2 uses gameplay_keep, so it is safe to spawn in any scene.
    Actor_Spawn(&play->actorCtx, play, ACTOR_EN_TORCH2,
                player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z,
                0, player->actor.shape.rot.y, 0,
                player->transformation);

    recomp_printf("[BEN] statue spawned (form %d)\n", (s32)player->transformation);
}
