#include "ben/ben.h"

// BEN hijacking the game's own textboxes.
//
// This is BEN's primary voice. When the player opens a sign or talks to an NPC
// and BEN has a line waiting, his words bleed in instead of the real text - far
// more unsettling than him interrupting on his own. His own forced textbox
// (ben_power_textbox) is reserved for urgent moments.
//
// We hook Message_OpenText, which every textbox path funnels through. The entry
// hook decides whether to hijack this box and stashes `play` (return hooks get no
// arguments); the return hook overwrites the just-loaded message with BEN's text
// before it is decoded.

static char sPending[BEN_TEXT_MAX];
static bool sPendingReady;

static PlayState* sHookPlay;
static bool       sHijackThisOne;
static u16        sHookTextId;

void ben_hijack_set_pending(const char* text) {
    u32 i = 0;
    while (text[i] != '\0' && i < BEN_TEXT_MAX - 1) {
        sPending[i] = text[i];
        i++;
    }
    sPending[i] = '\0';
    sPendingReady = true;
}

bool ben_hijack_pending_ready(void) {
    return sPendingReady;
}

// Bleed BEN's words into the message IN PLACE: overwrite only the leading run of
// printable letter bytes with BEN's text, leaving every byte's position and the
// length untouched. We walk the body and STOP at the first control code that
// isn't a known single safe byte (text color 0x00-0x08, newline 0x11) - because
// other codes carry argument bytes (sound ids, shift amounts, delays) that can
// fall in the printable range, and overwriting one corrupts a value the game
// later uses as an index, crashing. Stopping early at worst trims BEN's line; it
// never desyncs a conversation (structure is unchanged) and never corrupts an
// argument. Returns true if at least one of BEN's characters was placed.
static bool ben_hijack_substitute(MessageContext* msgCtx, const char* text) {
    char* buf = msgCtx->font.msgBuf.schar;
    bool placed = false;
    s32 t = 0; // index into BEN's text
    s32 k;

    // Body starts after the 11-byte header.
    for (k = 11; k < 1279; k++) {
        u8 c = (u8)buf[k];
        if (c >= 0x20 && c <= 0x7E) {
            // A printable letter slot: overwrite with BEN's char, or pad with a
            // space once his line is spent.
            if (text[t] != '\0') {
                buf[k] = text[t++];
                placed = true;
            } else {
                buf[k] = ' ';
            }
            continue;
        }
        if ((c <= 0x08) || (c == 0x11)) {
            // Text-color / newline: a single safe byte, pass over it.
            continue;
        }
        // Anything else may be a control code with argument bytes, a box break,
        // or the terminator. Stop rather than risk corrupting an argument.
        break;
    }
    return placed;
}

RECOMP_HOOK("Message_OpenText")
void ben_hijack_on_open(PlayState* play, u16 textId) {
    sHookPlay = play;
    sHookTextId = textId;
    // Candidate for hijack when BEN has a line ready - but never his own box.
    sHijackThisOne = sPendingReady && (textId != BEN_TEXT_ID);
}

// DIAGNOSTIC: dump the first 40 raw body bytes (hex) of a message we're about to
// hijack, in one line so it flushes intact. After a crash, the LAST [BENHEX] line
// is the message that broke - I can read its structure from these bytes.
static void ben_dump_message(MessageContext* msgCtx, u16 textId) {
    static const char H[] = "0123456789ABCDEF";
    const char* buf = msgCtx->font.msgBuf.schar;
    char hex[160];
    u32 p = 0;
    s32 d;
    for (d = 11; d < 11 + 40; d++) {
        u8 b = (u8)buf[d];
        hex[p++] = H[(b >> 4) & 0xF];
        hex[p++] = H[b & 0xF];
        hex[p++] = ' ';
    }
    hex[p] = '\0';
    recomp_printf("[BENHEX] textId=%04X: %s\n", (u32)textId, hex);
}

RECOMP_HOOK_RETURN("Message_OpenText")
void ben_hijack_on_open_return(void) {
    if (!sHijackThisOne || (sHookPlay == NULL) || !sPendingReady) {
        return;
    }
    sHijackThisOne = false;

    // Log the message we're about to alter so a crash log identifies it.
    ben_dump_message(&sHookPlay->msgCtx, sHookTextId);

    // Substitute BEN's words into the box in place (structure untouched). Only
    // consume the pending line if we actually placed it (skip boxes with no text,
    // e.g. an item-icon-only box, so BEN's line waits for a real one).
    if (ben_hijack_substitute(&sHookPlay->msgCtx, sPending)) {
        sPendingReady = false;
        recomp_printf("[BEN] hijacked a textbox: %s\n", sPending);
    }
}
