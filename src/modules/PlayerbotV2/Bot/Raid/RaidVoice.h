// RaidVoice - a 40-man raid that talks like a crowd of players: not 39 bots at once, and not a silent room.
//
// Up to 0.1.36 a group over 15 was silent for all of the bots' own chatter (the quiet-raid rule: forty bots talking at once is
// spam), so in Blackwing Lair only the callouts and the leader's word on a mistake were ever said (Karrz, 2026-11-13: "a wee bit
// more chatty"). This is the raid's shared voice instead:
//  * the bots' own chatter (loot, a death, a ready check, a new zone) is back, with one line for the whole raid per gap;
//  * after a kill a few bots say gg / grats, after a wipe a few talk on the run back and the leader calls the rebuff;
//  * between pulls somebody says something now and then;
//  * the leader says what the next boss does when a player walks up to it.
//
//   Playerbot.Raid.Talk  0 off (the silent raid as before), 1 normal (the default), 2 chatty
//
// The pure part (lines and limits) so it can be unit tested; RaidBrain.cpp and BotTextMgr.cpp pick the bots and send. Every line
// is English and translated per reader where it leaves the server (BotLanguage::GroupChat and the Russian catalog); with the bot
// bridge answering chat the program may word a line first (raid_line events), else the fixed line is said.

#pragma once

#include <cstdint>
#include <iterator>
#include <string_view>
#include <unordered_map>

