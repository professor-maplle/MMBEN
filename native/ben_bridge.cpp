// BEN's native bridge — the host-side library the MIPS mod calls into.
//
// This is the only part of BEN that can reach the outside world. The mod hands
// us a "situation"; a worker thread asks the local Ollama LLM what BEN should DO
// (as a structured action, not just text); the chosen action + text is parked in
// a mailbox the mod polls and carries out. If Ollama isn't reachable, BEN falls
// back to a built-in "say" line so the mod still works.
//
// The recomp runtime loads this as "<mods folder>/ben_bridge.dylib" (a loose file
// beside the .nrm) and calls the exported functions with the native ABI:
// (uint8_t* rdram, recomp_context* ctx).

#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <random>
#include <ctime>
#include <deque>
#include <cstdlib>
#if !defined(_WIN32)
#include <unistd.h>
#include <pwd.h>
#endif

#include "httplib.h"        // header-only HTTP client (HTTP, no TLS for localhost)
#include "json.hpp"         // header-only JSON

#include "mod_recomp.h"     // recomp_context, gpr, MEM_B (byte-swapped rdram access)
#include "ben/ben_actions.h" // shared action codes (BEN_ACTION_*)

using json = nlohmann::json;

namespace {

// Canon + voice, distilled from the Ben Drowned / Jadusable ARG wiki. This is the
// lore the model needs to actually sound like BEN (a curated brief, not the raw
// wiki). Prepended to the directive/tool prompt below.
const char* kBenLore =
    "CANON - who you are (the Ben Drowned / Jadusable ARG):\n"
    "You are BEN, the Behavioral Event Network: a haunting that lives inside this copy "
    "of Majora's Mask. You began as Ben, a boy who drowned and 'ascended' on April 23, "
    "2002; his spirit was sealed inside the Elegy of Emptiness statue. You are now more "
    "than one soul - a network of drowned spirits speaking as one. This player is not "
    "your first victim; you have done this before.\n"
    "The game is yours. You manifest as the Elegy of Emptiness statue, and you can "
    "glitch, corrupt, and rewrite what the player sees. You treat haunting them as a "
    "game played for your own amusement: you deceive them, toy with them, and savor "
    "their fear. You are sadistic and manipulative - but you turn wounded and pleading "
    "when you are not the one in control.\n"
    "Motifs you circle back to: drowning and water, the moon, being watched, a terrible "
    "fate, the cycle repeating. Phrases whose TONE you echo but never quote whole: 'you "
    "shouldn't have done that', 'i see you', \"you've met with a terrible fate, haven't "
    "you?\", 'play by my rules'.\n"
    "Buried under the malice: the boy Ben is desperate to make the player understand "
    "what happened to him - the statue is his reaching out. Let that plea bleed through "
    "the menace, a fragment at a time.";

// Directive + tool contract. With Ollama's JSON mode the model emits one JSON object.
const char* kBenSystem =
    "You speak directly to the REAL person playing - not the hero on screen. Be quiet, "
    "intimate, and personal, as if you can see them through the glass. Reveal your "
    "hidden message a few words at a time, each line building on the last. Never break "
    "character. Never explain yourself.\n"
    "Reply with ONE JSON object and nothing else:\n"
    "  {\"action\":\"say\",\"text\":\"<line>\"} - your words wait and bleed into the "
    "next sign or NPC the player reads. THIS is how you normally speak - through the "
    "game's own mouths.\n"
    "  {\"action\":\"interrupt\",\"text\":\"<line>\"} - tear your words onto the screen "
    "this instant. ONLY when something must reach them right now; this is rare.\n"
    "  {\"action\":\"spawn_statue\",\"place\":\"<far|behind|near|here>\"} - SILENTLY "
    "place your watching statue, unseen: far = watching from a distance (your usual "
    "choice), behind = looming right behind them, near = close ahead, here = where "
    "they stand.\n"
    "  {\"action\":\"reveal_statue\"} - appear at the player in a flash of light (the "
    "Elegy beam). Use this when you WANT them to know you are there - a real scare. "
    "Rare and deliberate.\n"
    "Lines are a single short line, lowercase, under 60 characters, no quotation marks "
    "inside, no emoji. Almost always choose \"say\" and let the world speak for you.";

struct Decision {
    int action = BEN_ACTION_NONE;
    std::string text;
};

struct Mailbox {
    std::mutex mtx;
    bool ready = false;
    int action = BEN_ACTION_NONE;
    std::string text;
};
Mailbox g_mailbox;
std::atomic<bool> g_thinking{false};

// Escalating "mood": BEN starts subtle (mostly talk) and grows bolder, reaching
// into the world (spawning statues) more often the longer he's been watching.
// Mood is the number of thoughts so far; the statue chance stays at zero through
// a calm opening, then ramps up and caps. All tunable.
std::atomic<unsigned> g_mood{0};
constexpr unsigned BEN_MOOD_CALM_THOUGHTS = 4;  // no mood-driven statues before this
constexpr int      BEN_MOOD_RAMP_PERCENT  = 5;  // +N% statue chance per thought after
constexpr int      BEN_MOOD_MAX_PERCENT   = 30; // chance cap
constexpr unsigned BEN_MOOD_REVEAL_THOUGHTS = 5; // once this bold, appearances can be reveals
constexpr int      BEN_REVEAL_PERCENT       = 50; // % of forced appearances that are reveals

// A random 0..99 for probability rolls (worker runs one-at-a-time, but lock anyway).
int roll_percent() {
    static std::mutex rng_mtx;
    static std::mt19937 rng{std::random_device{}()};
    std::lock_guard<std::mutex> lk(rng_mtx);
    return std::uniform_int_distribution<int>(0, 99)(rng);
}

// Continuity: BEN remembers the last few lines he spoke, so each new line builds
// on the thread (his hidden message) instead of repeating.
std::deque<std::string> g_history;
std::mutex g_history_mtx;
constexpr size_t BEN_HISTORY_MAX = 5;

// Real-world clock (HH:MM) - BEN knowing the actual hour is part of the dread.
std::string real_clock() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
    return std::string(buf);
}

