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
