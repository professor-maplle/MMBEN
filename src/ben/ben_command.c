#include "ben/ben.h"

// Fixed-capacity ring buffer. Small on purpose: under normal play BEN issues a
// trickle of commands and the executor drains them every frame, so overflow only
// happens if a director goes haywire — in which case dropping is the safe choice.
#define BEN_QUEUE_CAP 16

static BenCommand sQueue[BEN_QUEUE_CAP];
static u32 sHead;  // index of the next command to pop
static u32 sTail;  // index of the next slot to push into
static u32 sCount; // number of queued commands

void ben_queue_init(void) {
    sHead = 0;
    sTail = 0;
    sCount = 0;
}

bool ben_queue_push(const BenCommand* cmd) {
    if (sCount >= BEN_QUEUE_CAP) {
        return false; // full: drop
    }
    sQueue[sTail] = *cmd;
    sTail = (sTail + 1) % BEN_QUEUE_CAP;
    sCount++;
    return true;
}

bool ben_queue_pop(BenCommand* out) {
    if (sCount == 0) {
        return false;
    }
    *out = sQueue[sHead];
    sHead = (sHead + 1) % BEN_QUEUE_CAP;
    sCount--;
    return true;
}