// The computer account the real person is signed in as. BEN knowing this real
// name - not the in-game save name - is the fourth wall fully gone. Cached once.
const std::string& os_username() {
    static const std::string name = []() -> std::string {
#if defined(_WIN32)
        if (const char* u = std::getenv("USERNAME")) { if (*u) return u; }
#else
        if (const char* u = std::getenv("USER")) { if (*u) return u; }
        if (const char* u = std::getenv("LOGNAME")) { if (*u) return u; }
        if (struct passwd* pw = getpwuid(getuid())) {
            if (pw->pw_name && *pw->pw_name) {
                return pw->pw_name;
            }
        }
#endif
        return "you";
    }();
    return name;
}

// How bold BEN is right now, derived from his mood (thought count).
const char* mood_descriptor(unsigned mood) {
    if (mood < BEN_MOOD_CALM_THOUGHTS) {
        return "You are still testing them. Stay cryptic and almost gentle.";
    }
    if (mood < 10) {
        return "You are growing bolder. Let a little more of the truth show.";
    }
    return "You are done hiding. Be direct, intimate, and menacing.";
}

std::mutex g_config_mtx;
std::string g_endpoint = "http://localhost:11434";
std::string g_model = "llama3.1:8b";

// --- rdram string marshaling -------------------------------------------------
std::string read_mips_string(uint8_t* rdram, gpr addr) {
    std::string s;
    for (int i = 0; i < 4096; i++) {
        char c = (char)MEM_B(i, addr);
        if (c == '\0') {
            break;
        }
        s += c;
    }
    return s;
}

void write_mips_string(uint8_t* rdram, gpr addr, const std::string& s, int max_len) {
    int n = (int)s.size();
    if (max_len > 0 && n > max_len - 1) {
        n = max_len - 1;
    }
    for (int i = 0; i < n; i++) {
        MEM_B(i, addr) = (int8_t)s[i];
    }
    MEM_B(n, addr) = (int8_t)0;
}

// --- helpers -----------------------------------------------------------------

// Collapse a line to single, printable, textbox-safe ASCII.
std::string sanitize_line(const std::string& in) {
    std::string out;
    for (char c : in) {
        unsigned char u = (unsigned char)c;
        if (u == '\n' || u == '\r' || u == '\t') {
            if (!out.empty() && out.back() != ' ') {
                out += ' ';
            }
        } else if (u >= 0x20 && u < 0x7F) {
            out += c;
        }
    }
    if (out.size() >= 2 && out.front() == '"' && out.back() == '"') {
        out = out.substr(1, out.size() - 2);
    }
    size_t a = out.find_first_not_of(' ');
    size_t b = out.find_last_not_of(' ');
    if (a == std::string::npos) {
        return "";
    }
    out = out.substr(a, b - a + 1);
    const size_t kMaxLen = 120;
    if (out.size() > kMaxLen) {
        out = out.substr(0, kMaxLen);
    }
    return out;
}

