#include "ben/ben.h"

// BEN's senses: reads of live game state. Two jobs:
//   1. ben_player_in_control - the gate every director uses (no cutscene/etc).
//   2. ben_describe_situation - a compact, human-readable snapshot of what's
//      happening, fed to the LLM so BEN's words reflect the real game.
// This is where the perception layer grows (idle, progress flags, deaths, ...).

bool ben_player_in_control(PlayState* play) {
    return (play->msgCtx.msgMode == MSGMODE_NONE)         // no message on screen
        && (play->csCtx.state == CS_STATE_IDLE)           // no cutscene playing
        && (play->transitionTrigger == TRANS_TRIGGER_OFF) // not mid scene transition
        && !Player_InCsMode(play);                         // player not locked by a cs
}

// --- tiny string builder (no sprintf dependency) -----------------------------
static void s_append(char* out, u32* pos, u32 max, const char* s) {
    u32 p = *pos;
    while (*s != '\0' && p < max - 1) {
        out[p++] = *s++;
    }
    *pos = p;
}

static void s_append_uint(char* out, u32* pos, u32 max, u32 v) {
    char tmp[12];
    s32 n = 0;
    if (v == 0) {
        tmp[n++] = '0';
    }
    while (v > 0 && n < 11) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    u32 p = *pos;
    while (n > 0 && p < max - 1) {
        out[p++] = tmp[--n];
    }
    *pos = p;
}

// --- readable names for the values BEN perceives -----------------------------
static const char* ben_scene_name(s16 sceneId) {
    switch (sceneId) {
        case SCENE_CLOCKTOWER:        return "South Clock Town";
        case SCENE_BACKTOWN:          return "North Clock Town";
        case SCENE_TOWN:              return "East Clock Town";
        case SCENE_ICHIBA:            return "West Clock Town";
        case SCENE_INSIDETOWER:       return "the Clock Tower interior";
        case SCENE_OPENINGDAN:        return "the path before Clock Town";
        case SCENE_00KEIKOKU:         return "Termina Field";
        case SCENE_20SICHITAI2:       return "the Southern Swamp";
        case SCENE_22DEKUCITY:        return "the Deku Palace";
        case SCENE_MITURIN:           return "Woodfall Temple";
        case SCENE_10YUKIYAMANOMURA2: return "Mountain Village";
        case SCENE_HAKUGIN:           return "Snowhead Temple";
        case SCENE_30GYOSON:          return "the Great Bay Coast";
        case SCENE_SEA:               return "Great Bay Temple";
        case SCENE_IKANA:             return "Ikana Canyon";
        case SCENE_INISIE_N:          return "Stone Tower Temple";
        case SCENE_INISIE_R:          return "the inverted Stone Tower Temple";
        case SCENE_F01:               return "Romani Ranch";
        case SCENE_SOUGEN:            return "the Moon";
        default:                      return "somewhere in Termina";
    }
}

static const char* ben_form_name(u8 form) {
    switch (form) {
        case PLAYER_FORM_FIERCE_DEITY: return "the Fierce Deity";
        case PLAYER_FORM_GORON:        return "a Goron";
        case PLAYER_FORM_ZORA:         return "a Zora";
        case PLAYER_FORM_DEKU:         return "a Deku Scrub";
        case PLAYER_FORM_HUMAN:        return "human";
        default:                       return "an unknown shape";
    }
}

void ben_describe_situation(PlayState* play, char* out, u32 max) {
    u32 pos = 0;

    s16 health = gSaveContext.save.saveInfo.playerData.health;
    s16 capacity = gSaveContext.save.saveInfo.playerData.healthCapacity;
    s16 hearts = (health < 0) ? 0 : (health / 16);
    s16 maxHearts = (capacity < 0) ? 0 : (capacity / 16);
    u32 hour = ((u32)gSaveContext.save.time * 24) / 0x10000;
    s32 day = gSaveContext.save.day;
    bool night = (hour < 6) || (hour >= 18);

    s_append(out, &pos, max, "It is day ");
    s_append_uint(out, &pos, max, (u32)(day < 0 ? 0 : day));
    s_append(out, &pos, max, ", around hour ");
    s_append_uint(out, &pos, max, hour);
    s_append(out, &pos, max, night ? " of 24 (night). " : " of 24 (daytime). ");
    s_append(out, &pos, max, "The player is in ");
    s_append(out, &pos, max, ben_scene_name(play->sceneId));
    s_append(out, &pos, max, " as ");
    s_append(out, &pos, max, ben_form_name(GET_PLAYER_FORM));
    s_append(out, &pos, max, ", with ");
    s_append_uint(out, &pos, max, (u32)hearts);
    s_append(out, &pos, max, " of ");
    s_append_uint(out, &pos, max, (u32)maxHearts);
    s_append(out, &pos, max, " hearts left.");

    out[(pos < max) ? pos : (max - 1)] = '\0';
}