namespace Playerbot::Raid {

enum class RaidTalk : uint8_t { Off = 0, Normal = 1, Chatty = 2 };

inline RaidTalk RaidTalkFromConfig(int32_t v)
{
    return v <= 0 ? RaidTalk::Off : (v == 1 ? RaidTalk::Normal : RaidTalk::Chatty);
}

// A group bigger than this is a raid that talks with one voice (a 20-man and a 40-man; a five-man party grown into a raid group
// for a quest keeps the party's own chatter).
constexpr uint32_t kRaidVoiceGroupSize = 15;

struct RaidVoiceTuning
{
    uint32_t chatterGapMs = 0;    // the bots' own chatter: one line for the whole raid per this
    uint32_t chatterBotMs = 0;    // ... and one per bot per this
    uint32_t killCrowd = 0;       // bots who say something after a boss kill besides the leader (1 to this many)
    uint32_t runBackCrowd = 0;    // bots who talk on the run back after a wipe (1 to this many)
    uint32_t idleGapMs = 0;       // between pulls: one line from somebody about this often (0.7x to 1.3x)
    uint32_t idleQuietMs = 0;     // ... once the raid has been out of combat this long
    uint32_t explainAgainMs = 0;  // the leader explains a boss again after this (and after every wipe on it)
};

inline RaidVoiceTuning VoiceTuning(RaidTalk t)
{
    switch (t)
    {
        case RaidTalk::Normal: return { 30000, 120000, 3, 2, 150000, 20000, 1800000 };
        case RaidTalk::Chatty: return { 15000, 60000, 5, 3, 75000, 15000, 900000 };
        default:               return {};
    }
}

namespace VoiceDetail {

template <size_t N>
inline std::string_view PickOf(char const* const (&lines)[N], uint32_t pick) { return lines[pick % N]; }

// Short, lower case, the way people type in raid chat; no pronoun or past tense that needs a gender (the Russian forms).
inline constexpr char const* kKill[] = {
    "gg", "gg all", "grats all", "nice one", "ez", "gj everyone", "finally", "gz", "ty all", "good kill", "that went well",
    "clean kill", "loot pls" };
inline constexpr char const* kRunBack[] = {
    "brb repair", "rez me pls", "need water before the next one", "rebuff pls", "who has a soulstone up", "running back",
    "one more try", "that hurt", "drinking, 1 min", "ok again", "my repair bill though", "we almost had it" };
inline constexpr char const* kLeaderRebuff[] = {
    "rez up, rebuff, we go again", "everyone back, buff up and drink", "rez and rebuff, same plan" };
inline constexpr char const* kIdle[] = {
    "anyone got a spare mana pot", "this place is huge", "how many bosses left", "who's tanking the next one",
    "brb 1 min", "mage water pls", "can i get an int buff", "fort pls", "loot's been good so far", "careful with the trash here",
    "anyone selling flasks after", "who's on kicks next", "need a soulstone here", "my bags are full already" };
inline constexpr char const* kLeaderIdle[] = {
    "everyone drink up, next pull soon", "stack up, we pull in a minute", "buffs check before the next one" };

} // namespace VoiceDetail

inline std::string_view KillLine(uint32_t pick)          { return VoiceDetail::PickOf(VoiceDetail::kKill, pick); }
inline std::string_view RunBackLine(uint32_t pick)       { return VoiceDetail::PickOf(VoiceDetail::kRunBack, pick); }
inline std::string_view LeaderRebuffLine(uint32_t pick)  { return VoiceDetail::PickOf(VoiceDetail::kLeaderRebuff, pick); }
inline std::string_view IdleLine(uint32_t pick)          { return VoiceDetail::PickOf(VoiceDetail::kIdle, pick); }
inline std::string_view LeaderIdleLine(uint32_t pick)    { return VoiceDetail::PickOf(VoiceDetail::kLeaderIdle, pick); }

// The leader's word on a boss before the pull, the way a raid leader explains it; empty for a creature with none. Molten Core,
// Onyxia and Blackwing Lair (Lord Victor Nefarius for Nefarian's first phase).
inline std::string_view BossBrief(uint32_t entry)
{
    switch (entry)
    {
        // Molten Core
        case 12118: return "lucifron: adds first, decurse fast, sheep the mind controlled";
        case 11982: return "magmadar: hunters tranq the frenzy, stay out of the fire, fear ward the tank";
        case 12259: return "gehennas: adds first, decurse, get out of the rain of fire";
        case 12057: return "garr: banish the adds, kill them one at a time, tank garr away from the raid";
        case 12264: return "shazzrah: decurse fast, he blinks, so stay near the tank";
        case 12056: return "geddon: living bomb on you? run out. stay out of inferno";
        case 12098: return "sulfuron: priests first, kick their heals";
        case 11988: return "golemagg: tanks on the dogs, melee watch your stacks, everyone else on golemagg";
        case 12018: return "majordomo: adds only, kick the healers, stop hitting a reflect shield";
        case 11502: return "ragnaros: melee out for the knockback, kill the sons of flame when he goes under";
        // Onyxia
        case 10184: return "onyxia: tank her at the back, nobody in front or behind her, phase 3 watch the fear";
        // Blackwing Lair
        case 12435: return "razorgore: one dps takes the orb and breaks the eggs, the rest hold the doors";
        case 13020: return "vael: burning adrenaline on you? run out to the wall. tanks swap on it";
        case 12017: return "broodlord: tank him in the corner, watch the knock away, stay off the suppression devices";
        case 11983: return "firemaw: drop your buffet stacks behind the pillar, healers keep the tank up from cover";
        case 14601: return "ebonroc: tanks swap on shadow of ebonroc, everyone else behind him";
        case 11981: return "flamegor: tank him facing away, hunters tranq the frenzy";
        case 14020: return "chromaggus: hide behind the pillars when he breathes, dispel the afflictions, hunters tranq";
        case 10162: return "nefarius: kill the drakonids on both sides, sheep the mind controlled, then nefarian lands";
        case 11583: return "nefarian: tank him facing away, watch your class call, bones come up at 20%";
        default:    return {};
    }
}

// The raid's chatter gate: one line for the whole raid per chatterGapMs, one per bot per chatterBotMs. Not thread safe (the
// caller holds its lock).
class RaidVoiceGate
{
public:
    bool Allow(uint64_t group, uint64_t bot, RaidVoiceTuning const& t, uint32_t now)
    {
        if (!t.chatterGapMs)
            return false;
        if (auto g = group_.find(group); g != group_.end() && now - g->second < t.chatterGapMs)
            return false;
        if (auto b = bot_.find(bot); b != bot_.end() && now - b->second < t.chatterBotMs)
            return false;
        uint32_t const stamp = now ? now : 1u;
        group_[group] = stamp;
        bot_[bot] = stamp;
        return true;
    }

    // Drops what no longer limits anything (call now and then).
    void Prune(uint32_t now, uint32_t keepMs)
    {
        for (auto* m : { &group_, &bot_ })
            for (auto it = m->begin(); it != m->end();)
                it = now - it->second >= keepMs ? m->erase(it) : std::next(it);
    }

private:
    std::unordered_map<uint64_t, uint32_t> group_, bot_;
};

// When the leader explains a boss again: never twice within explainAgainMs, at once after a wipe on it.
class BossBriefGate
{
public:
    bool Allow(uint64_t group, uint32_t entry, RaidVoiceTuning const& t, uint32_t now)
    {
        if (!t.explainAgainMs || BossBrief(entry).empty())
            return false;
        uint32_t& last = said_[Key(group, entry)];
        if (last && now - last < t.explainAgainMs)
            return false;
        last = now ? now : 1u;
        return true;
    }

    void Wiped(uint64_t group, uint32_t entry) { said_.erase(Key(group, entry)); }

private:
    static uint64_t Key(uint64_t group, uint32_t entry) { return (group << 24) ^ entry; }
    std::unordered_map<uint64_t, uint32_t> said_;
};

} // namespace Playerbot::Raid