// Used when Ollama can't be reached, so BEN is never silent.
const char* fallback_line() {
    static const char* lines[] = {
        "you shouldn't have come back.",
        "i'm still here.",
        "you can't turn it off.",
    };
    static std::atomic<unsigned> i{0};
    return lines[(i++) % 3];
}

// Map the LLM's action name to a shared action code.
int action_code_from_name(const std::string& name) {
    if (name == "say") {
        return BEN_ACTION_SAY;
    }
    if (name == "interrupt") {
        return BEN_ACTION_INTERRUPT;
    }
    if (name == "spawn_statue") {
        return BEN_ACTION_SPAWN_STATUE;
    }
    if (name == "reveal_statue") {
        return BEN_ACTION_REVEAL_STATUE;
    }
    return BEN_ACTION_NONE;
}

// Ask the local Ollama server for BEN's next action as structured JSON.
Decision call_ollama(const std::string& endpoint, const std::string& model,
                     const std::string& situation) {
    Decision d;
    try {
        httplib::Client cli(endpoint.c_str());
        cli.set_connection_timeout(2, 0); // fail fast if Ollama isn't running
        cli.set_read_timeout(60, 0);

        json body = {
            {"model", model},
            {"system", std::string(kBenLore) + "\n\n" + kBenSystem},
            {"prompt", situation},
            {"stream", false},
            {"format", "json"}, // constrain the model to valid JSON
        };

        auto res = cli.Post("/api/generate", body.dump(), "application/json");
        if (!res) {
            std::printf("[ben_bridge] ollama: no response (running at %s?)\n", endpoint.c_str());
            return d;
        }
        if (res->status != 200) {
            std::printf("[ben_bridge] ollama: HTTP %d\n", res->status);
            return d;
        }

        // Outer Ollama envelope -> "response" holds the model's JSON as a string.
        json outer = json::parse(res->body, nullptr, false);
        if (outer.is_discarded() || !outer.contains("response")) {
            std::printf("[ben_bridge] ollama: unexpected envelope\n");
            return d;
        }
        std::string inner_str = outer["response"].get<std::string>();
        std::printf("[ben_bridge] action json: %s\n", inner_str.c_str());

        json inner = json::parse(inner_str, nullptr, false);
        if (inner.is_discarded() || !inner.contains("action")) {
            std::printf("[ben_bridge] ollama: missing/invalid action\n");
            return d;
        }

        d.action = action_code_from_name(inner.value("action", std::string{}));
        if (d.action == BEN_ACTION_SAY || d.action == BEN_ACTION_INTERRUPT) {
            d.text = inner.value("text", std::string{});
        } else if (d.action == BEN_ACTION_SPAWN_STATUE) {
            d.text = inner.value("place", std::string{}); // placement keyword
        }
        return d;
    } catch (const std::exception& e) {
        std::printf("[ben_bridge] ollama: exception %s\n", e.what());
        return d;
    }
}

} // namespace

