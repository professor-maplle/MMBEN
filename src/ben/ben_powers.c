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

// --- TEXTBOX: BEN hijacks the game's native dialogue system ------------------
//
// Sentinel text id: not a real message and below the 0x4E20 credits cutoff, so
// Message_OpenText takes the English (NES) path and Message_FindMessageNES
// harmlessly falls back to the first table entry (valid DMA). We immediately
// overwrite that loaded message with BEN's own bytes before it gets decoded.
#define BEN_TEXT_ID 0x3FFF

// MM NES message format: 11-byte header, ASCII body, 0xBF (MESSAGE_END) terminator.
#define BEN_MSG_END 0xBF

void ben_power_textbox(PlayState* play, const char* text) {
    MessageContext* msgCtx = &play->msgCtx;
    Font* font = &msgCtx->font;
    char* buf = font->msgBuf.schar;
    s32 i;
    s32 j;

    // Only intrude when nothing else is on screen, so we don't stomp a real
    // textbox or fire during a state that can't display one.
    if (msgCtx->msgMode != MSGMODE_NONE) {
        return;
    }

    // Load a real message (safe DMA + message-system setup), then overwrite it.
    Message_StartTextbox(play, BEN_TEXT_ID, NULL);

    // 11-byte header: unk11F08 = 0x0000 -> textBoxType 0 (black box); itemId 0xFE
    // (none); remaining header fields zero. Message_DecodeNES calls
    // Message_DecodeHeader, which consumes exactly these 11 bytes.
    i = 0;
    buf[i++] = 0x00; buf[i++] = 0x00;            // unk11F08 (box type/pos/flags)
    buf[i++] = (char)0xFE;                        // itemId: none
    buf[i++] = (char)0xFF; buf[i++] = (char)0xFF; // nextTextId = 0xFFFF: no follow-up box
    buf[i++] = 0x00; buf[i++] = 0x00;            // unk1206C
    buf[i++] = 0x00; buf[i++] = 0x00;       // unk12070
    buf[i++] = 0x00; buf[i++] = 0x00;       // unk12074

    // ASCII body (the NES font is ASCII-indexed), leaving room for the terminator.
    for (j = 0; text[j] != '\0' && i < (s32)(sizeof(font->msgBuf.schar) - 1); j++) {
        buf[i++] = (s8)text[j];
    }
    buf[i++] = (s8)BEN_MSG_END;

    msgCtx->msgLength = i;
    msgCtx->msgBufPos = 0;
    msgCtx->decodedTextLen = 0;

    // Re-parse the header for OUR bytes (mirrors Message_OpenText) so the black
    // box is chosen instead of the sentinel message's box type.
    msgCtx->unk11F08 = (((u8)buf[0]) << 8) | (u8)buf[1];
    msgCtx->unk11F18 = (msgCtx->unk11F08 & 0xF000) >> 12;
    msgCtx->textBoxType = (msgCtx->unk11F08 & 0xF00) >> 8;
    msgCtx->textBoxPos = (msgCtx->unk11F08 & 0xF0) >> 4;
    msgCtx->unk11F0C = msgCtx->unk11F08 & 0xF;
    msgCtx->itemId = 0xFE;

    recomp_printf("[BEN] textbox: %s\n", text);
}