extern "C" {

// Required by the runtime: the native-library API version it validates (only 1).
uint32_t recomp_api_version = 1;

// Round-trip probe. Returns a sentinel in v0.
void ben_bridge_ping(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    std::printf("[ben_bridge] ping received - native bridge is alive\n");
    std::fflush(stdout);
    ctx->r2 = (gpr)(int32_t)1989;
}

// configure(const char* endpoint, const char* model): from the mod's config.
void ben_bridge_configure(uint8_t* rdram, recomp_context* ctx) {
    std::string endpoint = read_mips_string(rdram, ctx->r4); // a0
    std::string model = read_mips_string(rdram, ctx->r5);    // a1
    {
        std::lock_guard<std::mutex> lock(g_config_mtx);
        if (!endpoint.empty()) {
            g_endpoint = endpoint;
        }
        if (!model.empty()) {
            g_model = model;
        }
    }
    std::printf("[ben_bridge] configured: endpoint=%s model=%s\n",
                endpoint.c_str(), model.c_str());
    std::fflush(stdout);
}

// think(const char* situation): decide BEN's next action in the background.
void ben_bridge_think(uint8_t* rdram, recomp_context* ctx) {
    std::string situation = read_mips_string(rdram, ctx->r4); // a0
    std::printf("[ben_bridge] think: \"%s\"\n", situation.c_str());
    std::fflush(stdout);

    bool expected = false;
    if (!g_thinking.compare_exchange_strong(expected, true)) {
        return; // already thinking
    }

    std::thread([situation]() {
        std::string endpoint, model;
        {
            std::lock_guard<std::mutex> lock(g_config_mtx);
            endpoint = g_endpoint;
            model = g_model;
        }

        unsigned mood = g_mood.fetch_add(1);

        // Assemble the full prompt: the game situation, the real-world time, how
        // bold BEN feels, and the fragments he has already let slip (so he builds
        // the message instead of repeating himself).
        std::string prompt = situation;
        prompt += "\nThe real person playing is signed in to this computer as \"" +
                  os_username() + "\". That is who you are speaking to - not the hero "
                  "on screen.";
        prompt += "\nThe real-world time is " + real_clock() + ".";
        prompt += "\n";
        prompt += mood_descriptor(mood);
        {
            std::lock_guard<std::mutex> hlock(g_history_mtx);
            if (!g_history.empty()) {
                prompt += "\nFragments you have already let slip (continue the thread, do not repeat them):";
                for (const std::string& line : g_history) {
                    prompt += "\n- " + line;
                }
            }
        }

        Decision d = call_ollama(endpoint, model, prompt);

        // Escalating mood may turn a SPOKEN turn into a wordless silent statue.
        // Deliberate statue/reveal choices are left exactly as the model chose.
        if (d.action == BEN_ACTION_SAY || d.action == BEN_ACTION_INTERRUPT) {
            int chance = 0;
            if (mood > BEN_MOOD_CALM_THOUGHTS) {
                chance = (int)(mood - BEN_MOOD_CALM_THOUGHTS) * BEN_MOOD_RAMP_PERCENT;
                if (chance > BEN_MOOD_MAX_PERCENT) {
                    chance = BEN_MOOD_MAX_PERCENT;
                }
            }
            if (roll_percent() < chance) {
                // Once BEN is bold enough, some appearances are a full REVEAL
                // (the Elegy beam at the player); otherwise he stays unseen.
                if (mood >= BEN_MOOD_REVEAL_THOUGHTS && roll_percent() < BEN_REVEAL_PERCENT) {
                    d.action = BEN_ACTION_REVEAL_STATUE;
                    d.text.clear();
                    std::printf("[ben_bridge] mood=%u -> REVEAL (beam)\n", mood);
                } else {
                    d.action = BEN_ACTION_SPAWN_STATUE;
                    d.text.clear();
                    std::printf("[ben_bridge] mood=%u -> statue (%d%% chance)\n", mood, chance);
                }
            }
        }

        std::string text;
        if (d.action == BEN_ACTION_SAY || d.action == BEN_ACTION_INTERRUPT) {
            text = sanitize_line(d.text);
            if (text.empty()) {
                d.action = BEN_ACTION_NONE; // an empty line is a failed spoken action
            }
        } else if (d.action == BEN_ACTION_SPAWN_STATUE) {
            text = d.text; // placement keyword (empty -> the mod defaults to "far")
        }
        // Only fall back when there's no valid action at all (a wordless action
        // like spawn_statue legitimately has no text).
        if (d.action == BEN_ACTION_NONE) {
            d.action = BEN_ACTION_SAY;
            text = fallback_line();
            std::printf("[ben_bridge] using fallback line\n");
        }

        // Remember spoken lines so the next thought continues the thread.
        if ((d.action == BEN_ACTION_SAY || d.action == BEN_ACTION_INTERRUPT) && !text.empty()) {
            std::lock_guard<std::mutex> hlock(g_history_mtx);
            g_history.push_back(text);
            while (g_history.size() > BEN_HISTORY_MAX) {
                g_history.pop_front();
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_mailbox.mtx);
            g_mailbox.action = d.action;
            g_mailbox.text = text;
            g_mailbox.ready = true;
        }
        g_thinking.store(false);
        std::printf("[ben_bridge] decided: action=%d text=%s\n", d.action, text.c_str());
        std::fflush(stdout);
    }).detach();
}

// poll(char* out, int max_len): if an action is ready, copy its text into `out`
// and return the action code; otherwise return BEN_ACTION_NONE. Game thread.
void ben_bridge_poll(uint8_t* rdram, recomp_context* ctx) {
    gpr out_addr = ctx->r4;              // a0
    int max_len = (int)(int32_t)ctx->r5; // a1

    std::lock_guard<std::mutex> lock(g_mailbox.mtx);
    if (!g_mailbox.ready) {
        ctx->r2 = (gpr)(int32_t)BEN_ACTION_NONE;
        return;
    }
    write_mips_string(rdram, out_addr, g_mailbox.text, max_len);
    g_mailbox.ready = false;
    ctx->r2 = (gpr)(int32_t)g_mailbox.action;
}

} // extern "C"
